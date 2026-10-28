#include "WoolooCamera/Graph/CameraRigService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/CameraNode.h"
#include "Core/CameraRigAsset.h"
#include "Core/ObjectTreeGraphRootObject.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace Wooloo::Camera
{
namespace
{
FString Result(const FString& AssetPath, const FString& OldNodePath, const FString& NewNodePath,
    bool bSuccess, bool bDirty, const FString& Message)
{
    auto Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("success"), bSuccess);
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("old_node_path"), OldNodePath);
    Data->SetStringField(TEXT("new_node_path"), NewNodePath);
    Data->SetBoolField(TEXT("dirty"), bDirty);
    Data->SetStringField(TEXT("message"), Message);
    FString Json;
    auto Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Data, Writer);
    return Json;
}

bool InScope(const FString& Path, FString Root)
{
    Root.RemoveFromEnd(TEXT("/"));
    FString RootFilename;
    FString Filename;
    return Root.Len() < NAME_SIZE && Path.Len() < NAME_SIZE
        && FPackageName::IsValidLongPackageName(Root) && FPackageName::IsValidLongPackageName(Path)
        && Root.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1) != INDEX_NONE
        && Path.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase)
        && FPackageName::TryConvertLongPackageNameToFilename(Root, RootFilename)
        && FPackageName::TryConvertLongPackageNameToFilename(Path, Filename)
        && !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),
            FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
}

bool HasChildren(UCameraNode& Node)
{
    // Native views can include empty child slots, which do not make a node non-leaf.
    for (const auto& Child : Node.GetChildren())
        if (Child) return true;
    return false;
}

struct FNodeSlot
{
    UObject* Owner;
    FProperty* Property;
    FObjectProperty* ObjectProperty;
    int32 ArrayIndex = INDEX_NONE;

    void* Value() const
    {
        auto* Container = Property->ContainerPtrToValuePtr<void>(Owner);
        if (ArrayIndex == INDEX_NONE) return Container;
        FScriptArrayHelper Array(CastFieldChecked<FArrayProperty>(Property), Container);
        return Array.GetRawPtr(ArrayIndex);
    }
};

bool GatherSlots(UObject& Owner, UCameraNode& OldNode, UClass& NewClass,
    TArray<FNodeSlot>& Slots, FString& Error)
{
    const auto StartCount = Slots.Num();
    for (TFieldIterator<FProperty> It(Owner.GetClass()); It; ++It)
    {
        auto* Property = *It;
        if (Property->ArrayDim != 1
            || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_NonTransactional)) continue;
        auto* ObjectProperty = CastField<FObjectProperty>(Property);
        auto* ArrayProperty = CastField<FArrayProperty>(Property);
        if (ArrayProperty) ObjectProperty = CastField<FObjectProperty>(ArrayProperty->Inner);
        if (!ObjectProperty || !ObjectProperty->PropertyClass->IsChildOf<UCameraNode>()) continue;

        const auto AddSlot = [&](int32 ArrayIndex, const void* Value)
        {
            if (ObjectProperty->GetObjectPropertyValue(Value) != &OldNode) return true;
            if (!NewClass.IsChildOf(ObjectProperty->PropertyClass))
            {
                Error = TEXT("New node class is incompatible with execution slot: ")
                    + Owner.GetName() + TEXT(".") + Property->GetName();
                return false;
            }
            Slots.Add({ &Owner, Property, ObjectProperty, ArrayIndex });
            return true;
        };

        if (ArrayProperty)
        {
            FScriptArrayHelper Array(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(&Owner));
            for (int32 Index = 0; Index < Array.Num(); ++Index)
                if (!AddSlot(Index, Array.GetRawPtr(Index))) return false;
        }
        else if (!AddSlot(INDEX_NONE, Property->ContainerPtrToValuePtr<void>(&Owner))) return false;
    }

    auto NativeChildCount = 0;
    if (auto* Parent = Cast<UCameraNode>(&Owner))
    {
        for (const auto& Child : Parent->GetChildren())
            if (Child == &OldNode) ++NativeChildCount;
    }
    else if (auto* Rig = Cast<UCameraRigAsset>(&Owner))
    {
        NativeChildCount = Rig->RootNode == &OldNode ? 1 : 0;
    }
    if (NativeChildCount != Slots.Num() - StartCount)
    {
        Error = TEXT("Unsupported custom child storage on: ") + Owner.GetName();
        return false;
    }
    return true;
}

bool ValidateReferences(UCameraRigAsset& Rig, UCameraNode& OldNode,
    const TArray<UObject*>& Objects, const TArray<FNodeSlot>& Slots, FString& Error)
{
    auto RegistryReferences = 0;
    // Inspect reflected containers directly. Calling UObject::Serialize here could run
    // native version migrations, and blanket reference replacement would silently retarget bindings.
    for (const auto* Object : Objects)
    {
        if (Object == &OldNode || Object->IsIn(&OldNode) || Object->HasAnyFlags(RF_Transient)) continue;
        for (TPropertyValueIterator<FObjectPropertyBase> It(Object->GetClass(), Object); It; ++It)
        {
            if (It->Key->GetObjectPropertyValue(It->Value) != &OldNode) continue;
            if (Slots.ContainsByPredicate([&](const auto& Slot)
                { return Slot.Owner == Object && Slot.ObjectProperty == It->Key && Slot.Value() == It->Value; }))
                continue;

            TArray<const FProperty*> Chain;
            It.GetPropertyChain(Chain);
            if (Object == &Rig && !Chain.IsEmpty() && Chain.Last()->GetFName() == TEXT("AllNodeTreeObjects"))
            {
                ++RegistryReferences;
                continue;
            }
            Error = TEXT("Node has an unsupported reference or exposed parameter binding: ")
                + Object->GetPathName(&Rig) + TEXT(".") + It.GetPropertyPathDebugString();
            return false;
        }
    }
    if (RegistryReferences != 1)
    {
        Error = TEXT("Node must have exactly one native node-tree registration.");
        return false;
    }
    return true;
}

bool StageProperties(UCameraNode& Candidate, const TMap<FString, FString>& Properties,
    TArray<FProperty*>& Staged, FString& Error)
{
    TArray<FString> Names;
    Properties.GetKeys(Names);
    Names.Sort();
    for (const auto& Name : Names)
    {
        if (Name.IsEmpty() || Name.Len() >= NAME_SIZE)
        {
            Error = TEXT("Property names must be non-empty native field names shorter than NAME_SIZE.");
            return false;
        }
        auto* Property = Candidate.GetClass()->FindPropertyByName(*Name);
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->ArrayDim != 1
            || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated
                | CPF_DisableEditOnInstance | CPF_NonTransactional | CPF_InstancedReference | CPF_ContainsInstancedReference)
            || Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCameraNode, bIsEnabled)
            || Staged.Contains(Property))
        {
            Error = TEXT("Unsupported, duplicate or reserved editable property: ") + Name;
            return false;
        }
        FStringOutputDevice Errors;
        const auto* Rest = Property->ImportText_Direct(*Properties.FindChecked(Name),
            Property->ContainerPtrToValuePtr<void>(&Candidate), &Candidate, PPF_None, &Errors);
        if (!Rest || !FString(Rest).TrimStartAndEnd().IsEmpty() || !Errors.IsEmpty())
        {
            Error = TEXT("Import failed for ") + Name + TEXT(": ") + Errors;
            return false;
        }
        Staged.Add(Property);
    }
    return true;
}
}

FString ReplaceRigNode(const FString& AssetPath, const FString& AllowedPackageRoot,
    const FString& NodePath, const FString& NewNodeClassPath,
    const TMap<FString, FString>& Properties, const FString& NodeComment)
{
    UCameraRigAsset* Rig = nullptr;
    const auto Refuse = [&](const FString& Message)
    {
        return Result(AssetPath, NodePath, FString(), false, Rig && Rig->GetOutermost()->IsDirty(), Message);
    };
    if (!IsInGameThread() || !GEditor || !GEditor->CanTransact() || GIsTransacting
        || GEditor->PlayWorld || GEditor->IsTransactionActive())
        return Refuse(TEXT("Replacement requires the editor game thread, stopped PIE and an idle transaction buffer."));
    if (!InScope(AssetPath, AllowedPackageRoot))
        return Refuse(TEXT("AssetPath must be a package inside a narrow mounted AllowedPackageRoot, outside engine content."));

    const auto ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
    auto* Asset = FindObject<UObject>(nullptr, *ObjectPath);
    if (!Asset)
    {
        auto& Registry = FAssetRegistryModule::GetRegistry();
        if (Registry.IsLoadingAssets()) return Refuse(TEXT("Wait for Asset Registry discovery to finish."));
        const auto Data = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
        if (!Data.IsValid() || Data.IsRedirector()) return Refuse(TEXT("An existing Camera Rig asset is required; redirectors are not accepted."));
        Asset = Data.GetAsset();
    }
    Rig = Cast<UCameraRigAsset>(Asset);
    if (!Rig || !Rig->GetPathName().Equals(ObjectPath, ESearchCase::IgnoreCase)
        || Rig->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_Transient))
        return Refuse(TEXT("AssetPath did not resolve to the exact editable Camera Rig."));

    auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    // The native toolkit owns a separate cached UEdGraph and exposes no exported rebuild API.
    // Do not leave that cache editable after replacing its backing UObject.
    if (!Editors || Editors->FindEditorForAsset(Rig, false))
        return Refuse(TEXT("Close the target Camera Rig asset editor before replacement; reopen it afterward to rebuild its graph view."));

    TArray<UObject*> Objects;
    GetObjectsWithOuter(Rig, Objects, EGetObjectsFlags::IncludeNestedObjects);
    UCameraNode* OldNode = nullptr;
    for (auto* Object : Objects)
        if (Object->GetPathName(Rig).Equals(NodePath, ESearchCase::CaseSensitive))
            OldNode = Cast<UCameraNode>(Object);
    if (!OldNode || OldNode->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject))
        return Refuse(TEXT("NodePath must exactly identify a CameraNode UObject relative to this Rig."));

    auto* Graph = static_cast<IObjectTreeGraphRootObject*>(Rig);
    TSet<UObject*> Registered;
    Graph->GetConnectableObjects(UCameraRigAsset::NodeTreeGraphName, Registered);
    if (!Registered.Contains(OldNode)) return Refuse(TEXT("Node is not registered in the Rig's node-tree graph."));
    if (HasChildren(*OldNode)) return Refuse(TEXT("Only leaf nodes can be replaced; the selected node has children."));
    for (auto* Object : Registered)
    {
        if (Object && Object != Rig && !Object->IsIn(Rig))
            return Refuse(TEXT("The node-tree graph contains externally owned objects; cross-asset graph replacement is unsupported."));
        if (Object && Object->IsIn(OldNode))
            return Refuse(TEXT("Selected node owns other registered graph objects; subtree migration is unsupported."));
    }
    for (const auto& Connection : Rig->Connections.Connections)
        if (Connection.Source == OldNode || Connection.Target == OldNode)
            return Refuse(TEXT("Selected node participates in a data connection or exposed parameter binding; migrate it explicitly first."));

    if (NewNodeClassPath.Len() >= NAME_SIZE || !FPackageName::IsValidObjectPath(NewNodeClassPath))
        return Refuse(TEXT("NewNodeClassPath must be a full class object path."));
    auto* NewClass = LoadClass<UCameraNode>(nullptr, *NewNodeClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!NewClass || !NewClass->GetPathName().Equals(NewNodeClassPath, ESearchCase::IgnoreCase)
        || NewClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Transient)
        || !OldNode->GetOuter()->IsA(NewClass->ClassWithin) || !GetTransientPackage()->IsA(NewClass->ClassWithin))
        return Refuse(TEXT("NewNodeClassPath must resolve to a concrete CameraNode class with compatible outer requirements."));

    Objects.Insert(Rig, 0);
    TArray<FNodeSlot> Slots;
    FString Error;
    for (auto* Object : Objects)
    {
        if (Object == Rig || (Object->IsA<UCameraNode>() && Registered.Contains(Object)))
            if (!GatherSlots(*Object, *OldNode, *NewClass, Slots, Error)) return Refuse(Error);
    }
    if (!ValidateReferences(*Rig, *OldNode, Objects, Slots, Error)) return Refuse(Error);

    TStrongObjectPtr<UCameraNode> Candidate(NewObject<UCameraNode>(GetTransientPackage(), NewClass, NAME_None, RF_Transient));
    TArray<FProperty*> Staged;
    if (!StageProperties(*Candidate, Properties, Staged, Error)) return Refuse(Error);
    if (HasChildren(*Candidate)) return Refuse(TEXT("The configured replacement must also be a leaf node."));
    for (TPropertyValueIterator<FObjectPropertyBase> It(NewClass, Candidate.Get()); It; ++It)
    {
        const auto* Referenced = It->Key->GetObjectPropertyValue(It->Value);
        if (Referenced && (Referenced == Rig || Referenced->IsIn(Rig) || Referenced->IsA<UCameraNode>()))
            return Refuse(TEXT("Replacement properties cannot introduce graph-object references: ") + It.GetPropertyPathDebugString());
    }

    // All input and structural checks precede the transaction. Cancellation would not
    // roll back arbitrary UObject edits, so there are no validation exits below this point.
    const FScopedTransaction Transaction(NSLOCTEXT("WoolooCamera", "ReplaceRigNode", "Replace Camera Rig leaf node"));
    Rig->SetFlags(RF_Transactional);
    Rig->Modify();
    TMap<UObject*, TSet<FProperty*>> Changed;
    for (const auto& Slot : Slots) Changed.FindOrAdd(Slot.Owner).Add(Slot.Property);
    for (const auto& Entry : Changed)
    {
        Entry.Key->SetFlags(RF_Transactional);
        Entry.Key->Modify();
        for (auto* Property : Entry.Value) Entry.Key->PreEditChange(Property);
    }

    auto* NewNode = NewObject<UCameraNode>(OldNode->GetOuter(), NewClass,
        MakeUniqueObjectName(OldNode->GetOuter(), NewClass), RF_Transactional);
    for (auto* Property : Staged)
        Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(NewNode),
            Property->ContainerPtrToValuePtr<void>(Candidate.Get()));
    NewNode->bIsEnabled = OldNode->bIsEnabled;
    NewNode->GraphNodePos = OldNode->GraphNodePos;
    NewNode->GraphNodeComment = NodeComment;
    NewNode->Modify();

    for (const auto& Slot : Slots) Slot.ObjectProperty->SetObjectPropertyValue(Slot.Value(), NewNode);
    Graph->RemoveConnectableObject(UCameraRigAsset::NodeTreeGraphName, OldNode);
    Graph->AddConnectableObject(UCameraRigAsset::NodeTreeGraphName, NewNode);
    // Keep the old UObject intact: Undo owns its previous graph registration and incoming links.
    for (const auto& Entry : Changed)
    {
        for (auto* Property : Entry.Value)
        {
            FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
            Entry.Key->PostEditChangeProperty(Event);
        }
    }
    NewNode->PostEditChange();
    static_cast<IHasCameraBuildStatus*>(Rig)->DirtyBuildStatus();
    Rig->MarkPackageDirty();
    return Result(AssetPath, NodePath, NewNode->GetPathName(Rig), true, Rig->GetOutermost()->IsDirty(),
        TEXT("Replaced the leaf node. The Rig is unsaved and needs a build; inspect it before separately building or saving."));
}
}
