#include "WoolooMutable/Graph/MutableGraphService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/ICustomizableObjectEditorModule.h"
#include "MuCOE/CustomizableObjectFactory.h"
#include "ScopedTransaction.h"
#include "UObject/FieldIterator.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
    FObjectPropertyBase* FindSourceProperty()
    {
        // Mutable owns Source; replace it natively without exposing a writable graph pointer to callers.
        return FindFProperty<FObjectPropertyBase>(UCustomizableObject::StaticClass(), TEXT("Source"));
    }

    UEdGraph* GetSourceGraph(UCustomizableObject* Object)
    {
        const auto Property = FindSourceProperty();
        return Object && Property ? Cast<UEdGraph>(Property->GetObjectPropertyValue_InContainer(Object)) : nullptr;
    }

    UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction)
    {
        return Node ? Node->FindPin(FName(*Name), Direction) : nullptr;
    }

    TArray<FString> GetSupportedNodeClasses()
    {
        // Only these node types have a verified standard placement/allocation lifecycle.
        return {
            TEXT("/Script/CustomizableObjectEditor.CustomizableObjectNodeObject"),
            TEXT("/Script/CustomizableObjectEditor.CustomizableObjectNodeObjectGroup"),
            TEXT("/Script/CustomizableObjectEditor.CustomizableObjectNodeSkeletalMesh"),
            TEXT("/Script/CustomizableObjectEditor.CONodeSkeletalMeshMake_V2"),
            TEXT("/Script/CustomizableObjectEditor.CONodeSkeletalMeshObjectMake"),
            TEXT("/Script/CustomizableObjectEditor.CONodeComponentSkeletalMesh"),
            TEXT("/Script/CustomizableObjectEditor.CONodeSkeletalMeshSection"),
            TEXT("/Script/CustomizableObjectEditor.CONodeModifierMutableSkeletalMeshMerge")
        };
    }

    bool ValidateOwnerMetadataKey(const FString& OwnerMetadataKey, FString& Report)
    {
        if (OwnerMetadataKey.TrimStartAndEnd().IsEmpty() || OwnerMetadataKey.Len() >= NAME_SIZE ||
            FName(*OwnerMetadataKey).IsNone())
        {
            Report = TEXT("OwnerMetadataKey must be a non-empty, non-None metadata key within the FName length limit.");
            return false;
        }
        return true;
    }

    bool ValidatePackageScope(const FString& PackageName, const FString& AllowedPackageRoot,
                              const FString& OwnerMetadataKey, const FString& ExpectedOwner, FString& Report)
    {
        if (!ValidateOwnerMetadataKey(OwnerMetadataKey, Report))
        {
            return false;
        }
        auto PackageRoot = AllowedPackageRoot;
        PackageRoot.RemoveFromEnd(TEXT("/"));
        // Validate a child name so callers can also scope a whole content mount, such as /Game.
        if (PackageRoot.IsEmpty() ||
            !FPackageName::IsValidLongPackageName(PackageRoot + TEXT("/__MutableGraphScope__")))
        {
            Report = FString::Printf(TEXT("Invalid or read-only allowed package root: %s"), *AllowedPackageRoot);
            return false;
        }

        const auto PackagePrefix = PackageRoot + TEXT("/");
        if (!FPackageName::IsValidLongPackageName(PackageName) ||
            PackageName.StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase) ||
            !PackageName.StartsWith(PackagePrefix, ESearchCase::IgnoreCase))
        {
            Report = FString::Printf(TEXT("Asset %s must be inside the allowed content directory %s"),
                                     *PackageName, *PackagePrefix);
            return false;
        }

        if (ExpectedOwner.TrimStartAndEnd().IsEmpty())
        {
            Report = TEXT("ExpectedOwner must be a non-empty builder identifier.");
            return false;
        }

        return true;
    }

    bool ValidateOwnedAsset(UCustomizableObject* Object, const FString& AllowedPackageRoot,
                            const FString& OwnerMetadataKey, const FString& ExpectedOwner, FString& Report)
    {
        if (!IsValid(Object) || !Object->IsAsset() || Object->HasAnyFlags(RF_Transient))
        {
            Report = TEXT("A package-backed Customizable Object asset is required.");
            return false;
        }

        const auto Package = Object->GetPackage();
        const auto PackageName = Package->GetName();
        if (!ValidatePackageScope(PackageName, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Report))
        {
            return false;
        }

        if (!Package->GetMetaData().GetValue(Object, *OwnerMetadataKey).Equals(ExpectedOwner, ESearchCase::CaseSensitive))
        {
            Report = FString::Printf(TEXT("Refusing to replace %s without the expected builder ownership marker: %s"),
                                     *PackageName, *ExpectedOwner);
            return false;
        }

        return true;
    }

    bool ValidateParentHierarchy(UCustomizableObject* Object, UEdGraph* Graph, FString& Report)
    {
        TSet<UCustomizableObject*> Visited;
        Visited.Add(Object);
        while (Graph)
        {
            UEdGraphNode* Root = nullptr;
            for (const auto Node : Graph->Nodes)
            {
                if (Node && Node->GetClass()->GetFName() == TEXT("CustomizableObjectNodeObject"))
                {
                    if (Root)
                    {
                        Report = TEXT("A Mutable hierarchy asset must have exactly one base object node.");
                        return false;
                    }
                    Root = Node;
                }
            }
            const auto Property = Root ? FindFProperty<FObjectPropertyBase>(Root->GetClass(), TEXT("ParentObject")) : nullptr;
            if (!Property)
            {
                break;
            }
            const auto Parent = Cast<UCustomizableObject>(Property->GetObjectPropertyValue_InContainer(Root));
            if (!Parent)
            {
                return true;
            }
            if (!Parent->IsAsset() || Visited.Contains(Parent))
            {
                Report = TEXT("Parent COs must be package-backed assets and must not form a cycle.");
                return false;
            }
            Visited.Add(Parent);
            Graph = GetSourceGraph(Parent);
        }
        Report = TEXT("A CO or its parent is missing a source graph/base object node.");
        return false;
    }

    struct FNativeCompileCallback
    {
        FCompileCallbackParams Flags;
        bool bReceived = false;
    };

    // Native out-of-date checks use saved package hashes and can miss an unsaved graph replacement.
    // Keep this session's authoring edits invalid until an explicit successful compile of their root.
    TSet<TWeakObjectPtr<UCustomizableObject>> RootsNeedingCompile;

    UCustomizableObject* LoadObjectAsset(const FString& AssetPath, FMutableToolResult& Result)
    {
        Result.AssetPath = AssetPath;
        if (!IsInGameThread() || !FPackageName::IsValidLongPackageName(AssetPath, true))
        {
            Result.Message = TEXT("A valid long package path without an object suffix and the game thread are required.");
            return nullptr;
        }

        const auto ObjectPath = AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
        const auto Object = LoadObject<UCustomizableObject>(nullptr, *ObjectPath);
        if (!IsValid(Object) || !Object->IsAsset() || Object->HasAnyFlags(RF_Transient))
        {
            Result.Message = TEXT("Customizable Object asset not found.");
            return nullptr;
        }
        Result.bDirty = Object->GetPackage()->IsDirty();
        return Object;
    }

    bool CheckReady(UCustomizableObject* Object, FMutableToolResult& Result)
    {
        if (!ValidateParentHierarchy(
                Object, GetSourceGraph(Object), Result.Message))
        {
            return false;
        }
        const auto EditorModule = ICustomizableObjectEditorModule::Get();
        if (!EditorModule)
        {
            Result.Message = TEXT("Mutable editor module is unavailable.");
            return false;
        }
        const auto Root = EditorModule->GetRootObject(Object);
        if (Object->IsLoading() || EditorModule->IsCompiling(*Object) ||
            (Root && (Root->IsLoading() || EditorModule->IsCompiling(*Root))))
        {
            Result.Message = TEXT("Mutable is loading or compiling this hierarchy; retry after it finishes.");
            return false;
        }
        return true;
    }

    UCustomizableObject* LoadOwnedAsset(const FString& AssetPath, const FString& AllowedPackageRoot,
                                       const FString& OwnerMetadataKey, const FString& ExpectedOwner,
                                       FMutableToolResult& Result)
    {
        Result.AssetPath = AssetPath;
        if (!ValidatePackageScope(
                AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Result.Message))
        {
            return nullptr;
        }
        const auto Object = LoadObjectAsset(AssetPath, Result);
        if (!Object || !ValidateOwnedAsset(
                Object, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Result.Message) || !CheckReady(Object, Result))
        {
            return nullptr;
        }
        return Object;
    }

    bool IsAuthoringProperty(const FProperty* Property)
    {
        const auto OwnerClass = Property->GetOwnerClass();
        if (!OwnerClass || !OwnerClass->GetPathName().StartsWith(TEXT("/Script/CustomizableObjectEditor.")) ||
            Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst |
                                         CPF_InstancedReference | CPF_ContainsInstancedReference))
        {
            return false;
        }
        // Mutable keeps several authoring fields non-editable because its graph UI edits them instead.
        const auto Name = Property->GetFName();
        return Property->HasAnyPropertyFlags(CPF_Edit) || Name == TEXT("ObjectName") ||
               Name == TEXT("GroupName") || Name == TEXT("Identifier") ||
               Name == TEXT("ComponentName") || Name == TEXT("SkeletalMeshName") ||
               Name == TEXT("ParentSkeletalMeshName") || Name == TEXT("Tags");
    }

    FString ExportProperty(const FProperty* Property, UObject* Object)
    {
        FString Value;
        Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Object),
                                       nullptr, Object, PPF_None);
        return Value;
    }

    FMutableToolResult Describe(UCustomizableObject* Object, const FString& OwnerMetadataKey,
                                FMutableToolGraph& Description,
                                TArray<FMutableToolPin>& Pins, FString& Owner, bool& bCanApply)
    {
        FMutableToolResult Result;
        Result.AssetPath = Object->GetPackage()->GetName();
        Result.bDirty = Object->GetPackage()->IsDirty();
        Owner = Object->GetPackage()->GetMetaData().GetValue(Object, *OwnerMetadataKey);
        const auto Graph = GetSourceGraph(Object);
        if (!Graph)
        {
            Result.Message = TEXT("Missing Mutable source graph.");
            return Result;
        }

        const auto Supported = GetSupportedNodeClasses();
        bCanApply = true;
        for (const auto Node : Graph->Nodes)
        {
            if (!IsValid(Node))
            {
                bCanApply = false;
                continue;
            }
            auto& Row = Description.Nodes.AddDefaulted_GetRef();
            Row.Id = Node->GetName();
            Row.ClassPath = Node->GetClass()->GetPathName();
            Row.Guid = Node->NodeGuid.ToString(EGuidFormats::Digits);
            Row.X = Node->NodePosX;
            Row.Y = Node->NodePosY;
            bCanApply &= Supported.Contains(Row.ClassPath);
            for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
            {
                if (IsAuthoringProperty(*It))
                {
                    auto& Property = Row.Properties.AddDefaulted_GetRef();
                    Property.Name = It->GetName();
                    Property.Value = ExportProperty(*It, Node);
                }
            }
            Row.Properties.Sort([](const auto& A, const auto& B) { return A.Name < B.Name; });
            for (const auto Pin : Node->Pins)
            {
                if (!Pin)
                {
                    bCanApply = false;
                    continue;
                }
                auto& PinRow = Pins.AddDefaulted_GetRef();
                PinRow.NodeId = Row.Id;
                PinRow.Name = Pin->PinName.ToString();
                PinRow.Category = Pin->PinType.PinCategory.ToString();
                PinRow.Direction = Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input");
                // The replacement format describes property-driven nodes, not arbitrary edited pin defaults.
                if (Pin->bOrphanedPin || Pin->ParentPin || !Pin->SubPins.IsEmpty() || Pin->DefaultObject ||
                    !Pin->DefaultTextValue.IsEmpty() || Pin->DefaultValue != Pin->AutogeneratedDefaultValue)
                {
                    bCanApply = false;
                }
                if (Pin->Direction == EGPD_Output)
                {
                    for (const auto Other : Pin->LinkedTo)
                    {
                        auto& Link = Description.Links.AddDefaulted_GetRef();
                        Link.FromNode = Row.Id;
                        Link.FromPin = PinRow.Name;
                        Link.ToNode = Other->GetOwningNode()->GetName();
                        Link.ToPin = Other->PinName.ToString();
                    }
                }
            }
        }
        Result.bSuccess = true;
        Result.Message = bCanApply
            ? TEXT("Graph inspected. ApplyGraph replaces the complete graph and does not save it.")
            : TEXT("Graph inspected, but unsupported nodes or custom pin state prevent a lossless ApplyGraph round trip.");
        return Result;
    }

    bool ReplaceGraph(UCustomizableObject* Object, const FMutableToolGraph& Spec, FString& Report)
    {
        if (Spec.Nodes.IsEmpty())
        {
            Report = TEXT("Graph needs at least one node.");
            return false;
        }
        const auto SourceProperty = FindSourceProperty();
        const auto GraphClass = LoadClass<UEdGraph>(nullptr,
            TEXT("/Script/CustomizableObjectEditor.CustomizableObjectGraph"));
        if (!SourceProperty || !GraphClass)
        {
            Report = TEXT("Mutable graph types are unavailable in this editor.");
            return false;
        }

        // Stage the entire replacement. Neither failed imports nor failed connections replace Source.
        const auto Graph = NewObject<UEdGraph>(Object, GraphClass, NAME_None, RF_Transactional);
        const auto SupportedClasses = GetSupportedNodeClasses();
        TMap<FString, UEdGraphNode*> Nodes;
        TSet<FName> Names;
        TSet<FGuid> Guids;
        int32 RootCount = 0;
        for (const auto& SpecNode : Spec.Nodes)
        {
            if (SpecNode.Id.IsEmpty() || SpecNode.Id.Len() >= NAME_SIZE || FName(*SpecNode.Id).IsNone() ||
                !FName::IsValidXName(SpecNode.Id, INVALID_OBJECTNAME_CHARACTERS) ||
                Names.Contains(FName(*SpecNode.Id)) || !SupportedClasses.Contains(SpecNode.ClassPath))
            {
                Report = TEXT("Each node needs a unique valid object name and a class from ListNodeTypes.");
                return false;
            }
            Names.Add(FName(*SpecNode.Id));
            const auto NodeClass = LoadClass<UEdGraphNode>(nullptr, *SpecNode.ClassPath);
            if (!NodeClass || NodeClass->HasAnyClassFlags(CLASS_Abstract))
            {
                Report = FString::Printf(TEXT("Unsupported node class: %s"), *SpecNode.ClassPath);
                return false;
            }
            const auto Node = NewObject<UEdGraphNode>(Graph, NodeClass, FName(*SpecNode.Id), RF_Transactional);
            Graph->AddNode(Node, false, false);
            Node->CreateNewGuid();
            if (!SpecNode.Guid.IsEmpty() &&
                (!FGuid::Parse(SpecNode.Guid, Node->NodeGuid) || !Node->NodeGuid.IsValid()))
            {
                Report = FString::Printf(TEXT("Invalid GUID on node %s"), *SpecNode.Id);
                return false;
            }
            if (Guids.Contains(Node->NodeGuid))
            {
                Report = TEXT("Node GUIDs must be unique.");
                return false;
            }
            Guids.Add(Node->NodeGuid);
            TSet<FName> PropertyNames;
            for (const auto& Entry : SpecNode.Properties)
            {
                const auto Property = FindFProperty<FProperty>(NodeClass, FName(*Entry.Name));
                if (!Property || !IsAuthoringProperty(Property) || PropertyNames.Contains(FName(*Entry.Name)) ||
                    !Property->ImportText_Direct(*Entry.Value, Property->ContainerPtrToValuePtr<void>(Node), Node, PPF_None))
                {
                    Report = FString::Printf(TEXT("Cannot import authoring property %s.%s"), *SpecNode.Id, *Entry.Name);
                    return false;
                }
                PropertyNames.Add(FName(*Entry.Name));
            }
            Node->NodePosX = SpecNode.X;
            Node->NodePosY = SpecNode.Y;
            // These supported nodes use Mutable's normal placement/allocation lifecycle.
            Node->PostPlacedNewNode();
            Node->AllocateDefaultPins();
            Nodes.Add(SpecNode.Id, Node);
            RootCount += SpecNode.ClassPath == TEXT("/Script/CustomizableObjectEditor.CustomizableObjectNodeObject") ? 1 : 0;
        }
        if (RootCount != 1)
        {
            Report = TEXT("A Mutable asset must contain exactly one base object node.");
            return false;
        }
        const auto Schema = Graph->GetSchema();
        if (!Schema)
        {
            Report = TEXT("Missing Mutable graph schema.");
            return false;
        }
        for (const auto& Link : Spec.Links)
        {
            const auto Output = FindPin(Nodes.FindRef(Link.FromNode), Link.FromPin, EGPD_Output);
            const auto Input = FindPin(Nodes.FindRef(Link.ToNode), Link.ToPin, EGPD_Input);
            if (!Output || !Input || !Schema->TryCreateConnection(Output, Input))
            {
                Report = FString::Printf(TEXT("Cannot connect %s.%s to %s.%s"),
                    *Link.FromNode, *Link.FromPin, *Link.ToNode, *Link.ToPin);
                return false;
            }
        }
        // Later connections can reconstruct pins or replace a prior single-input connection.
        for (const auto& Link : Spec.Links)
        {
            const auto Output = FindPin(Nodes.FindRef(Link.FromNode), Link.FromPin, EGPD_Output);
            const auto Input = FindPin(Nodes.FindRef(Link.ToNode), Link.ToPin, EGPD_Input);
            if (!Output || !Input || !Output->LinkedTo.Contains(Input))
            {
                Report = TEXT("A later connection invalidated an earlier link; Source was not replaced.");
                return false;
            }
        }
        if (!ValidateParentHierarchy(Object, Graph, Report))
        {
            return false;
        }

        Object->Modify();
        SourceProperty->SetObjectPropertyValue_InContainer(Object, Graph);
        for (const auto& Entry : Nodes)
        {
            if (Entry.Value->GetClass()->GetFName() == TEXT("CustomizableObjectNodeObject"))
            {
                // Refresh Mutable's cached child flag without opening the graph editor.
                auto Changed = FPropertyChangedEvent(
                    FindFProperty<FProperty>(Entry.Value->GetClass(), TEXT("ParentObject")));
                Entry.Value->PostEditChangeProperty(Changed);
            }
        }
        Graph->NotifyGraphChanged();
        Object->PostEditChange();
        (void)Object->MarkPackageDirty();
        return true;
    }
}

TArray<FMutableToolNodeType> FMutableGraphService::ListNodeTypes()
{
    TArray<FMutableToolNodeType> Types;
    for (const auto& ClassPath : GetSupportedNodeClasses())
    {
        const auto NodeClass = LoadClass<UEdGraphNode>(nullptr, *ClassPath);
        if (!NodeClass)
        {
            continue;
        }
        auto& Row = Types.AddDefaulted_GetRef();
        Row.ClassPath = ClassPath;
        for (TFieldIterator<FProperty> It(NodeClass); It; ++It)
        {
            if (IsAuthoringProperty(*It))
            {
                auto& Property = Row.Properties.AddDefaulted_GetRef();
                Property.Name = It->GetName();
                FString ExtendedType;
                Property.Type = It->GetCPPType(&ExtendedType);
                Property.Type += ExtendedType;
                Property.DefaultValue = ExportProperty(*It, NodeClass->GetDefaultObject());
            }
        }
        Row.Properties.Sort([](const auto& A, const auto& B) { return A.Name < B.Name; });
    }
    return Types;
}

FMutableToolResult FMutableGraphService::DescribeObject(const FString& AssetPath, const FString& OwnerMetadataKey,
                                                       FMutableToolGraph& Graph, TArray<FMutableToolPin>& Pins,
                                                       FString& Owner, bool& bCanApply,
                                                       bool bRequireReady)
{
    Graph = {};
    Pins.Reset();
    Owner.Reset();
    bCanApply = false;
    FMutableToolResult Result;
    Result.AssetPath = AssetPath;
    if (!ValidateOwnerMetadataKey(OwnerMetadataKey, Result.Message))
    {
        return Result;
    }
    const auto Object = LoadObjectAsset(AssetPath, Result);
    if (!Object || (bRequireReady && !CheckReady(Object, Result)))
    {
        return Result;
    }
    return Describe(Object, OwnerMetadataKey, Graph, Pins, Owner, bCanApply);
}

FMutableToolResult FMutableGraphService::CreateObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                                     const FString& OwnerMetadataKey, const FString& ExpectedOwner)
{
    FMutableToolResult Result;
    Result.AssetPath = AssetPath;
    if (!IsInGameThread() || !ValidatePackageScope(
            AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Result.Message))
    {
        if (Result.Message.IsEmpty())
        {
            Result.Message = TEXT("Mutable tools must run on the game thread.");
        }
        return Result;
    }
    const auto AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    FText NameError;
    if (AssetName.Len() >= NAME_SIZE || FName(*AssetName).IsNone() ||
        !FName(*AssetName).IsValidObjectName(NameError))
    {
        Result.Message = TEXT("Target package must have a valid non-None asset name.");
        return Result;
    }
    // Never let a factory overwrite another asset or reuse an unrelated in-memory package.
    if (FindPackage(nullptr, *AssetPath) || FPackageName::DoesPackageExist(AssetPath))
    {
        if (LoadOwnedAsset(AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Result))
        {
            Result.bSuccess = true;
            Result.Message = TEXT("Existing owned asset reused without modification.");
        }
        return Result;
    }
    const auto Factory = NewObject<UCustomizableObjectFactory>();
    const auto Object = Cast<UCustomizableObject>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(
        TEXT("AssetTools")).Get().CreateAsset(FPackageName::GetLongPackageAssetName(AssetPath),
        FPackageName::GetLongPackagePath(AssetPath), UCustomizableObject::StaticClass(), Factory));
    if (!Object)
    {
        Result.Message = TEXT("Mutable asset factory failed; no existing asset was overwritten.");
        return Result;
    }
    Object->GetPackage()->GetMetaData().SetValue(Object, *OwnerMetadataKey, *ExpectedOwner);
    Object->MarkPackageDirty();
    Result.bSuccess = true;
    Result.bCreated = true;
    Result.bDirty = Object->GetPackage()->IsDirty();
    Result.Message = TEXT("Owned CO created in memory. Apply a graph and call SaveObject explicitly.");
    return Result;
}

FMutableToolResult FMutableGraphService::ApplyGraph(const FString& AssetPath, const FString& AllowedPackageRoot,
                                                   const FString& OwnerMetadataKey, const FString& ExpectedOwner,
                                                   const FMutableToolGraph& Graph)
{
    FMutableToolResult Result;
    const auto Object = LoadOwnedAsset(AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Result);
    if (!Object)
    {
        return Result;
    }
    const auto PreviousRoot = ICustomizableObjectEditorModule::Get()->GetRootObject(Object);
    FScopedTransaction Transaction(NSLOCTEXT("WoolooMutable", "ApplyMutableGraph", "Apply Mutable graph"));
    if (!ReplaceGraph(Object, Graph, Result.Message))
    {
        Transaction.Cancel();
        return Result;
    }
    const auto Root = ICustomizableObjectEditorModule::Get()->GetRootObject(Object);
    RootsNeedingCompile.Add(Root ? Root : Object);
    if (PreviousRoot)
    {
        RootsNeedingCompile.Add(PreviousRoot);
    }
    Result.bSuccess = true;
    Result.bDirty = Object->GetPackage()->IsDirty();
    Result.Message = TEXT("Complete graph replaced in memory. Compile and save are separate operations.");
    return Result;
}

FMutableToolResult FMutableGraphService::CompileObject(const FString& AssetPath,
                                                       const FString& AllowedPackageRoot,
                                                       const FString& OwnerMetadataKey, const FString& ExpectedOwner,
                                                       FCompileCallbackParams& Flags, bool& bCallbackReceived)
{
    FMutableToolResult Result;
    Flags = {};
    bCallbackReceived = false;
    const auto Object = LoadOwnedAsset(AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Result);
    if (!Object)
    {
        return Result;
    }
    const auto EditorModule = ICustomizableObjectEditorModule::Get();
    if (!EditorModule->IsRootObject(*Object))
    {
        Result.Message = TEXT("Compile the hierarchy's owned root CO explicitly, not a child CO.");
        return Result;
    }
    // Discover newly saved external children before Mutable resolves the root's options.
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous(
        {AllowedPackageRoot}, true);

    // A shared result remains valid even if an engine-version change defers the native callback.
    const auto CallbackResult = MakeShared<FNativeCompileCallback>();
    FCompileParams Params;
    Params.bAsync = false;
    Params.bSkipIfCompiled = false;
    Params.bSkipIfNotOutOfDate = false;
    Params.bGatherReferences = true;
    Params.CallbackNative.BindLambda([CallbackResult](const FCompileCallbackParams& Value)
    {
        CallbackResult->bReceived = true;
        CallbackResult->Flags = Value;
    });
    const TStrongObjectPtr<UCustomizableObject> KeepAlive(Object);
    RootsNeedingCompile.Add(Object);
    Object->Compile(Params);
    bCallbackReceived = CallbackResult->bReceived;
    Flags = CallbackResult->Flags;
    Result.bDirty = Object->GetPackage()->IsDirty();
    Result.bSuccess = bCallbackReceived && !Flags.bRequestFailed &&
        !Flags.bErrors && !Flags.bSkipped && Flags.bCompiled;
    if (Result.bSuccess)
    {
        RootsNeedingCompile.Remove(Object);
    }
    Result.Message = Result.bSuccess
        ? (Flags.bWarnings ? TEXT("Compiled with warnings; inspect LogMutable. Package not saved.")
                           : TEXT("Mutable resource compilation succeeded. Package not saved."))
        : TEXT("This compilation request did not succeed. Inspect callback flags and LogMutable; an old compiled model is not a successful request.");
    return Result;
}

FMutableToolResult FMutableGraphService::GetParameters(const FString& AssetPath,
                                                       TArray<FMutableToolParameter>& Parameters,
                                                       TArray<FString>& Components, bool& bOutOfDate)
{
    FMutableToolResult Result;
    Parameters.Reset();
    Components.Reset();
    bOutOfDate = false;
    const auto Object = LoadObjectAsset(AssetPath, Result);
    if (!Object || !CheckReady(Object, Result))
    {
        return Result;
    }
    const auto EditorModule = ICustomizableObjectEditorModule::Get();
    if (!EditorModule->IsRootObject(*Object) || !Object->IsCompiled())
    {
        Result.Message = TEXT("An explicitly compiled root CO is required.");
        return Result;
    }
    if (RootsNeedingCompile.Contains(Object))
    {
        bOutOfDate = true;
        Result.Message = TEXT("This hierarchy has authoring edits or a failed compile. Run CompileObject successfully before reading parameters.");
        return Result;
    }
    TArray<FName> OutOfDatePackages;
    TArray<FName> AddedPackages;
    TArray<FName> RemovedPackages;
    bool bReleaseVersionDiff = false;
    bOutOfDate = EditorModule->IsCompilationOutOfDate(
        *Object, false, OutOfDatePackages, AddedPackages, RemovedPackages, bReleaseVersionDiff);
    if (bOutOfDate)
    {
        Result.Message = TEXT("Compiled model is out of date. CompileObject before inspecting parameters.");
        return Result;
    }

    const TStrongObjectPtr<UCustomizableObjectInstance> Defaults(Object->CreateInstance());
    for (int32 Index = 0; Index < Object->GetParameterCount(); ++Index)
    {
        auto& Parameter = Parameters.AddDefaulted_GetRef();
        Parameter.Name = Object->GetParameterName(Index);
        const auto Type = Object->GetParameterTypeByName(Parameter.Name);
        Parameter.Type = StaticEnum<EMutableParameterType>()->GetNameStringByValue(static_cast<int64>(Type));
        Parameter.bMultidimensional = Object->IsParameterMultidimensional(Parameter.Name);
        switch (Type)
        {
        case EMutableParameterType::Int:
            Parameter.bHasDefaultValue = true;
            Parameter.DefaultValue = FString::FromInt(Object->GetEnumParameterDefaultValue(Parameter.Name));
            for (int32 Option = 0; Option < Object->GetEnumParameterNumValues(Parameter.Name); ++Option)
            {
                Parameter.Options.Add(Object->GetEnumParameterValue(Parameter.Name, Option));
            }
            if (!Parameter.bMultidimensional && !Parameter.Options.IsEmpty())
            {
                Parameter.DefaultOption = Defaults->GetEnumParameterSelectedOption(Parameter.Name);
            }
            break;
        case EMutableParameterType::Bool:
            Parameter.bHasDefaultValue = true;
            Parameter.DefaultValue = Object->GetBoolParameterDefaultValue(Parameter.Name) ? TEXT("true") : TEXT("false");
            break;
        case EMutableParameterType::Float:
            Parameter.bHasDefaultValue = true;
            Parameter.DefaultValue = FString::SanitizeFloat(Object->GetFloatParameterDefaultValue(Parameter.Name));
            break;
        default:
            break;
        }
    }
    for (int32 Index = 0; Index < Object->GetComponentCount(); ++Index)
    {
        Components.Add(Object->GetComponentName(Index).ToString());
    }
    Result.bSuccess = true;
    Result.Message = TEXT("Compiled parameters and output components inspected; no mesh update or asset save requested.");
    return Result;
}

FMutableToolResult FMutableGraphService::SaveObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                                   const FString& OwnerMetadataKey, const FString& ExpectedOwner)
{
    FMutableToolResult Result;
    const auto Object = LoadOwnedAsset(AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Result);
    if (!Object)
    {
        return Result;
    }
    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(AssetPath, Filename, FPackageName::GetAssetPackageExtension()))
    {
        Result.Message = TEXT("Cannot resolve the target package to a writable content file.");
        return Result;
    }
    auto& Files = IFileManager::Get();
    if ((Files.FileExists(*Filename) && Files.IsReadOnly(*Filename)) ||
        !Files.MakeDirectory(*FPaths::GetPath(Filename), true))
    {
        Result.Message = TEXT("Target file is read-only or its directory cannot be created. No checkout was attempted.");
        return Result;
    }
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GWarn;
    Args.bSlowTask = false;
    Result.bSaved = UPackage::SavePackage(Object->GetPackage(), Object, *Filename, Args);
    Result.bSuccess = Result.bSaved;
    Result.bDirty = Object->GetPackage()->IsDirty();
    Result.Message = Result.bSaved ? TEXT("Target CO package saved; dependency packages were not saved.")
                                  : TEXT("Target package save failed; inspect the editor log.");
    return Result;
}
