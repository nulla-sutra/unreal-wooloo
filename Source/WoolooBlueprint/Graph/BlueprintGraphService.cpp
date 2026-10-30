#include "WoolooBlueprint/Graph/BlueprintGraphService.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/AnimBlueprintFactory.h"
#include "InputAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_EnhancedInputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/StringOutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/Interface.h"
#include "UObject/UnrealType.h"

namespace Wooloo::Blueprint
{
namespace
{
FWoolooBlueprintResult Result(bool bSuccess, const FString& Message, FGuid Id = {})
{
    FWoolooBlueprintResult Value;
    Value.bSuccess = bSuccess; Value.Message = Message; Value.NodeId = Id;
    return Value;
}
bool InScope(const FString& Path, FString Root)
{
    Root.RemoveFromEnd(TEXT("/"));
    FString Filename;
    return Root != TEXT("/Game") && FPackageName::IsValidLongPackageName(Root)
        && FPackageName::IsValidLongPackageName(Path) && Path.StartsWith(Root + TEXT("/"))
        && FPackageName::TryConvertLongPackageNameToFilename(Path, Filename)
        && !FPaths::ConvertRelativePathToFull(Filename)
            .StartsWith(FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
}
UBlueprint* LoadBlueprint(const FString& Path)
{ return FPackageName::IsValidLongPackageName(Path) ? LoadObject<UBlueprint>(nullptr, *Path) : nullptr; }
TArray<UEdGraph*> Graphs(UBlueprint& Asset)
{
    TArray<UEdGraph*> Out;
    Asset.GetAllGraphs(Out);
    // Native interface graphs are easy to miss when only enumerating FunctionGraphs.
    for (const auto& Interface : Asset.ImplementedInterfaces)
        for (const auto& Graph : Interface.Graphs)
            if (Graph) Out.AddUnique(Graph.Get());
    return Out;
}

/** Native reflection needs initialized property storage to validate imports without touching a live UObject. */
struct FStagedProperty
{
    FProperty* Property;
    void* Memory;
    explicit FStagedProperty(FProperty* InProperty) : Property(InProperty), Memory(Property->AllocateAndInitializeValue()) {}
    ~FStagedProperty() { Property->DestroyValue(Memory); FMemory::Free(Memory); }
};

bool ImportProperties(UObject& Object, const TMap<FString, FString>& Values, bool bApply, FString& Error,
    bool bNotify = true)
{
    TArray<TUniquePtr<FStagedProperty>> Staged;
    for (const auto& Pair : Values)
    {
        auto* Property = Object.GetClass()->FindPropertyByName(*Pair.Key);
        // Native factory/action references are not editor-visible, but determine which pins are allocated.
        const auto bFactoryField = Object.IsA<UK2Node_BaseAsyncTask>()
            && (Pair.Key == TEXT("ProxyFactoryClass") || Pair.Key == TEXT("ProxyFactoryFunctionName")
                || Pair.Key == TEXT("ProxyClass"));
        const auto bInputActionField = Object.IsA<UK2Node_EnhancedInputAction>() && Pair.Key == TEXT("InputAction");
        if (!Property || (!Property->HasAnyPropertyFlags(CPF_Edit) && !bFactoryField && !bInputActionField)
            || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_InstancedReference | CPF_ContainsInstancedReference))
        { Error = TEXT("Unsupported reflected property: ") + Pair.Key; return false; }
        auto Value = MakeUnique<FStagedProperty>(Property);
        Property->CopyCompleteValue(Value->Memory, Property->ContainerPtrToValuePtr<void>(&Object));
        FStringOutputDevice Errors;
        const auto* Rest = Property->ImportText_Direct(*Pair.Value, Value->Memory, &Object, PPF_None, &Errors);
        if (!Rest || !FString(Rest).TrimStartAndEnd().IsEmpty() || !Errors.IsEmpty())
        { Error = TEXT("Import failed for ") + Pair.Key + TEXT(": ") + Errors; return false; }
        Staged.Add(MoveTemp(Value));
    }
    if (bApply)
    {
        Object.Modify();
        for (const auto& Value : Staged)
        {
            if (bNotify) Object.PreEditChange(Value->Property);
            Value->Property->CopyCompleteValue(Value->Property->ContainerPtrToValuePtr<void>(&Object), Value->Memory);
            FPropertyChangedEvent Event(Value->Property, EPropertyChangeType::ValueSet);
            if (bNotify) Object.PostEditChangeProperty(Event);
        }
    }
    return true;
}

bool ValidateInputActionNode(const UK2Node_EnhancedInputAction& Node, FString& Error)
{
    const auto Fail = [&](const FString& Reason)
    {
        Error = FString::Printf(TEXT("Enhanced Input node %s: %s Replace it with an AddNode configured with InputAction before allocating pins."),
            *Node.NodeGuid.ToString(), *Reason);
        return false;
    };
    if (!IsValid(Node.InputAction.Get())) return Fail(TEXT("InputAction is missing or invalid."));
    // Native ExpandNode dereferences all four data pins when any event output is connected.
    for (const auto* Name : { TEXT("ActionValue"), TEXT("ElapsedSeconds"), TEXT("TriggeredSeconds"), TEXT("InputAction") })
    {
        const auto* Pin = Node.FindPin(Name, EGPD_Output);
        if (!Pin || Pin->bOrphanedPin) return Fail(FString::Printf(TEXT("Required output pin %s is missing or orphaned."), Name));
    }
    const auto* ActionPin = Node.FindPin(TEXT("InputAction"), EGPD_Output);
    if (ActionPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object
        || ActionPin->PinType.PinSubCategoryObject.Get() != Node.InputAction->GetClass()
        || ActionPin->DefaultObject.Get() != Node.InputAction.Get())
        return Fail(TEXT("InputAction output does not match the configured asset."));
    return true;
}
}

FWoolooBlueprintResult CreateAnimationTemplate(const FString& Path, const FString& Root)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before authoring assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    if (FindPackage(nullptr, *Path) || FPackageName::DoesPackageExist(Path))
        return Result(false, TEXT("Package already exists; creation never replaces or adopts it."));

    // Register the native animation Blueprint compiler before the factory's initial Blueprint setup.
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AnimGraph"));
    auto* Factory = NewObject<UAnimBlueprintFactory>();
    Factory->BlueprintType = BPTYPE_Normal;
    Factory->ParentClass = UAnimInstance::StaticClass();
    Factory->TargetSkeleton = nullptr;
    Factory->bTemplate = true;
    auto* Package = CreatePackage(*Path);
    auto* Asset = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package,
        *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional,
        nullptr, GWarn));
    if (!Asset)
        return Result(false, TEXT("Native animation Blueprint factory failed; no asset was saved. The package may remain in memory."));

    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("asset_path"), Path);
    Json->SetBoolField(TEXT("created"), true);
    Json->SetBoolField(TEXT("dirty"), Package->IsDirty());
    Json->SetBoolField(TEXT("is_template"), Asset->bIsTemplate);
    Json->SetStringField(TEXT("parent_class"), GetPathNameSafe(Asset->ParentClass));
    Json->SetStringField(TEXT("target_skeleton"), GetPathNameSafe(Asset->TargetSkeleton));
    auto Value = Result(true, TEXT("Created in memory with the native factory; compile and save explicitly."));
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Value.Json));
    return Value;
}

FWoolooBlueprintResult Describe(const FString& Path)
{
    auto* Asset = LoadBlueprint(Path);
    if (!Asset) return Result(false, TEXT("Blueprint not found."));
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("parent_class"), GetPathNameSafe(Asset->ParentClass));
    Json->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(Asset->Status));
    TArray<TSharedPtr<FJsonValue>> GraphItems;
    for (const auto* Graph : Graphs(*Asset))
    {
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Graph->GetName());
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for (const auto& Node : Graph->Nodes)
        {
            if (!Node) continue;
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
            Item->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
            Item->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            Item->SetStringField(TEXT("comment"), Node->NodeComment);
            Item->SetNumberField(TEXT("x"), Node->NodePosX); Item->SetNumberField(TEXT("y"), Node->NodePosY);
            Item->SetNumberField(TEXT("width"), Node->NodeWidth); Item->SetNumberField(TEXT("height"), Node->NodeHeight);
            TArray<TSharedPtr<FJsonValue>> Pins;
            for (const auto* Pin : Node->Pins)
            {
                auto PinItem = MakeShared<FJsonObject>();
                PinItem->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinItem->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
                PinItem->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
                PinItem->SetStringField(TEXT("default"), Pin->DefaultValue);
                PinItem->SetStringField(TEXT("object"), GetPathNameSafe(Pin->DefaultObject));
                TArray<TSharedPtr<FJsonValue>> Links;
                for (const auto* Link : Pin->LinkedTo)
                    Links.Add(MakeShared<FJsonValueString>(Link->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + Link->PinName.ToString()));
                PinItem->SetArrayField(TEXT("links"), Links);
                Pins.Add(MakeShared<FJsonValueObject>(PinItem));
            }
            Item->SetArrayField(TEXT("pins"), Pins);
            Nodes.Add(MakeShared<FJsonValueObject>(Item));
        }
        Entry->SetArrayField(TEXT("nodes"), Nodes);
        GraphItems.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Json->SetArrayField(TEXT("graphs"), GraphItems);
    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const auto& Interface : Asset->ImplementedInterfaces)
        Interfaces.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Interface.Interface)));
    Json->SetArrayField(TEXT("interfaces"), Interfaces);
    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const auto& Variable : Asset->NewVariables)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Variable.VarName.ToString());
        Item->SetStringField(TEXT("id"), Variable.VarGuid.ToString());
        Item->SetStringField(TEXT("category"), Variable.VarType.PinCategory.ToString());
        Item->SetStringField(TEXT("type"), GetPathNameSafe(Variable.VarType.PinSubCategoryObject.Get()));
        Item->SetStringField(TEXT("default"), Variable.DefaultValue);
        Variables.Add(MakeShared<FJsonValueObject>(Item));
    }
    Json->SetArrayField(TEXT("variables"), Variables);
    auto Value = Result(true, TEXT("Read only. Coordinates do not replace visual inspection."));
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Value.Json));
    return Value;
}

FWoolooBlueprintResult Edit(const FString& Path, const FString& Root, const FWoolooBlueprintEdit& Edit)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before authoring assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    auto* Asset = LoadBlueprint(Path);
    if (!Asset) return Result(false, TEXT("Blueprint not found."));
    FScopedTransaction Transaction(NSLOCTEXT("WoolooBlueprint", "Edit", "Edit Blueprint"));
    const auto Fail = [&](const FString& Error) { Transaction.Cancel(); return Result(false, Error); };
    Asset->Modify();
    FGuid ChangedId;
    FString Error;
    if (Edit.Operation == TEXT("RemoveInterface"))
    {
        auto* Class = LoadClass<UInterface>(nullptr, *Edit.ClassPath);
        if (!Class || !Asset->ImplementedInterfaces.ContainsByPredicate([&](const auto& Value) { return Value.Interface == Class; }))
            return Fail(TEXT("Implemented interface class not found."));
        FBlueprintEditorUtils::RemoveInterface(Asset, Class->GetClassPathName(), false);
    }
    else if (Edit.Operation == TEXT("RemoveFunction"))
    {
        auto All = Graphs(*Asset);
        auto** Found = All.FindByPredicate([&](const auto* Graph) { return Graph->GetFName() == Edit.FunctionName; });
        if (!Found || !Asset->FunctionGraphs.Contains(*Found)) return Fail(TEXT("Ordinary function graph not found; remove interface implementations via RemoveInterface."));
        FBlueprintEditorUtils::RemoveGraph(Asset, *Found);
    }
    else if (Edit.Operation == TEXT("SetClassDefaults"))
    {
        auto* Defaults = Asset->GeneratedClass ? Asset->GeneratedClass->GetDefaultObject() : nullptr;
        if (!Defaults || !ImportProperties(*Defaults, Edit.Properties, false, Error))
            return Fail(Defaults ? Error : TEXT("Compile first to create the generated class."));
        ImportProperties(*Defaults, Edit.Properties, true, Error);
    }
    else if (Edit.Operation == TEXT("RemoveVariable"))
    {
        if (!Asset->NewVariables.ContainsByPredicate([&](const auto& Variable) { return Variable.VarName == Edit.VariableName; }))
            return Fail(TEXT("Local member variable not found; inherited variables cannot be removed."));
        FBlueprintEditorUtils::RemoveMemberVariable(Asset, Edit.VariableName);
    }
    else
    {
        auto All = Graphs(*Asset);
        auto** Found = All.FindByPredicate([&](const auto* Graph) { return Graph->GetFName() == Edit.GraphName; });
        if (!Found) return Fail(TEXT("Graph name not found."));
        auto* Graph = *Found;
        auto* Schema = Graph->GetSchema();
        if (!Schema) return Fail(TEXT("Graph schema is unavailable."));
        auto* Node = Graph->Nodes.FindByPredicate([&](const auto& Value) { return Value && Value->NodeGuid == Edit.NodeId; });
        auto* Existing = Node ? Node->Get() : nullptr;
        Graph->Modify();
        if (Edit.Operation == TEXT("AddNode"))
        {
            auto* Class = LoadClass<UEdGraphNode>(nullptr, *Edit.ClassPath);
            if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || Existing) return Fail(TEXT("Invalid node class or duplicate node GUID."));
            auto* Added = NewObject<UEdGraphNode>(Graph, Class, NAME_None, RF_Transactional);
            Added->CreateNewGuid();
            if (Edit.NodeId.IsValid()) Added->NodeGuid = Edit.NodeId;
            if (!Edit.FunctionPath.IsEmpty())
            {
                auto* Call = Cast<UK2Node_CallFunction>(Added);
                auto* Function = LoadObject<UFunction>(nullptr, *Edit.FunctionPath);
                if (!Call || !Function) return Fail(TEXT("FunctionPath requires a loaded UFunction and CallFunction node."));
                Call->SetFromFunction(Function);
            }
            if (!Added->IsCompatibleWithGraph(Graph)) return Fail(TEXT("Node class is incompatible with this graph."));
            if (!ImportProperties(*Added, Edit.Properties, false, Error)) return Fail(Error);
            // Match native node spawning: configure the factory/action before AllocateDefaultPins.
            ImportProperties(*Added, Edit.Properties, true, Error, false);
            if (const auto* Input = Cast<UK2Node_EnhancedInputAction>(Added))
            {
                if (!IsValid(Input->InputAction.Get()))
                    return Fail(TEXT("Enhanced Input event nodes require a valid InputAction in Properties at creation."));
                // The native input event spawner also permits only one event node per action in a Blueprint.
                for (const auto* OtherGraph : All)
                    for (const auto& Other : OtherGraph->Nodes)
                        if (const auto* OtherInput = Cast<UK2Node_EnhancedInputAction>(Other.Get());
                            OtherInput && OtherInput->InputAction == Input->InputAction)
                            return Fail(TEXT("This Blueprint already has an event node for the requested InputAction."));
            }
            if (auto* Async = Cast<UK2Node_BaseAsyncTask>(Added))
            {
                const auto* Factory = Async->GetFactoryFunction();
                const auto* Return = Factory ? CastField<FObjectPropertyBase>(Factory->GetReturnProperty()) : nullptr;
                const auto* ProxyField = FindFProperty<FObjectPropertyBase>(Added->GetClass(), TEXT("ProxyClass"));
                const auto* Proxy = ProxyField ? Cast<UClass>(ProxyField->GetObjectPropertyValue_InContainer(Added)) : nullptr;
                if (!Factory || !Factory->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintCallable)
                    || !Return || !Proxy || Return->PropertyClass != Proxy)
                    return Fail(TEXT("Native async node requires a static callable factory and its matching return/proxy class."));
            }
            Graph->AddNode(Added, false, false);
            Added->PostPlacedNewNode();
            Added->AllocateDefaultPins();
            if (const auto* Input = Cast<UK2Node_EnhancedInputAction>(Added);
                Input && !ValidateInputActionNode(*Input, Error))
            {
                Graph->RemoveNode(Added);
                return Fail(Error);
            }
            Added->NodePosX = Edit.X; Added->NodePosY = Edit.Y;
            ChangedId = Added->NodeGuid;
        }
        else if (Edit.Operation == TEXT("SetComment"))
        {
            auto* Comment = Cast<UEdGraphNode_Comment>(Existing);
            if (Existing && !Comment) return Fail(TEXT("NodeId refers to a non-comment node."));
            if (Edit.Width <= 0 || Edit.Height <= 0) return Fail(TEXT("Comment bounds must be positive."));
            if (!Comment)
            {
                Comment = NewObject<UEdGraphNode_Comment>(Graph, NAME_None, RF_Transactional);
                Comment->CreateNewGuid();
                if (Edit.NodeId.IsValid()) Comment->NodeGuid = Edit.NodeId;
                Graph->AddNode(Comment, false, false);
            }
            Comment->Modify();
            Comment->NodeComment = Edit.Value;
            Comment->NodePosX = Edit.X; Comment->NodePosY = Edit.Y;
            Comment->NodeWidth = Edit.Width; Comment->NodeHeight = Edit.Height;
            Comment->CommentColor = Edit.CommentColor;
            ChangedId = Comment->NodeGuid;
        }
        else
        {
            if (!Existing) return Fail(TEXT("Node GUID not found."));
            Existing->Modify();
            ChangedId = Existing->NodeGuid;
            if (Edit.Operation == TEXT("RemoveNode")) FBlueprintEditorUtils::RemoveNode(Asset, Existing, true);
            else if (Edit.Operation == TEXT("MoveNode")) { Existing->NodePosX = Edit.X; Existing->NodePosY = Edit.Y; }
            else if (Edit.Operation == TEXT("SetNodeProperties"))
            {
                if (Existing->IsA<UK2Node_EnhancedInputAction>() && Edit.Properties.Contains(TEXT("InputAction")))
                    return Fail(TEXT("Replace input event nodes with AddNode/RemoveNode; existing pins depend on InputAction."));
                if (Existing->IsA<UK2Node_BaseAsyncTask>()
                    && (Edit.Properties.Contains(TEXT("ProxyFactoryClass"))
                        || Edit.Properties.Contains(TEXT("ProxyFactoryFunctionName"))
                        || Edit.Properties.Contains(TEXT("ProxyClass"))))
                    return Fail(TEXT("Replace async factory nodes with AddNode/RemoveNode; existing pins depend on the complete factory configuration."));
                if (!ImportProperties(*Existing, Edit.Properties, false, Error)) return Fail(Error);
                ImportProperties(*Existing, Edit.Properties, true, Error);
            }
            else
            {
                auto* Pin = Existing->FindPin(Edit.PinName);
                if (!Pin) return Fail(TEXT("Pin name not found."));
                if (Edit.Operation == TEXT("SetPinDefault"))
                {
                    if (Pin->Direction != EGPD_Input || !Pin->LinkedTo.IsEmpty()
                        || Pin->bDefaultValueIsReadOnly || Pin->bDefaultValueIsIgnored)
                        return Fail(TEXT("Set defaults only on editable, unconnected input pins."));
                    const auto* K2 = Cast<UEdGraphSchema_K2>(Schema);
                    if (!K2) return Fail(TEXT("Default authoring currently requires a K2 graph schema."));
                    FString DefaultValue;
                    TObjectPtr<UObject> DefaultObject = nullptr;
                    FText DefaultText;
                    K2->GetPinDefaultValuesFromString(Pin->PinType, Existing, Edit.Value,
                        DefaultValue, DefaultObject, DefaultText);
                    const auto Validation = K2->IsPinDefaultValid(Pin, DefaultValue, DefaultObject, DefaultText);
                    if (!Validation.IsEmpty()) return Fail(Validation);
                    if ((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
                        || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class
                        || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
                        && !Edit.Value.IsEmpty() && Edit.Value != TEXT("None") && !DefaultObject)
                        return Fail(TEXT("Object/class default did not resolve to a loaded object."));
                    // The native setter can silently reject input. Validate the requested value before invoking it.
                    K2->TrySetDefaultValue(*Pin, Edit.Value);
                }
                else if (Edit.Operation == TEXT("ConnectPins") || Edit.Operation == TEXT("DisconnectPins"))
                {
                    auto* Target = Graph->Nodes.FindByPredicate([&](const auto& Value) { return Value && Value->NodeGuid == Edit.TargetNodeId; });
                    auto* Other = Target ? (*Target)->FindPin(Edit.TargetPinName) : nullptr;
                    if (!Other) return Fail(TEXT("Target node/pin not found."));
                    Other->GetOwningNode()->Modify();
                    if (Edit.Operation == TEXT("DisconnectPins")) Schema->BreakSinglePinLink(Pin, Other);
                    else
                    {
                        const auto Response = Schema->CanCreateConnection(Pin, Other);
                        if (Response.Response != CONNECT_RESPONSE_MAKE && !Edit.bAllowBreakingLinks)
                            return Fail(TEXT("Connection would replace links, insert a conversion, or is invalid: ") + Response.Message.ToString());
                        if (!Schema->TryCreateConnection(Pin, Other)) return Fail(TEXT("Schema rejected the connection."));
                    }
                }
                else return Fail(TEXT("Unknown graph operation."));
            }
        }
        Graph->NotifyGraphChanged();
    }
    if (Edit.Operation == TEXT("AddNode") || Edit.Operation == TEXT("RemoveNode")
        || Edit.Operation == TEXT("RemoveFunction") || Edit.Operation == TEXT("RemoveInterface")
        || Edit.Operation == TEXT("RemoveVariable") || Edit.Operation == TEXT("SetNodeProperties"))
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Asset);
    else FBlueprintEditorUtils::MarkBlueprintAsModified(Asset);
    Asset->MarkPackageDirty();
    return Result(true, TEXT("Edited in memory; not compiled or saved."), ChangedId);
}

FWoolooBlueprintResult Compile(const FString& Path, const FString& Root)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before compiling assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    auto* Asset = LoadBlueprint(Path);
    if (!Asset) return Result(false, TEXT("Blueprint not found."));
    // Reject incomplete input events before native expansion can dereference absent pins. Do not repair on compile.
    FString Error;
    for (const auto* Graph : Graphs(*Asset))
        for (const auto& Node : Graph->Nodes)
            if (const auto* Input = Cast<UK2Node_EnhancedInputAction>(Node.Get());
                Input && !ValidateInputActionNode(*Input, Error))
                return Result(false, Error);
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Asset, EBlueprintCompileOptions::None, &Log);
    FString Messages;
    for (const auto& Message : Log.Messages) Messages += Message->ToText().ToString() + TEXT("\n");
    const auto bSuccess = Log.NumErrors == 0 && (Asset->Status == BS_UpToDate || Asset->Status == BS_UpToDateWithWarnings);
    return Result(bSuccess,
        Messages.IsEmpty() ? TEXT("Blueprint compilation completed; not saved.") : Messages);
}
FWoolooBlueprintResult Save(const FString& Path, const FString& Root)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before saving authored assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    auto* Asset = LoadBlueprint(Path);
    if (!Asset) return Result(false, TEXT("Blueprint not found."));
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    const auto Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
    return Result(UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args), TEXT("Save result; only the requested package was processed."));
}
}
