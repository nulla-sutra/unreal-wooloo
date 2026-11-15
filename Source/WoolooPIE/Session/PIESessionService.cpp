#include "WoolooPIE/Session/PIESessionService.h"

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "PlayInEditorDataTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace Wooloo::PIE
{
namespace
{
using FJson = TSharedPtr<FJsonObject>;

FWoolooPIEResult Failure(const FString& Message)
{
    FWoolooPIEResult Result;
    Result.Message = Message;
    return Result;
}

FWoolooPIEResult Success(const FJson& Json, const FString& Message)
{
    FWoolooPIEResult Result;
    Result.Message = Message;
    Result.bSuccess = FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Result.Json));
    return Result;
}

FString ObjectPath(const UObject* Object)
{
    return IsValid(Object) ? Object->GetPathName() : FString();
}

FString NetModeName(ENetMode Mode)
{
    switch (Mode)
    {
    case NM_Standalone: return TEXT("Standalone");
    case NM_DedicatedServer: return TEXT("DedicatedServer");
    case NM_ListenServer: return TEXT("ListenServer");
    case NM_Client: return TEXT("Client");
    default: return TEXT("Unknown");
    }
}

FJson TargetJson(const FWoolooPIEPlayerTarget& Target)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("WorldContextHandle"), Target.WorldContextHandle.ToString());
    Json->SetNumberField(TEXT("LocalPlayerIndex"), Target.LocalPlayerIndex);
    Json->SetStringField(TEXT("ExpectedControllerPath"), Target.ExpectedControllerPath);
    Json->SetStringField(TEXT("ExpectedPawnPath"), Target.ExpectedPawnPath);
    return Json;
}

UAbilitySystemComponent* FindAsc(AActor* Actor)
{
    if (!IsValid(Actor)) return nullptr;
    if (auto* Interface = Cast<IAbilitySystemInterface>(Actor))
        if (auto* Asc = Interface->GetAbilitySystemComponent(); IsValid(Asc)) return Asc;
    return Actor->FindComponentByClass<UAbilitySystemComponent>();
}

FJson DescribePlayer(const FWorldContext& Context, const UWorld* World, APlayerController* Controller)
{
    auto Json = MakeShared<FJsonObject>();
    const auto Pawn = Controller->GetPawn();
    const auto* Instance = World->GetGameInstance();
    auto* LocalPlayer = Controller->GetLocalPlayer();
    FWoolooPIEPlayerTarget Target;
    Target.WorldContextHandle = Context.ContextHandle;
    Target.LocalPlayerIndex = Instance && LocalPlayer ? Instance->GetLocalPlayers().IndexOfByKey(LocalPlayer) : INDEX_NONE;
    Target.ExpectedControllerPath = ObjectPath(Controller);
    Target.ExpectedPawnPath = ObjectPath(Pawn);
    Json->SetObjectField(TEXT("Target"), TargetJson(Target));
    Json->SetStringField(TEXT("ControllerRole"), UEnum::GetValueAsString(Controller->GetLocalRole()));
    Json->SetStringField(TEXT("PawnRole"), IsValid(Pawn) ? UEnum::GetValueAsString(Pawn->GetLocalRole()) : FString());
    Json->SetBoolField(TEXT("IsLocalController"), Controller->IsLocalController());
    Json->SetNumberField(TEXT("PlayerId"), Controller->PlayerState ? Controller->PlayerState->GetPlayerId() : INDEX_NONE);
    Json->SetBoolField(TEXT("HasEnhancedPlayerInput"), IsValid(Cast<UEnhancedPlayerInput>(Controller->PlayerInput)));
    Json->SetNumberField(TEXT("InputStackSize"), Controller->Debug_GetMostRecentInputStack().Num());

    if (IsValid(Pawn))
    {
        const auto Location = Pawn->GetActorLocation();
        auto Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("X"), Location.X);
        Position->SetNumberField(TEXT("Y"), Location.Y);
        Position->SetNumberField(TEXT("Z"), Location.Z);
        Json->SetObjectField(TEXT("Location"), Position);
    }

    auto* Asc = FindAsc(Pawn);
    if (!Asc) Asc = FindAsc(Controller->PlayerState);
    if (!Asc) Asc = FindAsc(Controller);
    if (IsValid(Asc))
    {
        auto AbilitySystem = MakeShared<FJsonObject>();
        AbilitySystem->SetStringField(TEXT("Path"), ObjectPath(Asc));
        AbilitySystem->SetStringField(TEXT("WorldPath"), ObjectPath(Asc->GetWorld()));
        AbilitySystem->SetBoolField(TEXT("MatchesPlayerWorld"), Asc->GetWorld() == World);
        AbilitySystem->SetStringField(TEXT("ScopedPredictionKey"), Asc->ScopedPredictionKey.ToString());
        if (const auto Info = Asc->AbilityActorInfo; Info.IsValid())
        {
            AbilitySystem->SetStringField(TEXT("OwnerPath"), ObjectPath(Info->OwnerActor.Get()));
            AbilitySystem->SetStringField(TEXT("AvatarPath"), ObjectPath(Info->AvatarActor.Get()));
            AbilitySystem->SetStringField(TEXT("ControllerPath"), ObjectPath(Info->PlayerController.Get()));
        }
        TArray<TSharedPtr<FJsonValue>> Abilities;
        for (const auto& Spec : static_cast<const UAbilitySystemComponent*>(Asc)->GetActivatableAbilities())
        {
            auto Ability = MakeShared<FJsonObject>();
            Ability->SetStringField(TEXT("Handle"), Spec.Handle.ToString());
            Ability->SetStringField(TEXT("ClassPath"), IsValid(Spec.Ability) ? Spec.Ability->GetClass()->GetPathName() : FString());
            Ability->SetBoolField(TEXT("IsActive"), Spec.IsActive());
            Abilities.Add(MakeShared<FJsonValueObject>(Ability));
        }
        AbilitySystem->SetArrayField(TEXT("Abilities"), Abilities);
        Json->SetObjectField(TEXT("AbilitySystem"), AbilitySystem);
    }

    auto* Mesh = IsValid(Pawn) ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
    if (IsValid(Asc) && Asc->AbilityActorInfo.IsValid() && Asc->AbilityActorInfo->SkeletalMeshComponent.IsValid())
        Mesh = Asc->AbilityActorInfo->SkeletalMeshComponent.Get();
    if (IsValid(Mesh))
    {
        Json->SetStringField(TEXT("MeshPath"), ObjectPath(Mesh));
        if (auto* Anim = Mesh->GetAnimInstance())
        {
            auto* Montage = Anim->GetCurrentActiveMontage();
            Json->SetStringField(TEXT("MontagePath"), ObjectPath(Montage));
            if (Montage) Json->SetNumberField(TEXT("MontagePosition"), Anim->Montage_GetPosition(Montage));
        }
    }
    return Json;
}
}

namespace Private
{
UWorld* ResolveWorld(FName ContextHandle)
{
    if (!GEngine || ContextHandle.IsNone()) return nullptr;
    const auto* Context = GEngine->GetWorldContextFromHandle(ContextHandle);
    return Context && Context->WorldType == EWorldType::PIE && IsValid(Context->World()) ? Context->World() : nullptr;
}

bool ValidateIdentity(const FWoolooPIEPlayerTarget& Target, const UWorld* World,
    const APlayerController* Controller, const APawn* Pawn, FString& Error)
{
    if (!IsValid(World) || !IsValid(Controller) || !IsValid(Pawn)
        || Controller->GetWorld() != World || Pawn->GetWorld() != World
        || Target.ExpectedControllerPath.IsEmpty() || Target.ExpectedPawnPath.IsEmpty()
        || Controller->GetPathName() != Target.ExpectedControllerPath || Pawn->GetPathName() != Target.ExpectedPawnPath)
    {
        Error = TEXT("Controller/Pawn identity changed or belongs to another world; call DescribeSession again.");
        return false;
    }
    return true;
}

bool ValidateValue(EInputActionValueType Type, const FVector& Value, FString& Error)
{
    if (!FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y) || !FMath::IsFinite(Value.Z))
    {
        Error = TEXT("Input value must be finite.");
        return false;
    }
    switch (Type)
    {
    case EInputActionValueType::Boolean:
        if ((Value.X == 0.0 || Value.X == 1.0) && Value.Y == 0.0 && Value.Z == 0.0) return true;
        break;
    case EInputActionValueType::Axis1D:
        if (Value.Y == 0.0 && Value.Z == 0.0) return true;
        break;
    case EInputActionValueType::Axis2D:
        if (Value.Z == 0.0) return true;
        break;
    case EInputActionValueType::Axis3D: return true;
    default: break;
    }
    Error = TEXT("Input value does not match the action type; unused components must be zero and Boolean X must be 0 or 1.");
    return false;
}

const UInputAction* FindBoundAction(TConstArrayView<TWeakObjectPtr<const UInputComponent>> Stack,
    const FString& ObjectPath, FString& Error)
{
    const UInputAction* Found = nullptr;
    for (const auto& Entry : Stack)
    {
        const auto* Component = Cast<UEnhancedInputComponent>(Entry.Get());
        if (!IsValid(Component)) continue;
        const UInputAction* BoundAction = nullptr;
        for (const auto& Binding : Component->GetActionEventBindings())
            if (const auto* Action = Binding->GetAction(); Action && Action->GetPathName() == ObjectPath) BoundAction = Action;
        for (const auto& Binding : Component->GetActionValueBindings())
            if (const auto* Action = Binding.GetAction(); Action && Action->GetPathName() == ObjectPath) BoundAction = Action;
        if (!BoundAction) continue;

        // Editor-callable input delegates would re-enable the same RPC-localizing guard during input processing.
        if (Component->ShouldFireDelegatesInEditor())
        {
            Error = TEXT("An action binding enables editor script execution; native network diagnostics require it disabled.");
            return nullptr;
        }
        Found = BoundAction;
    }
    if (!Found) Error = TEXT("Action is not bound in this player's most recently processed input stack; wait for normal input processing or inspect the bindings.");
    return Found;
}
}

FWoolooPIEResult StartSession(const FString& ExpectedMapPackage, EWoolooPIESessionMode Mode, int32 NumPlayers)
{
    if (!IsInGameThread()) return Failure(TEXT("PIE startup must run on the game thread."));
    if (!GEditor) return Failure(TEXT("Editor engine is not available."));
    if (GEditor->IsPlaySessionInProgress()) return Failure(TEXT("A play session is active or already queued."));
    if (GEditor->IsTransactionActive()) return Failure(TEXT("Finish the active editor transaction before starting PIE."));
    if (!FPackageName::IsValidLongPackageName(ExpectedMapPackage, true))
        return Failure(TEXT("ExpectedMapPackage must be a long map package name."));

    const auto bDedicatedServer = Mode == EWoolooPIESessionMode::DedicatedServer;
    if (!bDedicatedServer && Mode != EWoolooPIESessionMode::Standalone)
        return Failure(TEXT("Mode must be Standalone or DedicatedServer."));
    if (NumPlayers < 1 || (!bDedicatedServer && NumPlayers != 1))
        return Failure(TEXT("Standalone requires one player. DedicatedServer requires at least one client."));
    if (bDedicatedServer && !GEditor->bAllowMultiplePIEWorlds)
        return Failure(TEXT("The editor does not allow multiple in-process PIE worlds."));

    const UWorld* EditorWorld = nullptr;
    for (const auto& Context : GEditor->GetWorldContexts())
    {
        if (Context.WorldType == EWorldType::PIE)
            return Failure(TEXT("A PIE world is still present. Wait for teardown before starting another session."));
        if (Context.WorldType == EWorldType::Editor) EditorWorld = Context.World();
    }
    if (!IsValid(EditorWorld)) return Failure(TEXT("No editor map is open."));
    const auto MapPackage = EditorWorld->GetPackage()->GetName();
    if (MapPackage != ExpectedMapPackage)
        return Failure(FString::Printf(TEXT("Current editor map is '%s'. Open the expected map before starting PIE."), *MapPackage));

    // PIE can compile stale Blueprints or open a modal for compile errors after the request returns.
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        const auto* Blueprint = *It;
        if (!IsValid(Blueprint) || Blueprint->GetPackage()->HasAnyPackageFlags(PKG_ForDiffing)) continue;
        if (Blueprint->Status == BS_Error
            || (Blueprint->Status == BS_Dirty && !FBlueprintEditorUtils::IsDataOnlyBlueprint(Blueprint)))
            return Failure(FString::Printf(TEXT("Resolve the Blueprint compile status before starting PIE: %s"), *Blueprint->GetPathName()));
    }

    // Supply a transient settings copy so the requested topology never changes editor defaults.
    auto* Settings = DuplicateObject(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
    Settings->SetRunUnderOneProcess(true);
    Settings->SetPlayNetMode(bDedicatedServer ? EPlayNetMode::PIE_Client : EPlayNetMode::PIE_Standalone);
    Settings->bLaunchSeparateServer = false;
    Settings->SetPlayNumberOfClients(NumPlayers);
    int32 ConfiguredPlayers = 0;
    Settings->GetPlayNumberOfClients(ConfiguredPlayers);
    if (ConfiguredPlayers != NumPlayers)
        return Failure(FString::Printf(TEXT("NumPlayers exceeds the editor's PIE limit of %d."), ConfiguredPlayers));

    auto Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("Queued"), true);
    Json->SetStringField(TEXT("MapPackage"), MapPackage);
    Json->SetStringField(TEXT("Mode"), NetModeName(bDedicatedServer ? NM_DedicatedServer : NM_Standalone));
    Json->SetNumberField(TEXT("NumPlayers"), NumPlayers);
    auto Result = Success(Json, TEXT("PIE request queued. Confirm worlds and players with DescribeSession."));
    if (Result.bSuccess)
    {
        FRequestPlaySessionParams Params;
        Params.EditorPlaySettings = Settings;
        Params.bAllowOnlineSubsystem = false;
        GEditor->RequestPlaySession(Params);
    }
    return Result;
}

FWoolooPIEResult DescribeSession()
{
    if (!IsInGameThread()) return Failure(TEXT("PIE inspection must run on the game thread."));
    if (!GEngine || !GEditor) return Failure(TEXT("Editor engine is not available."));
    TArray<TSharedPtr<FJsonValue>> Worlds;
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        auto* World = Context.World();
        if (Context.WorldType != EWorldType::PIE || !IsValid(World)) continue;
        auto Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("WorldContextHandle"), Context.ContextHandle.ToString());
        Json->SetNumberField(TEXT("PIEInstance"), Context.PIEInstance);
        Json->SetStringField(TEXT("WorldPath"), World->GetPathName());
        Json->SetStringField(TEXT("NetMode"), NetModeName(World->GetNetMode()));
        Json->SetNumberField(TEXT("TimeSeconds"), World->GetTimeSeconds());
        Json->SetBoolField(TEXT("Paused"), World->IsPaused() || World->bDebugPauseExecution);
        Json->SetBoolField(TEXT("TearingDown"), World->bIsTearingDown);
        TArray<TSharedPtr<FJsonValue>> Players;
        for (auto It = World->GetPlayerControllerIterator(); It; ++It)
            if (auto* Controller = It->Get(); IsValid(Controller))
                Players.Add(MakeShared<FJsonValueObject>(DescribePlayer(Context, World, Controller)));
        Json->SetArrayField(TEXT("Players"), Players);
        Worlds.Add(MakeShared<FJsonValueObject>(Json));
    }
    auto Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("Worlds"), Worlds);
    return Success(Root, TEXT("Read-only PIE snapshot; no input, ability activation or game tick was requested."));
}

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogWoolooPIEInput, Log, All);

FWoolooPIEResult ResolveInputPlayer(const FWoolooPIEPlayerTarget& Target, APlayerController*& Controller,
    UEnhancedInputLocalPlayerSubsystem*& Subsystem)
{
    if (!IsInGameThread()) return Failure(TEXT("PIE input must run on the game thread."));
    // Never toggle the global: callers must enter through native MCP, outside the Python bridge.
    if (GAllowActorScriptExecutionInEditor)
        return Failure(TEXT("Editor script execution guard is active. Call this tool through native MCP, not Python or an editor-script bridge."));
    auto* World = Private::ResolveWorld(Target.WorldContextHandle);
    if (!World) return Failure(TEXT("PIE context is missing or stale; call DescribeSession again."));
    if (World->bIsTearingDown || World->IsBeingCleanedUp() || World->IsPaused() || World->bDebugPauseExecution)
        return Failure(TEXT("Target PIE world is paused or tearing down."));
    auto* Instance = World->GetGameInstance();
    if (!IsValid(Instance) || Target.LocalPlayerIndex < 0)
        return Failure(TEXT("Target has no valid game instance/local player index."));
    auto* Player = Instance->GetLocalPlayerByIndex(Target.LocalPlayerIndex);
    Controller = IsValid(Player) ? Player->GetPlayerController(World) : nullptr;
    const auto Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
    FString Error;
    if (!Private::ValidateIdentity(Target, World, Controller, Pawn, Error)) return Failure(Error);
    if (!Controller->IsLocalController() || Controller->GetLocalPlayer() != Player || Player->GetWorld() != World)
        return Failure(TEXT("Target is not a local player in the selected PIE world."));
    Subsystem = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    if (!IsValid(Subsystem) || !IsValid(Subsystem->GetPlayerInput()))
        return Failure(TEXT("Target has no active Enhanced Input local-player subsystem/player input."));

    FWoolooPIEResult Result;
    Result.bSuccess = true;
    return Result;
}

FWoolooPIEResult ProcessInput(const FWoolooPIEPlayerTarget& Target, const FWoolooPIEInputStep& Step, bool bSubmit)
{
    APlayerController* Controller = nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
    auto Result = ResolveInputPlayer(Target, Controller, Subsystem);
    if (!Result.bSuccess) return Result;
    const auto& Value = Step.Value;
    FString Error;
    // Match only already-bound actions; do not load assets or pick the first registry name match.
    FString Path = Step.InputActionPath;
    if (FPackageName::IsValidLongPackageName(Path, true)) Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    const FSoftObjectPath ActionPath(Path);
    if (!ActionPath.IsValid() || !ActionPath.GetSubPathUtf8String().IsEmpty()
        || !FPackageName::IsValidLongPackageName(ActionPath.GetLongPackageName(), true))
        return Failure(TEXT("InputActionPath must be a full asset package or object path."));
    const auto* Action = Private::FindBoundAction(Controller->Debug_GetMostRecentInputStack(), Path, Error);
    if (!Action || !Private::ValidateValue(Action->ValueType, Value, Error)) return Failure(Error);

    if (!bSubmit) return Result;
    Subsystem->InjectInputForAction(Action, FInputActionValue(Action->ValueType, Value), {}, {});
    auto Json = MakeShared<FJsonObject>();
    Json->SetObjectField(TEXT("Target"), TargetJson(Target));
    Json->SetStringField(TEXT("InputActionPath"), Action->GetPathName());
    Json->SetBoolField(TEXT("Submitted"), true);
    Json->SetNumberField(TEXT("Frames"), 1);
    return Success(Json, TEXT("One input frame submitted. Verify ability activation and server response separately."));
}

FWoolooPIEResult ProcessInput(const FWoolooPIEPlayerTarget& Target, const FWoolooPIEDebugKeyStep& Step, bool bSubmit)
{
    APlayerController* Controller = nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
    auto Result = ResolveInputPlayer(Target, Controller, Subsystem);
    if (!Result.bSuccess) return Result;
    const FInputDebugKeyBinding* Found = nullptr;
    for (const auto& Entry : Controller->Debug_GetMostRecentInputStack())
    {
        const auto* Component = Cast<UEnhancedInputComponent>(Entry.Get());
        if (!IsValid(Component)) continue;
        for (const auto& Binding : Component->GetDebugKeyBindings())
        {
            const auto& Chord = Binding->Chord;
            if (Chord.Key.GetFName() != Step.Key || Chord.bCtrl || Chord.bAlt || Chord.bShift || Chord.bCmd) continue;
            if (Component->ShouldFireDelegatesInEditor()) return Failure(TEXT("Debug Key binding enables editor script execution."));
            if (Binding->KeyEvent == IE_Pressed) Found = Binding.Get();
        }
    }
    if (!Found) return Failure(FString::Printf(TEXT("No native Debug Key press binding for %s on this player's stack."), *Step.Key.ToString()));
    if (bSubmit)
    {
        // PlayerInput retains both edges until the next input pass. A complete tap cannot leave a stuck key.
        const auto Press = FInputKeyEventArgs::CreateSimulated(Found->Chord.Key, IE_Pressed, 1.f);
        const auto Release = FInputKeyEventArgs::CreateSimulated(Found->Chord.Key, IE_Released, 0.f);
        Controller->InputKey(Press);
        Controller->InputKey(Release);
    }
    return Result;
}

template <typename StepType>
struct TInputSequence : TSharedFromThis<TInputSequence<StepType>>
{
    FWoolooPIEPlayerTarget Target;
    TArray<StepType> Steps;
    int32 Next = 0;
    double StartTime = 0.;
    FGuid Id = FGuid::NewGuid();

    void SubmitNext()
    {
        auto* World = Private::ResolveWorld(Target.WorldContextHandle);
        if (!IsValid(World)) return;
        if (Next == 0) StartTime = World->GetTimeSeconds();
        const auto& Step = Steps[Next];
        const auto Result = ProcessInput(Target, Step, true);
        if (!Result.bSuccess)
        {
            UE_LOG(LogWoolooPIEInput, Warning, TEXT("Sequence %s stopped at step %d: %s"), *Id.ToString(), Next, *Result.Message);
            return;
        }
        if (++Next == Steps.Num())
        {
            UE_LOG(LogWoolooPIEInput, Log, TEXT("Sequence %s submitted %d steps"), *Id.ToString(), Next);
            return;
        }
        // A single outstanding world timer owns the sequence; teardown drops the remaining inputs.
        // Overdue steps still go on separate frames so repeated actions can be processed normally.
        auto Delegate = FTimerDelegate::CreateLambda([Self = this->AsShared()] { Self->SubmitNext(); });
        const float Delay = float(StartTime + Steps[Next].OffsetSeconds - World->GetTimeSeconds());
        if (Delay <= 0.f) World->GetTimerManager().SetTimerForNextTick(MoveTemp(Delegate));
        else
        {
            FTimerHandle Timer;
            World->GetTimerManager().SetTimer(Timer, MoveTemp(Delegate), Delay, false);
        }
    }
};
template <typename StepType>
FWoolooPIEResult QueueSequence(const FWoolooPIEPlayerTarget& Target, const TArray<StepType>& Steps)
{
    if (Steps.IsEmpty() || Steps.Num() > 2048 || Steps[0].OffsetSeconds != 0.f)
        return Failure(TEXT("Supply 1 to 2048 steps, starting at zero game seconds."));
    float Previous = -1.f;
    for (const auto& Step : Steps)
    {
        if (!FMath::IsFinite(Step.OffsetSeconds) || Step.OffsetSeconds <= Previous || Step.OffsetSeconds > 120.f)
            return Failure(TEXT("Offsets must increase strictly and fit within 120 game seconds."));
        const auto Result = ProcessInput(Target, Step, false);
        if (!Result.bSuccess) return Result;
        Previous = Step.OffsetSeconds;
    }
    auto Sequence = MakeShared<TInputSequence<StepType>>();
    Sequence->Target = Target;
    Sequence->Steps = Steps;
    auto* World = Private::ResolveWorld(Target.WorldContextHandle);
    World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Sequence] { Sequence->SubmitNext(); }));
    auto Json = MakeShared<FJsonObject>();
    Json->SetObjectField(TEXT("Target"), TargetJson(Target));
    Json->SetStringField(TEXT("SequenceId"), Sequence->Id.ToString());
    Json->SetNumberField(TEXT("StepCount"), Steps.Num());
    return Success(Json, TEXT("Sequence queued. Inspect LogWoolooPIEInput for completion and verify gameplay separately."));
}
}

FWoolooPIEResult InjectInputAction(const FWoolooPIEPlayerTarget& Target, const FString& InputActionPath, const FVector& Value)
{
    FWoolooPIEInputStep Step;
    Step.InputActionPath = InputActionPath;
    Step.Value = Value;
    return ProcessInput(Target, Step, true);
}

FWoolooPIEResult InjectInputSequence(const FWoolooPIEPlayerTarget& Target, const TArray<FWoolooPIEInputStep>& Steps)
{
    return QueueSequence(Target, Steps);
}

FWoolooPIEResult InjectDebugKeySequence(const FWoolooPIEPlayerTarget& Target, const TArray<FWoolooPIEDebugKeyStep>& Steps)
{
    return QueueSequence(Target, Steps);
}
}
#endif
