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
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SoftObjectPath.h"

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

FWoolooPIEResult InjectInputAction(const FWoolooPIEPlayerTarget& Target,
    const FString& InputActionPath, const FVector& Value)
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
    auto* Controller = IsValid(Player) ? Player->GetPlayerController(World) : nullptr;
    const auto Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
    FString Error;
    if (!Private::ValidateIdentity(Target, World, Controller, Pawn, Error)) return Failure(Error);
    if (!Controller->IsLocalController() || Controller->GetLocalPlayer() != Player || Player->GetWorld() != World)
        return Failure(TEXT("Target is not a local player in the selected PIE world."));
    auto* Subsystem = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    if (!IsValid(Subsystem) || !IsValid(Subsystem->GetPlayerInput()))
        return Failure(TEXT("Target has no active Enhanced Input local-player subsystem/player input."));

    // Match only already-bound actions; do not load assets or pick the first registry name match.
    FString Path = InputActionPath;
    if (FPackageName::IsValidLongPackageName(Path, true)) Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    const FSoftObjectPath ActionPath(Path);
    if (!ActionPath.IsValid() || !ActionPath.GetSubPathUtf8String().IsEmpty()
        || !FPackageName::IsValidLongPackageName(ActionPath.GetLongPackageName(), true))
        return Failure(TEXT("InputActionPath must be a full asset package or object path."));
    const auto* Action = Private::FindBoundAction(Controller->Debug_GetMostRecentInputStack(), Path, Error);
    if (!Action || !Private::ValidateValue(Action->ValueType, Value, Error)) return Failure(Error);

    Subsystem->InjectInputForAction(Action, FInputActionValue(Action->ValueType, Value), {}, {});
    auto Json = MakeShared<FJsonObject>();
    Json->SetObjectField(TEXT("Target"), TargetJson(Target));
    Json->SetStringField(TEXT("InputActionPath"), Action->GetPathName());
    Json->SetBoolField(TEXT("Submitted"), true);
    Json->SetNumberField(TEXT("Frames"), 1);
    return Success(Json, TEXT("One input frame submitted. Verify ability activation and server response separately."));
}
}
#endif
