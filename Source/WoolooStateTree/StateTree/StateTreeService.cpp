#include "WoolooStateTree/StateTree/StateTreeService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "GameplayTagContainer.h"
#include "Misc/StringOutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PropertyBindingBinding.h"
#include "PropertyBindingBindableStructDescriptor.h"
#include "PropertyBindingPath.h"
#include "PropertyBindingTypes.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeDelegates.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeFactory.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace Wooloo::StateTree
{
namespace
{
FWoolooStateTreeResult Result(bool bSuccess, const FString& Message, FGuid Id = {})
{
    FWoolooStateTreeResult Value;
    Value.bSuccess = bSuccess;
    Value.Message = Message;
    Value.EntityId = Id;
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

UStateTree* LoadTree(const FString& Path)
{
    return FPackageName::IsValidLongPackageName(Path) ? LoadObject<UStateTree>(nullptr, *Path) : nullptr;
}

TSharedRef<FJsonObject> Properties(const UStruct* Type, const void* Memory)
{
    auto Json = MakeShared<FJsonObject>();
    if (Type && Memory)
    {
        for (TFieldIterator<FProperty> It(Type); It; ++It)
        {
            FString Text;
            It->ExportText_InContainer(0, Text, Memory, Memory, nullptr, PPF_None);
            Json->SetStringField(It->GetName(), Text);
        }
    }
    return Json;
}

TSharedRef<FJsonObject> DescribeNode(const FStateTreeEditorNode& Node)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), Node.ID.ToString());
    Json->SetStringField(TEXT("type"), GetPathNameSafe(Node.Node.GetScriptStruct()));
    Json->SetStringField(TEXT("name"), Node.GetName().ToString());
    Json->SetStringField(TEXT("instance_object"), GetPathNameSafe(Node.InstanceObject));
    const auto View = Node.GetInstance();
    Json->SetStringField(TEXT("instance_type"), GetPathNameSafe(View.GetStruct()));
    Json->SetObjectField(TEXT("instance"), Properties(View.GetStruct(), View.GetMemory()));
    return Json;
}

FStateTreeEditorNode* FindNode(UStateTreeEditorData& Data, FGuid Id, UObject*& Owner)
{
    if (!Id.IsValid()) return nullptr;
    for (auto& Node : Data.GlobalTasks)
    { if (Node.ID == Id) { Owner = &Data; return &Node; } }
    for (auto& Node : Data.Evaluators)
    { if (Node.ID == Id) { Owner = &Data; return &Node; } }
    FStateTreeEditorNode* Found = nullptr;
    Data.VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
    {
        const auto Search = [&](TArray<FStateTreeEditorNode>& Nodes)
        {
            for (auto& Node : Nodes)
            { if (Node.ID == Id) { Found = &Node; Owner = &State; return; } }
        };
        Search(State.Tasks);
        if (!Found) Search(State.EnterConditions);
        if (!Found && State.SingleTask.ID == Id) { Found = &State.SingleTask; Owner = &State; }
        for (auto& Transition : State.Transitions)
        { if (!Found) Search(Transition.Conditions); }
        return Found ? EStateTreeVisitor::Break : EStateTreeVisitor::Continue;
    });
    return Found;
}

// Stage individual reflected values: UObject memory must never be copied as a UScriptStruct.
struct FStagedInstanceProperty
{
    FProperty* Property;
    void* Memory;
    explicit FStagedInstanceProperty(FProperty* InProperty)
        : Property(InProperty), Memory(Property->AllocateAndInitializeValue()) {}
    ~FStagedInstanceProperty() { Property->DestroyValue(Memory); FMemory::Free(Memory); }
};

bool ImportInstance(FStateTreeEditorNode& Node, UObject* Owner, const TMap<FString, FString>& Values,
    bool bApply, FString& Error)
{
    if (Values.IsEmpty()) return true;
    const auto View = Node.GetInstance();
    const auto* Type = View.GetStruct();
    if (!Type || !View.GetMemory()) { Error = TEXT("An initialized node instance is required."); return false; }
    auto* InstanceObject = Node.InstanceObject.Get();
    TArray<TUniquePtr<FStagedInstanceProperty>> Staged;
    for (const auto& Pair : Values)
    {
        auto* Property = Type->FindPropertyByName(*Pair.Key);
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)
            || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_InstancedReference | CPF_ContainsInstancedReference))
        { Error = TEXT("Not an editable instance property: ") + Pair.Key; return false; }
        auto Value = MakeUnique<FStagedInstanceProperty>(Property);
        Property->CopyCompleteValue(Value->Memory, Property->ContainerPtrToValuePtr<void>(View.GetMutableMemory()));
        FStringOutputDevice ImportErrors;
        const auto* Rest = Property->ImportText_Direct(*Pair.Value, Value->Memory,
            InstanceObject ? InstanceObject : Owner, PPF_None, &ImportErrors);
        if (!Rest || !FString(Rest).TrimStartAndEnd().IsEmpty() || !ImportErrors.IsEmpty())
        { Error = TEXT("Import failed for ") + Pair.Key + TEXT(": ") + ImportErrors; return false; }
        Staged.Add(MoveTemp(Value));
    }
    if (bApply)
    {
        if (InstanceObject) InstanceObject->Modify();
        // Commit only requested fields; preserve UObject identity, other fields and nested instance ownership.
        for (const auto& Value : Staged)
        {
            if (InstanceObject) InstanceObject->PreEditChange(Value->Property);
            Value->Property->CopyCompleteValue(
                Value->Property->ContainerPtrToValuePtr<void>(View.GetMutableMemory()), Value->Memory);
        }
        auto* Base = Node.Node.GetMutablePtr<FStateTreeNodeBase>();
        for (const auto& Value : Staged)
        {
            auto* Property = Value->Property;
            FEditPropertyChain Chain;
            Chain.AddTail(Property);
            Chain.SetActivePropertyNode(Property);
            Chain.SetActiveMemberPropertyNode(Property);
            FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
            FPropertyChangedChainEvent ChainEvent(Chain, Event);
            if (InstanceObject) InstanceObject->PostEditChangeChainProperty(ChainEvent);
            if (Base) Base->PostEditInstanceDataChangeChainProperty(ChainEvent, Node.GetInstance());
        }
    }
    return true;
}
}

FWoolooStateTreeResult Describe(const FString& Path)
{
    auto* Tree = LoadTree(Path);
    auto* Data = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
    if (!Data) return Result(false, TEXT("StateTree editor data not found."));
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("schema"), Data->Schema ? Data->Schema->GetClass()->GetPathName() : FString());
    TArray<TSharedPtr<FJsonValue>> Context;
    if (Data->Schema)
    {
        for (const auto& Desc : Data->Schema->GetContextDataDescs())
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("id"), Desc.ID.ToString());
            Item->SetStringField(TEXT("name"), Desc.Name.ToString());
            Item->SetStringField(TEXT("type"), GetPathNameSafe(Desc.Struct));
            Context.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
    Json->SetArrayField(TEXT("context"), Context);
    Json->SetStringField(TEXT("root_parameters_id"), Data->GetRootParametersGuid().ToString());
    const auto Parameters = Data->GetRootParametersPropertyBag().GetValue();
    Json->SetObjectField(TEXT("parameters"), Properties(Parameters.GetScriptStruct(), Parameters.GetMemory()));
    Json->SetBoolField(TEXT("needs_compile"), UStateTreeEditingSubsystem::NeedsRecompile(Tree));
    TArray<TSharedPtr<FJsonValue>> GlobalTasks;
    for (const auto& Node : Data->GlobalTasks) GlobalTasks.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
    Json->SetArrayField(TEXT("global_tasks"), GlobalTasks);
    TArray<TSharedPtr<FJsonValue>> Evaluators;
    for (const auto& Node : Data->Evaluators) Evaluators.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
    Json->SetArrayField(TEXT("evaluators"), Evaluators);
    TArray<TSharedPtr<FJsonValue>> States;
    Data->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* Parent)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), State.ID.ToString());
        Item->SetStringField(TEXT("parent"), Parent ? Parent->ID.ToString() : FString());
        Item->SetStringField(TEXT("name"), State.Name.ToString());
        Item->SetStringField(TEXT("type"), StaticEnum<EStateTreeStateType>()->GetNameStringByValue(static_cast<int64>(State.Type)));
        TArray<TSharedPtr<FJsonValue>> Tasks;
        for (const auto& Node : State.Tasks) Tasks.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
        Item->SetArrayField(TEXT("tasks"), Tasks);
        if (State.SingleTask.Node.IsValid()) Item->SetObjectField(TEXT("single_task"), DescribeNode(State.SingleTask));
        TArray<TSharedPtr<FJsonValue>> Conditions;
        for (const auto& Node : State.EnterConditions) Conditions.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
        Item->SetArrayField(TEXT("enter_conditions"), Conditions);
        TArray<TSharedPtr<FJsonValue>> Transitions;
        for (const auto& Transition : State.Transitions)
        {
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("id"), Transition.ID.ToString());
            Entry->SetStringField(TEXT("target"), Transition.State.ID.ToString());
            Entry->SetStringField(TEXT("target_type"), StaticEnum<EStateTreeTransitionType>()->GetNameStringByValue(static_cast<int64>(Transition.State.LinkType)));
            Entry->SetStringField(TEXT("trigger"), StaticEnum<EStateTreeTransitionTrigger>()->GetNameStringByValue(static_cast<int64>(Transition.Trigger)));
            Entry->SetStringField(TEXT("event"), Transition.RequiredEvent.Tag.ToString());
            Entry->SetStringField(TEXT("priority"), StaticEnum<EStateTreeTransitionPriority>()->GetNameStringByValue(static_cast<int64>(Transition.Priority)));
            TArray<TSharedPtr<FJsonValue>> Tests;
            for (const auto& Node : Transition.Conditions) Tests.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
            Entry->SetArrayField(TEXT("conditions"), Tests);
            Transitions.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Item->SetArrayField(TEXT("transitions"), Transitions);
        States.Add(MakeShared<FJsonValueObject>(Item));
        return EStateTreeVisitor::Continue;
    });
    Json->SetArrayField(TEXT("states"), States);
    TArray<TSharedPtr<FJsonValue>> Bindings;
    Data->GetPropertyEditorBindings()->ForEachBinding([&](const FPropertyBindingBinding& Binding)
    {
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("source_id"), Binding.GetSourcePath().GetStructID().ToString());
        Entry->SetStringField(TEXT("source_path"), Binding.GetSourcePath().ToString());
        Entry->SetStringField(TEXT("target_id"), Binding.GetTargetPath().GetStructID().ToString());
        Entry->SetStringField(TEXT("target_path"), Binding.GetTargetPath().ToString());
        Bindings.Add(MakeShared<FJsonValueObject>(Entry));
    });
    Json->SetArrayField(TEXT("bindings"), Bindings);
    auto Value = Result(true, TEXT("Read only."));
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Value.Json));
    return Value;
}

FWoolooStateTreeResult Create(const FString& Path, const FString& Root, const FString& SchemaPath)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before authoring assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    if (FindObject<UObject>(nullptr, *(Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path)))
        || FPackageName::DoesPackageExist(Path)) return Result(false, TEXT("Asset already exists; use targeted edits."));
    auto* Schema = LoadClass<UStateTreeSchema>(nullptr, *SchemaPath);
    if (!Schema || Schema->HasAnyClassFlags(CLASS_Abstract) || !Schema->HasAnyClassFlags(CLASS_Native))
        return Result(false, TEXT("A concrete native schema class is required."));
    auto* Package = CreatePackage(*Path);
    auto* Factory = NewObject<UStateTreeFactory>();
    Factory->SetSchemaClass(Schema);
    auto* Tree = Cast<UStateTree>(Factory->FactoryCreateNew(UStateTree::StaticClass(), Package,
        *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    if (!Tree) return Result(false, TEXT("Native StateTree factory failed."));
    FAssetRegistryModule::AssetCreated(Tree);
    Tree->MarkPackageDirty();
    return Result(true, TEXT("Created in memory; compile and save explicitly."));
}

FWoolooStateTreeResult Edit(const FString& Path, const FString& Root, const FWoolooStateTreeEdit& Edit)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before authoring assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    auto* Tree = LoadTree(Path);
    auto* Data = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
    if (!Data) return Result(false, TEXT("StateTree editor data not found."));
    auto* State = Data->GetMutableStateByID(Edit.StateId);
    auto* Parent = Data->GetMutableStateByID(Edit.SourceId);
    FString Error;
    FGuid ChangedId;
    FScopedTransaction Transaction(NSLOCTEXT("WoolooStateTree", "Edit", "Edit StateTree"));
    Tree->Modify();
    Data->Modify();
    if (State) State->Modify();
    if (Parent) Parent->Modify();
    const auto Fail = [&](const FString& Message) { Transaction.Cancel(); return Result(false, Message); };

    if (Edit.Operation == TEXT("EnsureObjectParameter"))
    {
        auto* Class = LoadClass<UObject>(nullptr, *Edit.TypePath);
        if (!Class || Edit.Name.IsNone()) return Fail(TEXT("Parameter name and object class are required."));
        const auto* Existing = Data->GetRootParametersPropertyBag().FindPropertyDescByName(Edit.Name);
        if (Existing)
        {
            if (Existing->ValueType != EPropertyBagPropertyType::Object || Existing->ValueTypeObject != Class
                || Existing->ContainerTypes.Num() != 0) return Fail(TEXT("Existing parameter has a different type; no changes applied."));
        }
        else
        {
            UE::PropertyBinding::FPropertyCreationDescriptor Creation;
            Creation.PropertyDesc = FPropertyBagPropertyDesc(Edit.Name, EPropertyBagPropertyType::Object, Class);
            Data->CreateRootProperties(MakeArrayView(&Creation, 1));
            if (Creation.PropertyDesc.Name != Edit.Name) return Fail(TEXT("Native parameter creation renamed the property unexpectedly."));
        }
        ChangedId = Data->GetRootParametersGuid();
        UStateTreeEditingSubsystem::MarkAsPubliclyModified(Tree);
        UE::StateTree::Delegates::OnParametersChanged.Broadcast(*Tree);
    }
    else if (Edit.Operation == TEXT("AddState"))
    {
        if (Edit.Name.IsNone() || (Edit.SourceId.IsValid() && !Parent)) return Fail(TEXT("Name or parent state is invalid."));
        if (Edit.StateId.IsValid() && State) return Fail(TEXT("State ID already exists."));
        auto& Added = Parent ? Parent->AddChildState(Edit.Name) : Data->AddSubTree(Edit.Name);
        if (Edit.StateId.IsValid()) Added.ID = Edit.StateId;
        ChangedId = Added.ID;
    }
    else if (Edit.Operation == TEXT("RemoveState"))
    {
        if (!State) return Fail(TEXT("State ID not found."));
        if (State->Parent) { State->Parent->Modify(); State->Parent->Children.Remove(State); }
        else Data->SubTrees.Remove(State);
        Data->ReparentStates();
        Data->RemoveInvalidBindings();
    }
    else if (Edit.Operation == TEXT("SetInstanceProperties"))
    {
        UObject* Owner = nullptr;
        auto* Node = FindNode(*Data, Edit.NodeId, Owner);
        if (!Node || !Node->Node.IsValid() || !Node->Node.GetScriptStruct()->IsChildOf(FStateTreeNodeBase::StaticStruct()))
            return Fail(TEXT("A valid native StateTree node ID is required."));
        if (!ImportInstance(*Node, Owner, Edit.Properties, false, Error)) return Fail(Error);
        Owner->Modify();
        if (!ImportInstance(*Node, Owner, Edit.Properties, true, Error)) return Fail(Error);
        ChangedId = Node->ID;
    }
    else if (Edit.Operation == TEXT("AddTask") || Edit.Operation == TEXT("AddEnterCondition")
        || Edit.Operation == TEXT("AddTransitionCondition"))
    {
        if (!State) return Fail(TEXT("State ID not found."));
        auto* Type = LoadObject<UStruct>(nullptr, *Edit.TypePath);
        auto* Struct = Cast<UScriptStruct>(Type);
        auto* TaskClass = Cast<UClass>(Type);
        const auto bTask = Edit.Operation == TEXT("AddTask");
        if (TaskClass)
        {
            if (!bTask || !TaskClass->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass())
                || TaskClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
                || (Data->Schema && !Data->Schema->IsClassAllowed(TaskClass)))
                return Fail(TEXT("AddTask requires a concrete StateTree task class allowed by the schema."));
        }
        else if (!Struct || !Struct->IsChildOf(bTask ? FStateTreeTaskBase::StaticStruct() : FStateTreeConditionBase::StaticStruct())
            || (Data->Schema && !Data->Schema->IsStructAllowed(Struct)))
            return Fail(TEXT("Node struct is incompatible with the schema or operation."));
        UObject* ExistingOwner = nullptr;
        if (Edit.NodeId.IsValid() && FindNode(*Data, Edit.NodeId, ExistingOwner)) return Fail(TEXT("Node ID already exists."));
        auto* Nodes = bTask ? &State->Tasks : &State->EnterConditions;
        if (Edit.Operation == TEXT("AddTransitionCondition"))
        {
            auto* Transition = State->Transitions.FindByPredicate([&](const auto& Item) { return Item.ID == Edit.TransitionId; });
            if (!Transition) return Fail(TEXT("Transition ID not found."));
            Nodes = &Transition->Conditions;
        }
        FStateTreeEditorNode Added;
        if (TaskClass)
        {
            // Match the native picker: set the class before InitializeAs allocates instance data.
            FStateTreeBlueprintTaskWrapper Wrapper;
            Wrapper.TaskClass = TaskClass;
            Added.InitializeAs<FStateTreeBlueprintTaskWrapper>(State, MoveTemp(Wrapper));
        }
        else Added.InitializeAs(State, Struct);
        if (Edit.NodeId.IsValid()) Added.ID = Edit.NodeId;
        if (!Edit.Name.IsNone()) Added.SetNodeName(Edit.Name);
        if (!ImportInstance(Added, State, Edit.Properties, true, Error)) return Fail(Error);
        ChangedId = Added.ID;
        Nodes->Add(MoveTemp(Added));
    }
    else if (Edit.Operation == TEXT("RemoveNode"))
    {
        UObject* Owner = nullptr;
        if (!FindNode(*Data, Edit.NodeId, Owner)) return Fail(TEXT("Node ID not found."));
        Owner->Modify();
        const auto Matches = [&](const FStateTreeEditorNode& Node) { return Node.ID == Edit.NodeId; };
        Data->GlobalTasks.RemoveAll(Matches);
        Data->Evaluators.RemoveAll(Matches);
        if (auto* NodeState = Cast<UStateTreeState>(Owner))
        {
            NodeState->Tasks.RemoveAll(Matches);
            NodeState->EnterConditions.RemoveAll(Matches);
            if (NodeState->SingleTask.ID == Edit.NodeId) NodeState->SingleTask.Reset();
            for (auto& Transition : NodeState->Transitions) Transition.Conditions.RemoveAll(Matches);
        }
        Data->RemoveInvalidBindings();
    }
    else if (Edit.Operation == TEXT("SetTransition"))
    {
        if (!State) return Fail(TEXT("State ID not found."));
        const auto Trigger = StaticEnum<EStateTreeTransitionTrigger>()->GetValueByNameString(Edit.Trigger);
        const auto Type = StaticEnum<EStateTreeTransitionType>()->GetValueByNameString(Edit.TargetType);
        const auto Priority = StaticEnum<EStateTreeTransitionPriority>()->GetValueByNameString(Edit.Priority);
        auto* Target = Data->GetMutableStateByID(Edit.TargetStateId);
        const auto Tag = Edit.EventTag.IsEmpty() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(*Edit.EventTag, false);
        if (Trigger == INDEX_NONE || Type == INDEX_NONE || Priority == INDEX_NONE
            || (Type == static_cast<int64>(EStateTreeTransitionType::GotoState) && !Target)
            || (!Edit.EventTag.IsEmpty() && !Tag.IsValid())) return Fail(TEXT("Invalid transition enum, target or event tag."));
        auto* Transition = State->Transitions.FindByPredicate([&](const auto& Item) { return Item.ID == Edit.TransitionId; });
        if (!Transition)
        {
            Transition = &State->AddTransition(static_cast<EStateTreeTransitionTrigger>(Trigger), static_cast<EStateTreeTransitionType>(Type), Target);
            if (Edit.TransitionId.IsValid()) Transition->ID = Edit.TransitionId;
        }
        Transition->Trigger = static_cast<EStateTreeTransitionTrigger>(Trigger);
        Transition->State = Type == static_cast<int64>(EStateTreeTransitionType::GotoState)
            ? Target->GetLinkToState() : FStateTreeStateLink(static_cast<EStateTreeTransitionType>(Type));
        Transition->Priority = static_cast<EStateTreeTransitionPriority>(Priority);
        Transition->RequiredEvent.Tag = Tag;
        ChangedId = Transition->ID;
    }
    else if (Edit.Operation == TEXT("RemoveTransition"))
    {
        if (!State || !State->Transitions.ContainsByPredicate([&](const auto& Item) { return Item.ID == Edit.TransitionId; }))
            return Fail(TEXT("Transition ID not found."));
        State->Transitions.RemoveAll([&](const auto& Item) { return Item.ID == Edit.TransitionId; });
        Data->RemoveInvalidBindings();
    }
    else if (Edit.Operation == TEXT("BindProperty") || Edit.Operation == TEXT("RemoveBinding"))
    {
        FPropertyBindingPath Target;
        Target.SetStructID(Edit.NodeId);
        if (Edit.TargetPath.IsEmpty() || !Target.FromString(Edit.TargetPath) || !Edit.NodeId.IsValid())
            return Fail(TEXT("Invalid target binding path."));
        TInstancedStruct<FPropertyBindingBindableStructDescriptor> TargetDesc;
        TArray<FPropertyBindingPathIndirection> TargetAccess;
        if (!Data->GetBindableStructByID(Edit.NodeId, TargetDesc)
            || !Target.ResolveIndirections(TargetDesc.Get().Struct, TargetAccess, &Error) || TargetAccess.IsEmpty()
            || !TargetAccess.Last().GetProperty())
            return Fail(TEXT("Target binding property could not be resolved: ") + Error);
        if (Edit.Operation == TEXT("RemoveBinding")) Data->RemovePropertyBinding(Target);
        else
        {
            FPropertyBindingPath Source;
            Source.SetStructID(Edit.SourceId.IsValid() ? Edit.SourceId : Data->GetRootParametersGuid());
            if (!Source.FromString(Edit.SourcePath)) return Fail(TEXT("Invalid source binding path."));
            TInstancedStruct<FPropertyBindingBindableStructDescriptor> SourceDesc;
            TArray<FPropertyBindingPathIndirection> SourceAccess;
            TArray<TInstancedStruct<FPropertyBindingBindableStructDescriptor>> Accessible;
            Data->GetBindableStructs(Edit.NodeId, Accessible);
            if (!Accessible.ContainsByPredicate([&](const auto& Desc) { return Desc.Get().ID == Source.GetStructID(); })
                || !Data->GetBindableStructByID(Source.GetStructID(), SourceDesc))
                return Fail(TEXT("Source binding is unavailable in the target execution path."));
            const auto* TargetProperty = TargetAccess.Last().GetProperty();
            if (Edit.SourcePath.IsEmpty())
            {
                // Native bindings can copy a whole context object/struct into a matching property.
                const auto* ObjectProperty = CastField<FObjectPropertyBase>(TargetProperty);
                const auto* StructProperty = CastField<FStructProperty>(TargetProperty);
                const auto* SourceType = SourceDesc.Get().Struct.Get();
                if (!SourceType
                    || !((ObjectProperty && SourceType->IsChildOf(ObjectProperty->PropertyClass))
                        || (StructProperty && SourceType == StructProperty->Struct)))
                    return Fail(TEXT("Whole-source binding type is incompatible with the target property."));
            }
            else
            {
                if (!Source.ResolveIndirections(SourceDesc.Get().Struct, SourceAccess, &Error) || SourceAccess.IsEmpty())
                    return Fail(TEXT("Source binding property could not be resolved: ") + Error);
                if (UE::PropertyBinding::GetPropertyCompatibility(SourceAccess.Last().GetProperty(), TargetProperty)
                    == UE::PropertyBinding::EPropertyCompatibility::Incompatible)
                    return Fail(TEXT("Binding property types are incompatible."));
            }
            Data->AddPropertyBinding(Source, Target);
            Data->OnPropertyBindingChanged(Source, Target);
        }
    }
    else return Fail(TEXT("Unknown operation. Inspect FWoolooStateTreeEdit documentation."));

    Data->UpdateBindings();
    UStateTreeEditingSubsystem::MarkAsModified(Tree);
    Tree->MarkPackageDirty();
    if (GEditor)
    {
        if (auto* Subsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
        {
            if (auto ViewModel = Subsystem->FindViewModel(Tree)) ViewModel->NotifyAssetChangedExternally();
        }
    }
    return Result(true, TEXT("Edited in memory; not compiled or saved."), ChangedId);
}

FWoolooStateTreeResult Compile(const FString& Path, const FString& Root)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before compiling assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    auto* Tree = LoadTree(Path);
    if (!Tree) return Result(false, TEXT("StateTree not found."));
    FStateTreeCompilerLog Log;
    const auto bSuccess = UStateTreeEditingSubsystem::CompileStateTree(Tree, Log);
    FString Messages;
    for (const auto& Message : Log.ToTokenizedMessages()) Messages += Message->ToText().ToString() + TEXT("\n");
    return Result(bSuccess, Messages.IsEmpty() ? (bSuccess ? TEXT("Compiled; not saved.") : TEXT("Compilation failed.")) : Messages);
}

FWoolooStateTreeResult Save(const FString& Path, const FString& Root)
{
    if (GEditor && GEditor->PlayWorld) return Result(false, TEXT("Stop PIE before saving authored assets."));
    if (!InScope(Path, Root)) return Result(false, TEXT("Asset path is outside the explicit content root."));
    auto* Tree = LoadTree(Path);
    if (!Tree) return Result(false, TEXT("StateTree not found."));
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    const auto Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
    return Result(UPackage::SavePackage(Tree->GetOutermost(), Tree, *Filename, Args), TEXT("Save result; only the requested package was processed."));
}
}
