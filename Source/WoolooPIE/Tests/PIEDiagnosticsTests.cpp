#if WITH_DEV_AUTOMATION_TESTS && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

#include "WoolooPIE/Tools/PIEToolset.h"
#include "WoolooPIE/Session/PIESessionService.h"

#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Script.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace
{
// Explicit tests create only their own transient worlds; they never launch PIE, tick gameplay or save packages.
struct FWorldFixture
{
    TStrongObjectPtr<UWorld> World;
    FName ContextHandle;

    FWorldFixture()
    {
        const auto Name = FName(*(TEXT("WoolooPIETest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        const auto Options = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false)
            .RequiresHitProxies(false).CreateFXSystem(false);
        World.Reset(UWorld::CreateWorld(EWorldType::PIE, false, Name, nullptr, false, ERHIFeatureLevel::Num, &Options));
        World->SetFlags(RF_Transient);
        World->GetOutermost()->SetFlags(RF_Transient);
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::PIE);
        Context.SetCurrentWorld(World.Get());
        ContextHandle = Context.ContextHandle;
    }

    ~FWorldFixture()
    {
        if (World.IsValid())
        {
            GEngine->DestroyWorldContext(World.Get());
            World->DestroyWorld(false);
            World->GetOutermost()->SetDirtyFlag(false);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooPIEWorldIsolationTest, "Wooloo.PIE.WorldIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooPIEWorldIsolationTest::RunTest(const FString& Parameters)
{
    if (!GEngine || !GEditor) return false;
    FName ExpiredHandle;
    {
        FWorldFixture First;
        FWorldFixture Second;
        TestTrue(TEXT("Exact first world"), Wooloo::PIE::Private::ResolveWorld(First.ContextHandle) == First.World.Get());
        TestTrue(TEXT("Exact second world"), Wooloo::PIE::Private::ResolveWorld(Second.ContextHandle) == Second.World.Get());

        auto* Controller = First.World->SpawnActor<APlayerController>();
        auto* Pawn = First.World->SpawnActor<APawn>();
        auto* OtherPawn = Second.World->SpawnActor<APawn>();
        if (!TestNotNull(TEXT("Controller fixture"), Controller) || !TestNotNull(TEXT("Pawn fixture"), Pawn)
            || !TestNotNull(TEXT("Other world pawn"), OtherPawn)) return false;
        FWoolooPIEPlayerTarget Target;
        Target.WorldContextHandle = First.ContextHandle;
        Target.LocalPlayerIndex = 0;
        Target.ExpectedControllerPath = Controller->GetPathName();
        Target.ExpectedPawnPath = Pawn->GetPathName();
        FString Error;
        TestTrue(TEXT("Matching actors"), Wooloo::PIE::Private::ValidateIdentity(Target, First.World.Get(), Controller, Pawn, Error));
        Target.ExpectedPawnPath = OtherPawn->GetPathName();
        TestFalse(TEXT("Cross-world pawn refused even with matching path"),
            Wooloo::PIE::Private::ValidateIdentity(Target, First.World.Get(), Controller, OtherPawn, Error));
        TestFalse(TEXT("Replaced pawn refused"),
            Wooloo::PIE::Private::ValidateIdentity(Target, First.World.Get(), Controller, Pawn, Error));

        const auto Snapshot = UWoolooPIEToolset::DescribeSession();
        TSharedPtr<FJsonObject> Json;
        TestTrue(TEXT("Snapshot succeeds"), Snapshot.bSuccess);
        if (TestTrue(TEXT("Snapshot is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot.Json), Json)))
        {
            TSet<FString> Handles;
            for (const auto& Entry : Json->GetArrayField(TEXT("Worlds")))
                Handles.Add(Entry->AsObject()->GetStringField(TEXT("WorldContextHandle")));
            TestTrue(TEXT("Snapshot contains first context"), Handles.Contains(First.ContextHandle.ToString()));
            TestTrue(TEXT("Snapshot contains second context"), Handles.Contains(Second.ContextHandle.ToString()));
        }
        ExpiredHandle = First.ContextHandle;
    }
    TestNull(TEXT("Destroyed session cannot be retargeted"), Wooloo::PIE::Private::ResolveWorld(ExpiredHandle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooPIEInputValidationTest, "Wooloo.PIE.InputValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooPIEInputValidationTest::RunTest(const FString& Parameters)
{
    FString Error;
    using namespace Wooloo::PIE::Private;
    TestTrue(TEXT("Boolean press"), ValidateValue(EInputActionValueType::Boolean, FVector(1, 0, 0), Error));
    TestTrue(TEXT("Boolean release"), ValidateValue(EInputActionValueType::Boolean, FVector::ZeroVector, Error));
    TestFalse(TEXT("Boolean does not silently coerce"), ValidateValue(EInputActionValueType::Boolean, FVector(2, 0, 0), Error));
    TestFalse(TEXT("Axis1D does not truncate"), ValidateValue(EInputActionValueType::Axis1D, FVector(1, 1, 0), Error));
    TestTrue(TEXT("Axis2D"), ValidateValue(EInputActionValueType::Axis2D, FVector(-1, 0.5, 0), Error));
    TestFalse(TEXT("Axis2D does not truncate"), ValidateValue(EInputActionValueType::Axis2D, FVector(0, 0, 1), Error));
    TestTrue(TEXT("Axis3D"), ValidateValue(EInputActionValueType::Axis3D, FVector(1, 2, 3), Error));
    TestFalse(TEXT("NaN refused"), ValidateValue(EInputActionValueType::Axis3D,
        FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), Error));

    TStrongObjectPtr<UInputAction> Action(NewObject<UInputAction>());
    TStrongObjectPtr<UEnhancedInputComponent> Input(NewObject<UEnhancedInputComponent>());
    Input->BindActionValue(Action.Get());
    TArray<TWeakObjectPtr<const UInputComponent>> Stack;
    Stack.Add(Input.Get());
    TestTrue(TEXT("Exact bound action found"), FindBoundAction(Stack, Action->GetPathName(), Error) == Action.Get());
    TestNull(TEXT("Short names never match"), FindBoundAction(Stack, Action->GetName(), Error));
    Input->SetShouldFireDelegatesInEditor(true);
    TestNull(TEXT("Editor-callable binding refused"), FindBoundAction(Stack, Action->GetPathName(), Error));
    Stack.Reset();
    TestNull(TEXT("Popped input component is not eligible"), FindBoundAction(Stack, Action->GetPathName(), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooPIEScriptGuardTest, "Wooloo.PIE.ScriptGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooPIEScriptGuardTest::RunTest(const FString& Parameters)
{
    const bool bOriginalGuard = GAllowActorScriptExecutionInEditor;
    {
        FEditorScriptExecutionGuard Guard;
        const auto Result = UWoolooPIEToolset::InjectInputAction({}, TEXT("/Game/Unused/Action"), FVector(1, 0, 0));
        TestFalse(TEXT("Python/editor script bridge refused before any target or action access"), Result.bSuccess);
        TestTrue(TEXT("Failure identifies script guard"), Result.Message.Contains(TEXT("guard is active")));
        TestTrue(TEXT("Tool leaves guard intact"), GAllowActorScriptExecutionInEditor);
    }
    TestEqual(TEXT("Guard lifetime unchanged"), GAllowActorScriptExecutionInEditor, bOriginalGuard);
    return true;
}
#endif
