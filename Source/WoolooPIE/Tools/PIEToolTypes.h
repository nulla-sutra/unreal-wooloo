#pragma once

#include "CoreMinimal.h"
#include "PIEToolTypes.generated.h"

// The owning module's configuration allow-list excludes these reflected diagnostics from Test/Shipping.
USTRUCT(BlueprintType)
struct WOOLOOPIE_API FWoolooPIEPlayerTarget
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="PIE") FName WorldContextHandle;
    UPROPERTY(BlueprintReadWrite, Category="PIE") int32 LocalPlayerIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadWrite, Category="PIE") FString ExpectedControllerPath;
    UPROPERTY(BlueprintReadWrite, Category="PIE") FString ExpectedPawnPath;
};

USTRUCT(BlueprintType)
struct WOOLOOPIE_API FWoolooPIEResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PIE") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="PIE") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="PIE") FString Json;
};
