#pragma once

#include "CoreMinimal.h"
#include "BlueprintToolTypes.generated.h"

USTRUCT(BlueprintType)
struct WOOLOOBLUEPRINT_API FWoolooBlueprintResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Blueprint") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Blueprint") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Blueprint") FString Json;
    UPROPERTY(BlueprintReadOnly, Category="Blueprint") FGuid NodeId;
};

USTRUCT(BlueprintType)
struct WOOLOOBLUEPRINT_API FWoolooBlueprintEdit
{
    GENERATED_BODY()
    /** AddNode, RemoveNode, ConnectPins, DisconnectPins, SetPinDefault, MoveNode, SetComment,
     * SetNodeProperties, SetClassDefaults, RemoveInterface, RemoveFunction, RemoveVariable. */
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FString Operation;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FName GraphName = TEXT("EventGraph");
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FGuid NodeId;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FGuid TargetNodeId;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FName PinName;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FName TargetPinName;
    /** Full UEdGraphNode class path, or interface class path for RemoveInterface. */
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FString ClassPath;
    /** Optional reflected UFunction path for a new UK2Node_CallFunction. */
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FString FunctionPath;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FName FunctionName;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FName VariableName;
    /** Top-level reflected values in UE ImportText syntax. New nodes apply these before pin allocation. */
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") TMap<FString, FString> Properties;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FString Value;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") int32 X = 0;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") int32 Y = 0;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") int32 Width = 640;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") int32 Height = 320;
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") FLinearColor CommentColor = FLinearColor(0.1f, 0.2f, 0.3f);
    /** Replacing an existing exec/data link requires explicit opt-in. */
    UPROPERTY(BlueprintReadWrite, Category="Blueprint") bool bAllowBreakingLinks = false;
};
