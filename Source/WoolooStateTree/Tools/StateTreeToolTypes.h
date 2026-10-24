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
    /** AddState, RemoveState, AddTask, AddEnterCondition, AddTransitionCondition, SetInstanceProperties,
     * RemoveNode, SetTransition, RemoveTransition, EnsureObjectParameter, BindProperty, RemoveBinding. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString Operation;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid StateId;
    /** AddState parent; invalid creates a root. BindProperty source; invalid selects global parameters. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid SourceId;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid NodeId;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid TransitionId;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FName Name;
    /** AddTask: task struct or concrete UStateTreeTaskBlueprintBase class path (including Blueprint _C classes).
     * Conditions: native struct path. EnsureObjectParameter: object class path. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString TypePath;
    /** Editable struct/UObject instance fields using UE ImportText. Nested structs accept a complete literal. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") TMap<FString, FString> Properties;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FGuid TargetStateId;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString Trigger = TEXT("OnTick");
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString TargetType = TEXT("GotoState");
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString Priority = TEXT("Normal");
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString EventTag;
    /** Empty binds the whole source context object/struct; otherwise a property path within that source. */
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString SourcePath;
    UPROPERTY(BlueprintReadWrite, Category="StateTree") FString TargetPath;
};
