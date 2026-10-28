#if WITH_DEV_AUTOMATION_TESTS && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

#include "WoolooCamera/Tools/CameraToolset.h"

#include "Core/CameraRigAsset.h"
#include "Core/ObjectTreeGraphComment.h"
#include "Core/ObjectTreeGraphRootObject.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Nodes/Common/ArrayCameraNode.h"
#include "Nodes/Common/ClippingPlanesCameraNode.h"
#include "Nodes/Common/FieldOfViewCameraNode.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
struct FRigFixture
{
    FString Root = TEXT("/Game/WoolooCameraTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UCameraRigAsset> Rig;

    FRigFixture()
        : Rig(NewObject<UCameraRigAsset>(CreatePackage(*(Root + TEXT("/Rig"))), TEXT("Rig"), RF_Transactional))
    {
    }

    template<typename T>
    T* Add(const TCHAR* Name)
    {
        auto* Object = NewObject<T>(Rig.Get(), Name, RF_Transactional);
        static_cast<IObjectTreeGraphRootObject*>(Rig.Get())->AddConnectableObject(UCameraRigAsset::NodeTreeGraphName, Object);
        return Object;
    }

    TSet<UObject*> Registered() const
    {
        TSet<UObject*> Objects;
        static_cast<IObjectTreeGraphRootObject*>(Rig.Get())->GetConnectableObjects(UCameraRigAsset::NodeTreeGraphName, Objects);
        return Objects;
    }

    void Clean() const
    {
        Rig->BuildStatus = ECameraBuildStatus::Clean;
        Rig->GetOutermost()->SetDirtyFlag(false);
    }

    TSharedPtr<FJsonObject> Replace(UCameraNode* Node, UClass* Class = UClippingPlanesCameraNode::StaticClass(),
        const TMap<FString, FString>& Properties = {}, const FString& Comment = TEXT("Replacement")) const
    {
        return Invoke(Node->GetPathName(Rig.Get()), Class->GetPathName(), Properties, Root, Comment);
    }

    TSharedPtr<FJsonObject> Invoke(const FString& NodePath, const FString& ClassPath,
        const TMap<FString, FString>& Properties, const FString& AllowedRoot,
        const FString& Comment = TEXT("Replacement")) const
    {
        const auto Json = UWoolooCameraToolset::ReplaceCameraRigNode(Rig->GetOutermost()->GetName(),
            AllowedRoot, NodePath, ClassPath, Properties, Comment);
        TSharedPtr<FJsonObject> Data;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data);
        return Data;
    }

    ~FRigFixture()
    {
        // Keep the user's Undo history intact. Only our unique unsaved package is made transient.
        Rig->ClearFlags(RF_Public | RF_Standalone);
        Rig->SetFlags(RF_Transient);
        Rig->GetOutermost()->SetFlags(RF_Transient);
        Rig->GetOutermost()->SetDirtyFlag(false);
    }
};

bool Succeeded(const TSharedPtr<FJsonObject>& Data)
{
    bool bSuccess = false;
    return Data.IsValid() && Data->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooCameraLeafReplacementTest,
    "Wooloo.Camera.ReplaceRigNode.PreserveExecutionLayoutAndUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooCameraLeafReplacementTest::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("An idle editor transaction buffer is required"),
        GEditor && GEditor->Trans && !GEditor->PlayWorld && !GEditor->IsTransactionActive())) return false;
    FRigFixture Fixture;
    auto* Sequence = Fixture.Add<UArrayCameraNode>(TEXT("Sequence"));
    auto* Before = Fixture.Add<UFieldOfViewCameraNode>(TEXT("Before"));
    auto* Old = Fixture.Add<UFieldOfViewCameraNode>(TEXT("Old"));
    auto* After = Fixture.Add<UFieldOfViewCameraNode>(TEXT("After"));
    auto* Comment = Fixture.Add<UObjectTreeGraphComment>(TEXT("Comment"));
    Comment->CommentText = TEXT("Existing group");
    Comment->GraphNodePos = FIntVector2(64, 128);
    Comment->GraphNodeSize = FIntVector2(800, 400);
    Fixture.Rig->RootNode = Sequence;
    Sequence->Children = { Before, Old, nullptr, After, Old };
    Old->bIsEnabled = false;
    Old->GraphNodePos = FIntVector2(256, 192);
    Old->GraphNodeComment = TEXT("Original");
    auto& UnrelatedConnection = Fixture.Rig->Connections.Connections.AddDefaulted_GetRef();
    UnrelatedConnection.Source = Before;
    UnrelatedConnection.Target = After;
    UnrelatedConnection.SourcePropertyName = TEXT("FieldOfView");
    UnrelatedConnection.TargetPropertyName = TEXT("FieldOfView");
    Fixture.Clean();

    const auto Data = Fixture.Replace(Old, UClippingPlanesCameraNode::StaticClass(),
        {{TEXT("NearPlane"), TEXT("(Value=12.5)")}}, TEXT("Camera clipping"));
    if (!TestTrue(TEXT("Leaf replacement succeeds"), Succeeded(Data))) return false;
    auto* NewNode = Cast<UClippingPlanesCameraNode>(Sequence->Children[1]);
    if (!TestNotNull(TEXT("Execution slot contains the new concrete class"), NewNode)) return false;
    TestEqual(TEXT("Array length is preserved"), Sequence->Children.Num(), 5);
    TestTrue(TEXT("Other slots, null slot and duplicate incoming links retain their order"),
        Sequence->Children[0] == Before && !Sequence->Children[2]
        && Sequence->Children[3] == After && Sequence->Children[4] == NewNode);
    TestTrue(TEXT("Root is unchanged"), Fixture.Rig->RootNode == Sequence);
    TestFalse(TEXT("Disabled state is preserved"), NewNode->bIsEnabled);
    TestTrue(TEXT("Position is preserved"), NewNode->GraphNodePos == Old->GraphNodePos);
    TestEqual(TEXT("Requested node comment is used"), NewNode->GraphNodeComment, FString(TEXT("Camera clipping")));
    TestEqual(TEXT("Explicit native property import is applied"), NewNode->NearPlane.Value, 12.5);
    TestEqual(TEXT("Unspecified properties use new-class defaults"), NewNode->FarPlane.Value,
        GetDefault<UClippingPlanesCameraNode>()->FarPlane.Value);
    TestEqual(TEXT("Old UObject remains intact for Undo"), Old->GraphNodeComment, FString(TEXT("Original")));
    TestTrue(TEXT("Native registration replaces the old node"),
        Fixture.Registered().Contains(NewNode) && !Fixture.Registered().Contains(Old));
    TestTrue(TEXT("Existing comment box is preserved"), Fixture.Registered().Contains(Comment)
        && Comment->CommentText == TEXT("Existing group") && Comment->GraphNodePos == FIntVector2(64, 128)
        && Comment->GraphNodeSize == FIntVector2(800, 400));
    TestEqual(TEXT("Unrelated data connections remain"), Fixture.Rig->Connections.Connections.Num(), 1);
    TestTrue(TEXT("Unrelated connection endpoints remain"), Fixture.Rig->Connections.Connections[0].Source == Before
        && Fixture.Rig->Connections.Connections[0].Target == After);
    TestTrue(TEXT("Rig needs a native build"), Fixture.Rig->BuildStatus == ECameraBuildStatus::Dirty);
    TestTrue(TEXT("Package is dirty"), Fixture.Rig->GetOutermost()->IsDirty() && Data->GetBoolField(TEXT("dirty")));
    TestEqual(TEXT("Result identifies the old relative path"), Data->GetStringField(TEXT("old_node_path")), Old->GetPathName(Fixture.Rig.Get()));
    TestEqual(TEXT("Result identifies the new relative path"), Data->GetStringField(TEXT("new_node_path")), NewNode->GetPathName(Fixture.Rig.Get()));
    TestFalse(TEXT("No asset is saved"), FPackageName::DoesPackageExist(Fixture.Rig->GetOutermost()->GetName()));

    if (!TestTrue(TEXT("One Undo succeeds"), GEditor->UndoTransaction())) return false;
    TestTrue(TEXT("Undo restores all old execution slots"), Sequence->Children[1] == Old && Sequence->Children[4] == Old);
    TestTrue(TEXT("Undo restores native registration"), Fixture.Registered().Contains(Old) && !Fixture.Registered().Contains(NewNode));
    if (!TestTrue(TEXT("One Redo succeeds"), GEditor->RedoTransaction())) return false;
    TestTrue(TEXT("Redo restores new execution slots"), Sequence->Children[1] == NewNode && Sequence->Children[4] == NewNode);
    TestTrue(TEXT("Redo restores native registration"), Fixture.Registered().Contains(NewNode) && !Fixture.Registered().Contains(Old));
    TestEqual(TEXT("Redo retains configured values"), NewNode->NearPlane.Value, 12.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooCameraRootReplacementTest,
    "Wooloo.Camera.ReplaceRigNode.RootAndDisconnectedLeaf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooCameraRootReplacementTest::RunTest(const FString& Parameters)
{
    FRigFixture Fixture;
    auto* Old = Fixture.Add<UFieldOfViewCameraNode>(TEXT("Root"));
    Fixture.Rig->RootNode = Old;
    Old->FieldOfView.Value = 123.f;
    Fixture.Clean();
    const auto Data = Fixture.Replace(Old, UFieldOfViewCameraNode::StaticClass(), {}, TEXT(""));
    if (!TestTrue(TEXT("A leaf root can be replaced"), Succeeded(Data))) return false;
    auto* NewNode = Cast<UFieldOfViewCameraNode>(Fixture.Rig->RootNode);
    if (!TestNotNull(TEXT("Root has the requested type"), NewNode)) return false;
    TestTrue(TEXT("Same-class replacement still creates a fresh node"), NewNode != Old);
    TestEqual(TEXT("Old values are not implicitly copied"), NewNode->FieldOfView.Value,
        GetDefault<UFieldOfViewCameraNode>()->FieldOfView.Value);
    TestTrue(TEXT("An empty NodeComment clears the comment"), NewNode->GraphNodeComment.IsEmpty());

    auto* Disconnected = Fixture.Add<UFieldOfViewCameraNode>(TEXT("Disconnected"));
    Fixture.Clean();
    const auto LooseData = Fixture.Replace(Disconnected);
    TestTrue(TEXT("A registered disconnected leaf can be replaced"), Succeeded(LooseData));
    TestTrue(TEXT("Replacing a disconnected leaf preserves the root"), Fixture.Rig->RootNode == NewNode);

    FRigFixture NestedFixture;
    auto* Parent = NestedFixture.Add<UArrayCameraNode>(TEXT("Parent"));
    auto* Nested = NewObject<UFieldOfViewCameraNode>(Parent, TEXT("Nested"), RF_Transactional);
    static_cast<IObjectTreeGraphRootObject*>(NestedFixture.Rig.Get())->AddConnectableObject(UCameraRigAsset::NodeTreeGraphName, Nested);
    Parent->Children = { Nested };
    NestedFixture.Rig->RootNode = Parent;
    NestedFixture.Clean();
    if (!TestTrue(TEXT("A nested Rig-relative path resolves exactly"), Succeeded(NestedFixture.Replace(Nested)))) return false;
    TestTrue(TEXT("Replacement preserves the original outer"), Parent->Children[0]->GetOuter() == Parent);
    TestTrue(TEXT("Nested execution slot contains the new class"), Parent->Children[0]->IsA<UClippingPlanesCameraNode>());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooCameraReplacementValidationTest,
    "Wooloo.Camera.ReplaceRigNode.RefusalDoesNotMutateRig",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooCameraReplacementValidationTest::RunTest(const FString& Parameters)
{
    FRigFixture Fixture;
    auto* Sequence = Fixture.Add<UArrayCameraNode>(TEXT("Sequence"));
    auto* Old = Fixture.Add<UFieldOfViewCameraNode>(TEXT("Old"));
    Sequence->Children = { Old };
    Fixture.Rig->RootNode = Sequence;
    Fixture.Clean();
    const auto OldPath = Old->GetPathName(Fixture.Rig.Get());
    const auto ClassPath = UClippingPlanesCameraNode::StaticClass()->GetPathName();
    TArray<UObject*> OriginalObjects;
    GetObjectsWithOuter(Fixture.Rig.Get(), OriginalObjects, EGetObjectsFlags::IncludeNestedObjects);

    const auto Refused = [&](const TCHAR* Label, const FString& NodePath, const FString& NewClass,
        const TMap<FString, FString>& Properties, const FString& Root)
    {
        const auto Data = Fixture.Invoke(NodePath, NewClass, Properties, Root);
        if (!TestTrue(Label, Data.IsValid())) return;
        TestFalse(Label, Succeeded(Data));
        TestTrue(TEXT("Failure does not claim a new node"), Data->GetStringField(TEXT("new_node_path")).IsEmpty());
        TestTrue(TEXT("Failure explains its reason"), !Data->GetStringField(TEXT("message")).IsEmpty());
        TestTrue(TEXT("Failure leaves root and child intact"), Fixture.Rig->RootNode == Sequence && Sequence->Children[0] == Old);
        TestTrue(TEXT("Failure preserves registration"), Fixture.Registered().Contains(Old) && Fixture.Registered().Num() == 2);
        TestFalse(TEXT("Failure does not dirty the Rig"), Fixture.Rig->GetOutermost()->IsDirty());
        TestTrue(TEXT("Failure does not change build status"), Fixture.Rig->BuildStatus == ECameraBuildStatus::Clean);
        TArray<UObject*> CurrentObjects;
        GetObjectsWithOuter(Fixture.Rig.Get(), CurrentObjects, EGetObjectsFlags::IncludeNestedObjects);
        TestEqual(TEXT("Failure creates no Rig-owned objects"), CurrentObjects.Num(), OriginalObjects.Num());
    };
    Refused(TEXT("Entire content mount is too broad"), OldPath, ClassPath, {}, TEXT("/Game"));
    Refused(TEXT("A matching string prefix is not a directory boundary"), OldPath, ClassPath, {}, Fixture.Root.LeftChop(1));
    Refused(TEXT("Outside-scope Rig is refused"), OldPath, ClassPath, {}, Fixture.Root + TEXT("/Elsewhere"));
    Refused(TEXT("Non-leaf target is refused"), Sequence->GetPathName(Fixture.Rig.Get()), ClassPath, {}, Fixture.Root);
    Refused(TEXT("Absolute node path is not accepted"), Old->GetPathName(), ClassPath, {}, Fixture.Root);
    Refused(TEXT("Unknown node is refused"), TEXT("Missing"), ClassPath, {}, Fixture.Root);
    Refused(TEXT("Abstract node class is refused"), OldPath, UCameraNode::StaticClass()->GetPathName(), {}, Fixture.Root);
    Refused(TEXT("Non-node class is refused"), OldPath, UObject::StaticClass()->GetPathName(), {}, Fixture.Root);
    Refused(TEXT("Unknown property is refused"), OldPath, ClassPath, {{TEXT("Missing"), TEXT("1")}}, Fixture.Root);
    Refused(TEXT("Enabled state cannot be overridden"), OldPath, ClassPath, {{TEXT("bIsEnabled"), TEXT("True")}}, Fixture.Root);
    Refused(TEXT("Graph position cannot be imported"), OldPath, ClassPath, {{TEXT("GraphNodePos"), TEXT("(X=0,Y=0)")}}, Fixture.Root);
    Refused(TEXT("Child ownership cannot be imported"), OldPath, UArrayCameraNode::StaticClass()->GetPathName(),
        {{TEXT("Children"), TEXT("()")}}, Fixture.Root);
    Refused(TEXT("Trailing import text is refused"), OldPath, ClassPath, {{TEXT("NearPlane"), TEXT("(Value=5) trailing")}}, Fixture.Root);
    Refused(TEXT("Malformed value is refused"), OldPath, ClassPath, {{TEXT("NearPlane"), TEXT("not a struct")}}, Fixture.Root);
    Refused(TEXT("Case aliases cannot import one field twice"), OldPath, ClassPath,
        {{TEXT("NearPlane"), TEXT("(Value=5)")}, {TEXT("nearplane"), TEXT("(Value=6)")}}, Fixture.Root);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooCameraBoundNodeReplacementTest,
    "Wooloo.Camera.ReplaceRigNode.RefuseBindingsAndUnsupportedReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooCameraBoundNodeReplacementTest::RunTest(const FString& Parameters)
{
    FRigFixture Fixture;
    auto* Old = Fixture.Add<UFieldOfViewCameraNode>(TEXT("Old"));
    Fixture.Rig->RootNode = Old;
    auto* Parameter = NewObject<UCameraObjectInterfaceBlendableParameter>(Fixture.Rig.Get(), TEXT("Parameter"), RF_Transactional);
    Fixture.Rig->Interface.BlendableParameters.Add(Parameter);
    auto& Binding = Fixture.Rig->Connections.Connections.AddDefaulted_GetRef();
    Binding.Source = Parameter;
    Binding.Target = Old;
    Binding.TargetPropertyName = TEXT("FieldOfView");
    Fixture.Clean();
    TestFalse(TEXT("Exposed parameter binding prevents replacement"), Succeeded(Fixture.Replace(Old)));
    TestTrue(TEXT("Binding target is unchanged"), Binding.Target == Old);
    Binding.Source = Old;
    Binding.Target = Parameter;
    TestFalse(TEXT("Outgoing data connection also prevents replacement"), Succeeded(Fixture.Replace(Old)));
    TestFalse(TEXT("Binding refusals do not dirty the package"), Fixture.Rig->GetOutermost()->IsDirty());
    Fixture.Rig->Connections.Connections.Reset();

    // A second graph's ownership is not an execution slot and must never be silently remapped.
    auto* Graph = static_cast<IObjectTreeGraphRootObject*>(Fixture.Rig.Get());
    Graph->AddConnectableObject(UCameraRigAsset::TransitionsGraphName, Old);
    Fixture.Clean();
    TestFalse(TEXT("Unsupported reflected reference prevents replacement"), Succeeded(Fixture.Replace(Old)));
    TestTrue(TEXT("Root remains intact"), Fixture.Rig->RootNode == Old);
    TestFalse(TEXT("Reference refusal leaves package clean"), Fixture.Rig->GetOutermost()->IsDirty());
    return true;
}

#endif
