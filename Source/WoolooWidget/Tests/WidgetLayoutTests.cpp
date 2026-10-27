#if WITH_DEV_AUTOMATION_TESTS && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

#include "WoolooWidget/Tools/WidgetToolset.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"

namespace
{
// Explicitly invoked tests use unique in-memory packages; no asset is saved or registered on disk.
struct FWidgetFixture
{
    FString Root = TEXT("/Game/WoolooWidgetTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<TStrongObjectPtr<UWidgetBlueprint>> Blueprints;

    UWidgetBlueprint* Create(const TCHAR* Name, UClass* Parent = UUserWidget::StaticClass())
    {
        auto* Package = CreatePackage(*(Root + TEXT("/") + Name));
        Package->SetFlags(RF_Transient);
        auto* Blueprint = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(Parent, Package, Name,
            BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
        if (!Blueprint->WidgetTree)
            Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
        Blueprints.Emplace(Blueprint);
        return Blueprint;
    }

    ~FWidgetFixture()
    {
        // Leave the user's Undo history intact. Test packages are transient and never saved.
        for (auto& Blueprint : Blueprints)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
            Blueprint->SetFlags(RF_Transient);
            Blueprint->GetOutermost()->SetDirtyFlag(false);
        }
    }

    FWoolooWidgetResult Set(UWidgetBlueprint* Blueprint, const FWoolooWidgetLayoutChange& Change) const
    {
        return UWoolooWidgetToolset::SetWidgetLayout(Blueprint->GetOutermost()->GetName(), Root, Change);
    }
};

TSharedPtr<FJsonObject> Json(const FWoolooWidgetResult& Result)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Json), Out);
    return Out;
}

void RegisterWidgetNames(UWidgetBlueprint* Blueprint)
{
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            Blueprint->OnVariableAdded(Widget->GetFName());
    });
}

UWidgetBlueprint* ParentWithSlot(FWidgetFixture& Fixture)
{
    auto* Parent = Fixture.Create(TEXT("Parent"));
    auto* Border = Parent->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ParentRoot"));
    Border->SetContent(Parent->WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(), TEXT("Body")));
    Parent->WidgetTree->RootWidget = Border;
    RegisterWidgetNames(Parent);
    FKismetEditorUtilities::CompileBlueprint(Parent);
    return Parent;
}

FWoolooWidgetLayoutChange Inherit(bool bDiscard = true)
{
    FWoolooWidgetLayoutChange Change;
    Change.bDiscardLocalRoot = bDiscard;
    return Change;
}

FWoolooWidgetLayoutChange Local(UClass* Class, FName Name, bool bDiscard = false)
{
    FWoolooWidgetLayoutChange Change;
    Change.Mode = EWoolooWidgetLayoutMode::Local;
    Change.RootWidgetClassPath = Class->GetPathName();
    Change.RootWidgetName = Name;
    Change.bDiscardLocalRoot = bDiscard;
    return Change;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooWidgetDescribeTest, "Wooloo.Widget.Layout.SourceAndCompiledOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooWidgetDescribeTest::RunTest(const FString& Parameters)
{
    FWidgetFixture Fixture;
    auto* Parent = ParentWithSlot(Fixture);
    auto* Middle = Fixture.Create(TEXT("Middle"), Parent->GeneratedClass);
    auto* Child = Fixture.Create(TEXT("Child"), Middle->GeneratedClass);
    const auto Path = Child->GetOutermost()->GetName();
    auto Result = UWoolooWidgetToolset::DescribeWidgetLayout(Path);
    TestTrue(TEXT("Inherited inspection succeeds"), Result.bSuccess);
    auto Data = Json(Result);
    if (!TestTrue(TEXT("Valid JSON"), Data.IsValid())) return false;
    TestEqual(TEXT("No local root inherits"), Data->GetStringField(TEXT("source_mode")), FString(TEXT("Inherit")));
    TestEqual(TEXT("Traversal reaches the ancestor with a root"),
        Data->GetObjectField(TEXT("source_layout"))->GetStringField(TEXT("blueprint")), Parent->GetPathName());

    // An unsaved local root must not be mistaken for the old compiled inherited tree.
    Child->WidgetTree->RootWidget = Child->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LocalRoot"));
    Child->Status = BS_Dirty;
    Child->GetOutermost()->SetDirtyFlag(false);
    Result = UWoolooWidgetToolset::DescribeWidgetLayout(Path);
    Data = Json(Result);
    if (!TestTrue(TEXT("Valid JSON after source edit"), Data.IsValid())) return false;
    TestEqual(TEXT("Source selects the local root"), Data->GetStringField(TEXT("source_mode")), FString(TEXT("Local")));
    TestTrue(TEXT("Competing roots are diagnosed"), Result.Json.Contains(TEXT("competing widget hierarchies")));
    TestTrue(TEXT("Compiled result is labelled stale"), Data->GetObjectField(TEXT("compiled_layout"))->GetBoolField(TEXT("may_be_stale")));
    TestFalse(TEXT("Inspection did not dirty the package"), Child->GetOutermost()->IsDirty());
    TestTrue(TEXT("Inspection did not compile"), Child->Status == BS_Dirty);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooWidgetInheritTest, "Wooloo.Widget.Layout.InheritPreservesSlotsAndUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooWidgetInheritTest::RunTest(const FString& Parameters)
{
    FWidgetFixture Fixture;
    auto* Parent = ParentWithSlot(Fixture);
    auto* Child = Fixture.Create(TEXT("Child"), Parent->GeneratedClass);
    auto* OldRoot = Child->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LocalRoot"));
    Child->WidgetTree->RootWidget = OldRoot;
    auto* Content = Child->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotContent"));
    Child->WidgetTree->SetContentForSlot(TEXT("Body"), Content);
    RegisterWidgetNames(Child);
    const auto RootGuid = Child->WidgetVariableNameToGuidMap.FindChecked(OldRoot->GetFName());
    Child->GetOutermost()->SetDirtyFlag(false);
    const auto ParentRoot = Parent->WidgetTree->RootWidget;

    auto Result = Fixture.Set(Child, Inherit(false));
    TestFalse(TEXT("Discard permission is required"), Result.bSuccess);
    TestTrue(TEXT("Rejected operation preserves root"), Child->WidgetTree->RootWidget == OldRoot);
    TestFalse(TEXT("Rejected operation does not dirty package"), Child->GetOutermost()->IsDirty());

    Result = Fixture.Set(Child, Inherit());
    if (!TestTrue(*Result.Message, Result.bSuccess)) return false;
    TestNull(TEXT("Local root removed"), Child->WidgetTree->RootWidget.Get());
    TestTrue(TEXT("Named Slot content identity preserved"), Child->WidgetTree->GetContentForSlot(TEXT("Body")) == Content);
    TestTrue(TEXT("Parent remains untouched"), Parent->WidgetTree->RootWidget == ParentRoot);
    TestTrue(TEXT("Old root was detached from the asset"), OldRoot->GetOuter() == GetTransientPackage());
    TestFalse(TEXT("Removed root metadata is cleared"), Child->WidgetVariableNameToGuidMap.Contains(TEXT("LocalRoot")));
    TestTrue(TEXT("Undo succeeds"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores root identity and ownership"), Child->WidgetTree->RootWidget == OldRoot && OldRoot->GetOuter() == Child->WidgetTree);
    TestTrue(TEXT("Undo restores root GUID"), Child->WidgetVariableNameToGuidMap.FindRef(TEXT("LocalRoot")) == RootGuid);
    TestTrue(TEXT("Undo retains Named Slot content"), Child->WidgetTree->GetContentForSlot(TEXT("Body")) == Content);
    TestTrue(TEXT("Redo succeeds"), GEditor->RedoTransaction());
    TestNull(TEXT("Redo removes local root"), Child->WidgetTree->RootWidget.Get());
    FKismetEditorUtilities::CompileBlueprint(Child);
    TestTrue(TEXT("Inherited Blueprint compiles"), Child->Status == BS_UpToDate || Child->Status == BS_UpToDateWithWarnings);
    auto* Generated = Cast<UWidgetBlueprintGeneratedClass>(Child->GeneratedClass);
    TestTrue(TEXT("Native compiled layout belongs to the parent"), Generated && Generated->FindWidgetTreeOwningClass() == Parent->GeneratedClass);
    Result = Fixture.Set(Child, Local(UBorder::StaticClass(), TEXT("ConflictingRoot")));
    TestFalse(TEXT("Creating a competing local hierarchy is rejected"), Result.bSuccess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooWidgetDependencyTest, "Wooloo.Widget.Layout.RejectionPreservesDependencies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooWidgetDependencyTest::RunTest(const FString& Parameters)
{
    FWidgetFixture Fixture;
    auto* Parent = ParentWithSlot(Fixture);
    auto* Child = Fixture.Create(TEXT("Child"), Parent->GeneratedClass);
    auto* Root = Child->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LocalRoot"));
    auto* Label = Child->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
    Root->SetContent(Label);
    Child->WidgetTree->RootWidget = Root;
    RegisterWidgetNames(Child);
    FDelegateEditorBinding Binding;
    Binding.ObjectName = TEXT("Label");
    Binding.PropertyName = TEXT("Text");
    Child->Bindings.Add(Binding);
    auto Result = Fixture.Set(Child, Inherit());
    TestFalse(TEXT("Binding blocks subtree removal"), Result.bSuccess);
    TestTrue(TEXT("Binding blocker identifies the cause"), Result.Json.Contains(TEXT("Property/delegate binding")));
    TestEqual(TEXT("Binding is not automatically deleted"), Child->Bindings.Num(), 1);
    TestTrue(TEXT("Subtree is retained"), Child->WidgetTree->RootWidget == Root && Root->GetContent() == Label);
    Child->Bindings.Reset();

    auto* Animation = NewObject<UWidgetAnimation>(Child, TEXT("TestAnimation"));
    FWidgetAnimationBinding AnimationBinding;
    AnimationBinding.WidgetName = TEXT("Label");
    Animation->AnimationBindings.Add(AnimationBinding);
    Child->Animations.Add(Animation);
    Result = Fixture.Set(Child, Inherit());
    TestFalse(TEXT("Animation blocks subtree removal"), Result.bSuccess);
    TestTrue(TEXT("Animation blocker identifies the cause"), Result.Json.Contains(TEXT("Animation references")));
    TestEqual(TEXT("Animation binding is retained"), Animation->AnimationBindings.Num(), 1);
    Child->Animations.Reset();

    auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Child, TEXT("DependencyGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    Child->UbergraphPages.Add(Graph);
    auto* GetWidget = NewObject<UK2Node_VariableGet>(Graph);
    GetWidget->CreateNewGuid();
    GetWidget->VariableReference.SetSelfMember(TEXT("Label"));
    Graph->AddNode(GetWidget, false, false);
    Result = Fixture.Set(Child, Inherit());
    TestFalse(TEXT("Uncompiled variable reference blocks deletion without relying on a generated GUID"), Result.bSuccess);
    TestTrue(TEXT("Graph blocker identifies the cause"), Result.Json.Contains(TEXT("Graph node references")));
    TestTrue(TEXT("Referenced graph node is retained"), Graph->Nodes.Contains(GetWidget));
    Child->UbergraphPages.Remove(Graph);

    auto* Content = Child->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotContent"));
    Child->WidgetTree->SetContentForSlot(TEXT("MissingSlot"), Content);
    Result = Fixture.Set(Child, Inherit());
    TestFalse(TEXT("Missing inherited Named Slot blocks switch"), Result.bSuccess);
    TestTrue(TEXT("Rejected switch preserves Named Slot map"), Child->WidgetTree->GetContentForSlot(TEXT("MissingSlot")) == Content);
    Child->WidgetTree->SetContentForSlot(TEXT("MissingSlot"), nullptr);
    Child->WidgetTree->SetContentForSlot(TEXT("Body"), Content);
    auto* Slot = CastChecked<UNamedSlot>(Parent->WidgetTree->FindWidget(TEXT("Body")));
    Slot->SetContent(Parent->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ParentContent")));
    Result = Fixture.Set(Child, Inherit());
    TestFalse(TEXT("Unsaved ancestor slot occupancy is respected"), Result.bSuccess);
    TestTrue(TEXT("All failed preflights retained local root"), Child->WidgetTree->RootWidget == Root);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooWidgetLocalTest, "Wooloo.Widget.Layout.LocalAndScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooWidgetLocalTest::RunTest(const FString& Parameters)
{
    FWidgetFixture Fixture;
    auto* Asset = Fixture.Create(TEXT("LocalWidget"));
    auto Change = Local(UCanvasPanel::StaticClass(), TEXT("LocalRoot"));
    auto Result = Fixture.Set(Asset, Change);
    if (!TestTrue(*Result.Message, Result.bSuccess)) return false;
    auto* Root = Asset->WidgetTree->RootWidget.Get();
    TestTrue(TEXT("Requested concrete root created"), Root && Root->IsA<UCanvasPanel>());
    Asset->GetOutermost()->SetDirtyFlag(false);
    Result = Fixture.Set(Asset, Change);
    TestTrue(TEXT("Identical selection is a no-op"), Result.bSuccess && Asset->WidgetTree->RootWidget == Root);
    TestFalse(TEXT("No-op does not dirty package"), Asset->GetOutermost()->IsDirty());
    Result = Fixture.Set(Asset, Local(UBorder::StaticClass(), TEXT("Replacement")));
    TestFalse(TEXT("Replacement requires explicit discard"), Result.bSuccess);
    Result = Fixture.Set(Asset, Local(UBorder::StaticClass(), TEXT("Bad.Name"), true));
    TestFalse(TEXT("Invalid object name is rejected"), Result.bSuccess);
    Result = Fixture.Set(Asset, Local(UBorder::StaticClass(), TEXT("Visibility"), true));
    TestFalse(TEXT("Native member name conflict is rejected"), Result.bSuccess);
    Result = Fixture.Set(Asset, Local(Asset->GeneratedClass, TEXT("RecursiveRoot"), true));
    TestFalse(TEXT("Self-nesting is rejected"), Result.bSuccess);
    Result = Fixture.Set(Asset, Inherit());
    TestFalse(TEXT("Inheritance without ancestor layout is rejected"), Result.bSuccess);
    const auto Path = Asset->GetOutermost()->GetName();
    Result = UWoolooWidgetToolset::SetWidgetLayout(Path, TEXT("/Game"), Change);
    TestFalse(TEXT("Whole mount is not an allowed authoring scope"), Result.bSuccess);
    Result = UWoolooWidgetToolset::SetWidgetLayout(Path, Fixture.Root + TEXT("_Sibling"), Change);
    TestFalse(TEXT("Sibling path prefix cannot escape scope"), Result.bSuccess);
    Result = UWoolooWidgetToolset::SetWidgetLayout(TEXT("/Engine/EditorWidgets/Example"), TEXT("/Engine/EditorWidgets"), Change);
    TestFalse(TEXT("Engine-owned packages are rejected before loading"), Result.bSuccess);
    TestTrue(TEXT("Failed calls preserve root"), Asset->WidgetTree->RootWidget == Root);

    Result = Fixture.Set(Asset, Local(UBorder::StaticClass(), TEXT("LocalRoot"), true));
    if (!TestTrue(*Result.Message, Result.bSuccess)) return false;
    TestTrue(TEXT("Replacement can reuse the removed root name"), Asset->WidgetTree->RootWidget->IsA<UBorder>());
    TestTrue(TEXT("Undo replacement succeeds"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores previous root"), Asset->WidgetTree->RootWidget == Root);
    return true;
}

#endif
