#pragma once

#include "CoreMinimal.h"
#include "StateTreeToolTypes.generated.h"

USTRUCT(BlueprintType)
struct WOOLOOSTATETREE_API FWoolooStateTreeResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="StateTree") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="StateTree") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="StateTree") FString Json;
    UPROPERTY(BlueprintReadOnly, Category="StateTree") FGuid EntityId;
};

/** One explicit edit. No action compiles or saves implicitly. IDs come from DescribeStateTree. */
USTRUCT(BlueprintType)
struct WOOLOOSTATETREE_API FWoolooStateTreeEdit
{
    GENERATED_BODY()
    /** AddState, RemoveState, AddTask, AddEvaluator, AddEnterCondition, AddTransitionCondition, SetInstanceProperties,
     * RemoveNode, SetTransition, RemoveTransition, EnsureObjectParameter, RemoveRootParameter, BindProperty,
     * BindPropertyFunction, RemoveBinding. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString Operation;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid StateId;
    /** AddState parent; invalid creates a root. BindProperty source; invalid selects global parameters. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid SourceId;
    /** Binding target node. BindPropertyFunction returns its owned function instance ID in EntityId. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid NodeId;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid TransitionId;
    /** AddState/EnsureObjectParameter require a name; naming added nodes is optional.
     * RemoveRootParameter requires an existing, unbound root parameter name. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FName Name;
    /** AddTask: task struct or concrete UStateTreeTaskBlueprintBase class path (including Blueprint _C classes).
     * AddEvaluator/Conditions/BindPropertyFunction: native struct path. EnsureObjectParameter: object class path. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString TypePath;
    /** Editable instance fields using UE ImportText. BindPropertyFunction configures the new function instance. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") TMap<FString, FString> Properties;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid TargetStateId;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString Trigger = TEXT("OnTick");
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString TargetType = TEXT("GotoState");
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString Priority = TEXT("Normal");
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString EventTag;
    /** Empty binds the whole source context object/struct; otherwise a property path within that source.
     * BindPropertyFunction requires an output path on the new function, e.g. bMatches. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString SourcePath;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString TargetPath;
};
