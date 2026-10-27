#include "WoolooWidget/Layout/WidgetLayoutService.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/BlueprintExtension.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlot.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_GetClassDefaults.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

namespace Wooloo::Widget
{
namespace
{
struct FLayoutSource
{
    const UWidgetBlueprint* Blueprint = nullptr;
    const UClass* Class = nullptr;
    const UWidgetTree* Tree = nullptr;

    const UWidget* Root() const { return Tree ? Tree->RootWidget.Get() : nullptr; }
};

TArray<FLayoutSource> Sources(const UWidgetBlueprint& Asset, TArray<FString>& Issues)
{
    TArray<FLayoutSource> Out;
    Out.Add({ &Asset, Asset.GeneratedClass, Asset.WidgetTree });
    TSet<const UClass*> Visited;
    TSet<const UWidgetBlueprint*> Blueprints;
    Blueprints.Add(&Asset);
    auto* Class = Asset.ParentClass.Get();
    while (Class && Class->IsChildOf(UUserWidget::StaticClass()))
    {
        const auto* Blueprint = Cast<UWidgetBlueprint>(Class->ClassGeneratedBy);
        if (Visited.Contains(Class) || (Blueprint && Blueprints.Contains(Blueprint)))
        {
            Issues.Add(TEXT("Cyclic widget Blueprint inheritance."));
            break;
        }
        Visited.Add(Class);
        if (Blueprint) Blueprints.Add(Blueprint);
        const auto* Generated = Cast<UWidgetBlueprintGeneratedClass>(Class);
        const auto* Tree = Blueprint ? Blueprint->WidgetTree.Get()
            : (Generated ? Generated->GetWidgetTreeArchetype() : nullptr);
        Out.Add({ Blueprint, Class, Tree });
        // ParentClass reflects unsaved reparenting; GetSuperClass can still describe an older compile.
        Class = Blueprint ? Blueprint->ParentClass.Get() : Class->GetSuperClass();
    }
    return Out;
}

TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const auto& Value : Values) Out.Add(MakeShared<FJsonValueString>(Value));
    return Out;
}

TSharedRef<FJsonObject> RootJson(const UWidget* Root)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("present"), Root != nullptr);
    Out->SetStringField(TEXT("name"), Root ? Root->GetName() : FString());
    Out->SetStringField(TEXT("class"), Root ? Root->GetClass()->GetPathName() : FString());
    return Out;
}

TSharedRef<FJsonObject> SourceJson(const FLayoutSource& Source)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("blueprint"), Source.Blueprint ? Source.Blueprint->GetPathName() : FString());
    Out->SetStringField(TEXT("class"), Source.Class ? Source.Class->GetPathName() : FString());
    Out->SetStringField(TEXT("tree"), Source.Tree ? Source.Tree->GetPathName() : FString());
    Out->SetStringField(TEXT("basis"), Source.Blueprint ? TEXT("Source") : TEXT("ClassArchetype"));
    Out->SetObjectField(TEXT("root"), RootJson(Source.Root()));
    if (Source.Blueprint)
        Out->SetStringField(TEXT("compile_status"), UEnum::GetValueAsString(Source.Blueprint->Status));
    return Out;
}

TSharedRef<FJsonObject> Snapshot(const UWidgetBlueprint& Asset)
{
    TArray<FString> Issues;
    const auto Chain = Sources(Asset, Issues);
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("asset"), Asset.GetPathName());
    Out->SetStringField(TEXT("compile_status"), UEnum::GetValueAsString(Asset.Status));
    Out->SetBoolField(TEXT("package_dirty"), Asset.GetOutermost()->IsDirty());
    Out->SetObjectField(TEXT("local"), SourceJson(Chain[0]));
    TArray<TSharedPtr<FJsonValue>> Ancestors;
    const FLayoutSource* Effective = nullptr;
    auto RootCount = 0;
    auto bCompiledMayBeStale = false;
    for (auto Index = 0; Index < Chain.Num(); ++Index)
    {
        const auto& Source = Chain[Index];
        if (Index > 0) Ancestors.Add(MakeShared<FJsonValueObject>(SourceJson(Source)));
        if (Source.Root())
        {
            ++RootCount;
            if (!Effective) Effective = &Source;
        }
        if (Source.Blueprint && Source.Blueprint->Status != BS_UpToDate && Source.Blueprint->Status != BS_UpToDateWithWarnings)
            bCompiledMayBeStale = true;
    }
    Out->SetArrayField(TEXT("ancestors"), Ancestors);
    Out->SetStringField(TEXT("source_mode"), !Effective ? TEXT("Empty")
        : (Effective == &Chain[0] ? TEXT("Local") : TEXT("Inherit")));
    Out->SetObjectField(TEXT("source_layout"), Effective ? SourceJson(*Effective) : MakeShared<FJsonObject>());
    if (!Effective) Issues.Add(TEXT("No root widget exists in the source inheritance chain."));
    if (RootCount > 1) Issues.Add(TEXT("Multiple inheritance levels own roots; UE does not support these competing widget hierarchies."));
    if (!Asset.WidgetTree) Issues.Add(TEXT("The Blueprint has no local WidgetTree object."));

    const auto* Generated = Cast<UWidgetBlueprintGeneratedClass>(Asset.GeneratedClass);
    const auto* Owner = Generated ? Generated->FindWidgetTreeOwningClass() : nullptr;
    const auto* CompiledTree = Owner ? Owner->GetWidgetTreeArchetype() : nullptr;
    const auto* CompiledRoot = CompiledTree ? CompiledTree->RootWidget.Get() : nullptr;
    auto Compiled = MakeShared<FJsonObject>();
    Compiled->SetBoolField(TEXT("available"), Generated != nullptr);
    Compiled->SetStringField(TEXT("owner_class"), CompiledRoot ? Owner->GetPathName() : FString());
    Compiled->SetStringField(TEXT("owner_blueprint"), CompiledRoot && Owner->ClassGeneratedBy
        ? Owner->ClassGeneratedBy->GetPathName() : FString());
    Compiled->SetObjectField(TEXT("root"), RootJson(CompiledRoot));
    const auto bSameOwner = Effective && CompiledRoot
        && (Effective->Blueprint ? Effective->Blueprint == Owner->ClassGeneratedBy : Effective->Class == Owner)
        && Effective->Root()->GetFName() == CompiledRoot->GetFName()
        && Effective->Root()->GetClass() == CompiledRoot->GetClass();
    const auto bOwnershipMatches = bSameOwner || (!Effective && Generated && !CompiledRoot);
    Compiled->SetBoolField(TEXT("ownership_matches_source"), bOwnershipMatches);
    Compiled->SetBoolField(TEXT("may_be_stale"), bCompiledMayBeStale || !bOwnershipMatches);
    Out->SetObjectField(TEXT("compiled_layout"), Compiled);
    if (Generated && !bOwnershipMatches) Issues.Add(TEXT("Source and compiled layout ownership differ; compile explicitly to refresh the generated class."));

    TArray<TSharedPtr<FJsonValue>> SlotBindings;
    if (Asset.WidgetTree)
    {
        TArray<FName> Names;
        Asset.WidgetTree->NamedSlotBindings.GetKeys(Names);
        Names.Sort(FNameLexicalLess());
        for (const auto Name : Names)
        {
            auto Slot = MakeShared<FJsonObject>();
            Slot->SetStringField(TEXT("slot"), Name.ToString());
            Slot->SetObjectField(TEXT("content"), RootJson(Asset.WidgetTree->GetContentForSlot(Name)));
            SlotBindings.Add(MakeShared<FJsonValueObject>(Slot));
        }
    }
    Out->SetArrayField(TEXT("named_slot_bindings"), SlotBindings);
    Out->SetArrayField(TEXT("diagnostics"), Strings(Issues));
    return Out;
}

FWoolooWidgetResult Result(bool bSuccess, const FString& Message, const UWidgetBlueprint* Asset = nullptr,
    const TArray<FString>& Issues = {}, bool bChanged = false)
{
    FWoolooWidgetResult Out;
    Out.bSuccess = bSuccess;
    Out.Message = Message;
    auto Json = Asset ? Snapshot(*Asset) : MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("changed"), bChanged);
    Json->SetArrayField(TEXT("blocking_issues"), Strings(Issues));
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Out.Json));
    return Out;
}

bool InScope(const FString& Path, FString Root)
{
    Root.RemoveFromEnd(TEXT("/"));
    FString Filename;
    if (!FPackageName::IsValidLongPackageName(Path) || !FPackageName::IsValidLongPackageName(Root)
        || Root.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1) == INDEX_NONE
        || !Path.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase)
        || !FPackageName::TryConvertLongPackageNameToFilename(Path, Filename)
        || FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), FPaths::ConvertRelativePathToFull(FPaths::EngineDir())))
        return false;
    return !FPackageName::DoesPackageExist(Path, &Filename) || !IFileManager::Get().IsReadOnly(*Filename);
}

UWidgetBlueprint* Load(const FString& Path)
{
    return FPackageName::IsValidLongPackageName(Path) ? LoadObject<UWidgetBlueprint>(nullptr, *Path) : nullptr;
}

TArray<UWidget*> Subtree(UWidget* Root)
{
    TArray<UWidget*> Out;
    if (Root)
    {
        Out.Add(Root);
        UWidgetTree::GetChildWidgets(Root, Out);
    }
    return Out;
}

void CheckRemoval(const UWidgetBlueprint& Asset, const TArray<UWidget*>& Removed, TArray<FString>& Issues)
{
    if (Removed.IsEmpty()) return;
    TSet<FName> Names;
    for (const auto* Widget : Removed) Names.Add(Widget->GetFName());
    TArray<UEdGraph*> Graphs;
    Asset.GetAllGraphs(Graphs);
    for (const auto* Graph : Graphs)
    {
        if (!Graph) continue;
        for (const auto& Node : Graph->Nodes)
        {
            if (!Node) continue;
            auto bUsesRemovedWidget = false;
            if (const auto* Variable = Cast<UK2Node_Variable>(Node))
            {
                const auto* Owner = Variable->VariableReference.GetMemberParentClass();
                bUsesRemovedWidget = Names.Contains(Variable->GetVarName())
                    && (Variable->VariableReference.IsSelfContext() || (Owner
                        && (Owner == Asset.GeneratedClass || Owner == Asset.SkeletonGeneratedClass)));
            }
            if (const auto* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
                bUsesRemovedWidget |= Names.Contains(Event->GetComponentPropertyName());
            if (const auto* Defaults = Cast<UK2Node_GetClassDefaults>(Node);
                Defaults && Defaults->GetInputClass() && (Defaults->GetInputClass() == Asset.GeneratedClass
                    || Defaults->GetInputClass() == Asset.SkeletonGeneratedClass))
            {
                for (const auto* Pin : Defaults->Pins)
                    bUsesRemovedWidget |= Names.Contains(Pin->PinName) && !Pin->LinkedTo.IsEmpty();
            }
            if (const auto* K2Node = Cast<UK2Node>(Node); K2Node && Asset.SkeletonGeneratedClass)
            {
                for (const auto Name : Names)
                    bUsesRemovedWidget |= K2Node->ReferencesVariable(Name, Asset.SkeletonGeneratedClass);
            }
            for (const auto* Pin : Node->Pins)
                bUsesRemovedWidget |= Removed.Contains(Cast<UWidget>(Pin->DefaultObject.Get()));
            if (bUsesRemovedWidget)
                Issues.Add(TEXT("Graph node references a removed widget: ") + Node->GetPathName());
        }
    }
    for (const auto& Binding : Asset.Bindings)
    {
        auto bUsesRemovedWidget = Names.Contains(FName(*Binding.ObjectName)) || Names.Contains(Binding.SourceProperty);
        for (const auto& Segment : Binding.SourcePath.Segments)
            bUsesRemovedWidget |= Names.Contains(Segment.GetMemberName());
        if (bUsesRemovedWidget)
            Issues.Add(TEXT("Property/delegate binding references a removed widget: ") + Binding.ObjectName + TEXT(".") + Binding.PropertyName.ToString());
    }
    for (const auto& Animation : Asset.Animations)
    {
        if (!Animation) continue;
        for (const auto& Binding : Animation->GetBindings())
            if (!Binding.bIsRootWidget && (Names.Contains(Binding.WidgetName) || Names.Contains(Binding.SlotWidgetName)))
                Issues.AddUnique(TEXT("Animation references a removed widget: ") + Animation->GetPathName());
    }
    Asset.WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (Removed.Contains(Widget) || !Widget->Navigation) return;
        const auto* Navigation = Widget->Navigation.Get();
        for (const auto* Direction : { &Navigation->Up, &Navigation->Down, &Navigation->Left,
            &Navigation->Right, &Navigation->Next, &Navigation->Previous })
            if (Direction->Rule == EUINavigationRule::Explicit && Names.Contains(Direction->WidgetToFocus))
                Issues.AddUnique(TEXT("Preserved widget navigation references a removed widget: ") + Widget->GetName());
    });
    const auto* Defaults = Asset.GeneratedClass ? Cast<UUserWidget>(Asset.GeneratedClass->GetDefaultObject(false)) : nullptr;
    if (Defaults && Names.Contains(Defaults->GetDesiredFocusWidgetName()))
        Issues.Add(TEXT("Desired focus references a removed widget: ") + Defaults->GetDesiredFocusWidgetName().ToString());
    if (Asset.ParentClass)
        for (TFieldIterator<FObjectPropertyBase> Property(Asset.ParentClass); Property; ++Property)
            if (Names.Contains(Property->GetFName()) && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(*Property))
                Issues.Add(TEXT("BindWidget property references a removed widget: ") + Property->GetName());
    // Extensions can own additional name-based bindings (for example view-model bindings).
    // Do not guess at private schemas or silently leave an extension pointing at a removed widget.
    for (const auto& Extension : Asset.GetExtensions())
        if (Extension) Issues.Add(TEXT("Removal cannot validate custom Blueprint extension bindings: ") + Extension->GetPathName());
}

void CheckNamedSlots(const UWidgetBlueprint& Asset, const TArray<FLayoutSource>& Chain,
    const TArray<UWidget*>& Removed, TArray<FString>& Issues)
{
    TMap<FName, const UNamedSlot*> Slots;
    TSet<FName> Occupied;
    TSet<FName> InheritedNames;
    for (auto Index = 1; Index < Chain.Num(); ++Index)
    {
        const auto* Tree = Chain[Index].Tree;
        if (!Tree) continue;
        Tree->ForEachWidget([&](UWidget* Widget)
        {
            if (InheritedNames.Contains(Widget->GetFName()))
                Issues.Add(TEXT("Duplicate inherited widget name: ") + Widget->GetName());
            InheritedNames.Add(Widget->GetFName());
            if (const auto* Slot = Cast<UNamedSlot>(Widget))
            {
                Slots.Add(Slot->GetFName(), Slot);
                if (Slot->GetContent()) Occupied.Add(Slot->GetFName());
            }
        });
        for (const auto& Binding : Tree->NamedSlotBindings)
            if (Binding.Value) Occupied.Add(Binding.Key);
    }
    for (const auto& Binding : Asset.WidgetTree->NamedSlotBindings)
    {
        if (!Binding.Value) continue;
        const auto* Slot = Slots.FindRef(Binding.Key);
        if (!Slot || Slot->bExposeOnInstanceOnly || Occupied.Contains(Binding.Key))
            Issues.Add(TEXT("Named Slot is absent, instance-only or already filled by an ancestor: ") + Binding.Key.ToString());
        for (const auto* Widget : Subtree(Binding.Value))
        {
            if (Removed.Contains(Widget))
                Issues.Add(TEXT("Named Slot content overlaps the root subtree being removed: ") + Widget->GetName());
            if (InheritedNames.Contains(Widget->GetFName()))
                Issues.Add(TEXT("Preserved Named Slot content conflicts with an inherited widget name: ") + Widget->GetName());
            InheritedNames.Add(Widget->GetFName());
        }
    }
}

bool WouldCreateCycle(const UWidgetBlueprint& Asset, const UClass* Class, TSet<const UClass*>& Visiting)
{
    if (!Class || !Class->IsChildOf(UUserWidget::StaticClass())) return false;
    if (Class->ClassGeneratedBy == &Asset || (Asset.GeneratedClass && Class->IsChildOf(Asset.GeneratedClass))) return true;
    if (Visiting.Contains(Class)) return true;
    Visiting.Add(Class);
    const auto* Blueprint = Cast<UWidgetBlueprint>(Class->ClassGeneratedBy);
    const auto* Generated = Cast<UWidgetBlueprintGeneratedClass>(Class);
    const auto* Tree = Blueprint ? Blueprint->WidgetTree.Get() : (Generated ? Generated->GetWidgetTreeArchetype() : nullptr);
    auto bCycle = false;
    if (Tree)
        Tree->ForEachWidget([&](UWidget* Widget) { bCycle |= WouldCreateCycle(Asset, Widget->GetClass(), Visiting); });
    bCycle |= WouldCreateCycle(Asset, Blueprint ? Blueprint->ParentClass.Get() : Class->GetSuperClass(), Visiting);
    Visiting.Remove(Class);
    return bCycle;
}
}

FWoolooWidgetResult Describe(const FString& AssetPath)
{
    if (!IsInGameThread()) return Result(false, TEXT("Widget inspection requires the editor game thread."));
    const auto* Asset = Load(AssetPath);
    return Asset ? Result(true, TEXT("Read-only source and compiled layout snapshots."), Asset)
        : Result(false, TEXT("WidgetBlueprint not found; supply a long package name."));
}

FWoolooWidgetResult SetLayout(const FString& AssetPath, const FString& AllowedPackageRoot,
    const FWoolooWidgetLayoutChange& Change)
{
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
        return Result(false, TEXT("Widget authoring requires the editor game thread with PIE stopped."));
    if (!InScope(AssetPath, AllowedPackageRoot))
        return Result(false, TEXT("Asset must be writable, outside engine content and inside a narrow AllowedPackageRoot."));
    auto* Asset = Load(AssetPath);
    if (!Asset) return Result(false, TEXT("WidgetBlueprint not found."));
    if (!Asset->GetOutermost()->GetName().Equals(AssetPath, ESearchCase::IgnoreCase))
        return Result(false, TEXT("Redirected asset paths are not accepted for writes; use the actual package path."));
    if (!Asset->WidgetTree || Asset->WidgetTree->GetOuter() != Asset || Asset->bBeingCompiled || Asset->bQueuedForCompilation)
        return Result(false, TEXT("A stable Blueprint with its own WidgetTree is required."), Asset);
    auto* Tree = Asset->WidgetTree.Get();
    auto* OldRoot = Tree->RootWidget.Get();
    TArray<FString> Issues;
    const auto Chain = Sources(*Asset, Issues);
    auto ParentRoots = 0;
    for (auto Index = 1; Index < Chain.Num(); ++Index)
        if (Chain[Index].Root()) ++ParentRoots;

    UClass* RootClass = nullptr;
    if (Change.Mode == EWoolooWidgetLayoutMode::Inherit)
    {
        if (!Change.RootWidgetClassPath.IsEmpty() || !Change.RootWidgetName.IsNone())
            Issues.Add(TEXT("Inherit requires an empty RootWidgetClassPath and RootWidgetName=None."));
        if (ParentRoots != 1) Issues.Add(TEXT("Inherit requires exactly one ancestor layout; absent or competing ancestor roots cannot be selected."));
    }
    else if (Change.Mode == EWoolooWidgetLayoutMode::Local)
    {
        if (ParentRoots) Issues.Add(TEXT("Local layout would conflict with an ancestor root. Use Inherit instead."));
        FText NameError;
        if (Change.RootWidgetName.IsNone() || !Change.RootWidgetName.IsValidObjectName(NameError))
            Issues.Add(TEXT("Supply a valid, non-None RootWidgetName. ") + NameError.ToString());
        if (Change.RootWidgetClassPath.StartsWith(TEXT("/")))
            RootClass = LoadClass<UWidget>(nullptr, *Change.RootWidgetClassPath);
        if (!RootClass || RootClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            Issues.Add(TEXT("RootWidgetClassPath must resolve to a concrete, current UWidget class."));
        TSet<const UClass*> Visiting;
        if (RootClass && WouldCreateCycle(*Asset, RootClass, Visiting))
            Issues.Add(TEXT("The requested root would create a circular UserWidget dependency."));
        for (const auto& Variable : Asset->NewVariables)
            if (Variable.VarName == Change.RootWidgetName) Issues.Add(TEXT("RootWidgetName conflicts with a Blueprint member variable."));
    }
    else Issues.Add(TEXT("Unknown layout mode."));

    const auto bNoChange = Change.Mode == EWoolooWidgetLayoutMode::Inherit ? OldRoot == nullptr
        : (OldRoot && RootClass == OldRoot->GetClass() && Change.RootWidgetName == OldRoot->GetFName());
    const auto Removed = bNoChange ? TArray<UWidget*>() : Subtree(OldRoot);
    if (!Removed.IsEmpty() && !Change.bDiscardLocalRoot)
        Issues.Add(TEXT("Removing the existing local root subtree requires bDiscardLocalRoot=true."));
    for (const auto* Widget : Removed)
        if (Widget->GetOuter() != Tree)
            Issues.Add(TEXT("A root descendant is not owned by this WidgetTree: ") + Widget->GetPathName());
    if (OldRoot && OldRoot->GetParent()) Issues.Add(TEXT("The current root unexpectedly has a panel parent."));
    CheckRemoval(*Asset, Removed, Issues);
    CheckNamedSlots(*Asset, Chain, Removed, Issues);
    if (!bNoChange && Change.Mode == EWoolooWidgetLayoutMode::Local && !Change.RootWidgetName.IsNone())
    {
        const auto bReusesRemovedName = Removed.ContainsByPredicate(
            [&](const auto* Widget) { return Widget->GetFName() == Change.RootWidgetName; });
        const auto* Existing = StaticFindObjectFast(UObject::StaticClass(), Tree, Change.RootWidgetName);
        if (Existing && !Removed.Contains(Cast<UWidget>(Existing)))
            Issues.Add(TEXT("RootWidgetName is already owned by another object in the local WidgetTree."));
        if (Asset->WidgetVariableNameToGuidMap.Contains(Change.RootWidgetName) && !bReusesRemovedName)
            Issues.Add(TEXT("RootWidgetName already has variable metadata outside the removed subtree."));
        const auto* Property = Asset->ParentClass
            ? CastField<FObjectPropertyBase>(Asset->ParentClass->FindPropertyByName(Change.RootWidgetName)) : nullptr;
        if (Property && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property))
        {
            if (!RootClass || !RootClass->IsChildOf(Property->PropertyClass))
                Issues.Add(TEXT("Requested root class is incompatible with the native BindWidget property."));
        }
        else
        {
            FKismetNameValidator Validator(Asset);
            const auto Validation = Validator.IsValid(Change.RootWidgetName);
            const auto bExistingRemovedName = bReusesRemovedName
                && (Validation == EValidatorResult::AlreadyInUse || Validation == EValidatorResult::ExistingName);
            if (Validation != EValidatorResult::Ok && !bExistingRemovedName)
                Issues.Add(TEXT("RootWidgetName: ") + INameValidatorInterface::GetErrorString(Change.RootWidgetName.ToString(), Validation));
        }
    }
    if (!Issues.IsEmpty()) return Result(false, TEXT("Layout change rejected before mutation."), Asset, Issues);
    if (bNoChange) return Result(true, TEXT("The requested layout is already selected."), Asset);

    FScopedTransaction Transaction(NSLOCTEXT("WoolooWidget", "SetLayout", "Set Widget Layout"));
    Asset->SetFlags(RF_Transactional);
    Asset->Modify();
    Tree->SetFlags(RF_Transactional);
    Tree->Modify();
    if (OldRoot)
    {
        // Use native tree removal/rename and variable bookkeeping, but not DeleteWidgets:
        // that helper can remove graph nodes and bindings rather than reject their dependencies.
        Tree->RemoveWidget(OldRoot);
        for (auto* Widget : Removed)
        {
            const auto Name = Widget->GetFName();
            Widget->SetFlags(RF_Transactional);
            Widget->Modify();
            Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
            Asset->OnVariableRemoved(Name);
        }
    }
    if (Change.Mode == EWoolooWidgetLayoutMode::Local)
    {
        // Record a distinct pre-rename identity so Undo frees the requested name before
        // restoring an old root with that same name. Unreferenced templates are not tree content.
        const auto TemporaryName = MakeUniqueObjectName(Tree, RootClass,
            FName(*(Change.RootWidgetName.ToString() + TEXT("_PendingLayout"))));
        auto* NewRoot = Tree->ConstructWidget<UWidget>(RootClass, TemporaryName);
        NewRoot->CreatedFromPalette();
        NewRoot->Modify();
        NewRoot->Rename(*Change.RootWidgetName.ToString(), Tree, REN_DontCreateRedirectors);
        Tree->RootWidget = NewRoot;
        Asset->OnVariableAdded(NewRoot->GetFName());
    }
    // Native structural notification marks the package dirty and refreshes only the skeleton.
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Asset);
    return Result(true, TEXT("Layout changed with Undo support. Skeleton refreshed; full compilation and saving remain explicit."), Asset, {}, true);
}
}
