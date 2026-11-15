#pragma once

#include "CoreMinimal.h"
#include "PIEToolTypes.generated.h"

// The owning module's configuration allow-list excludes these reflected diagnostics from Test/Shipping.
UENUM(BlueprintType)
enum class EWoolooPIESessionMode : uint8
{
    Standalone,
    DedicatedServer
};

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
struct WOOLOOPIE_API FWoolooPIEInputStep
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="PIE") float OffsetSeconds = 0.f;
    UPROPERTY(BlueprintReadWrite, Category="PIE") FString InputActionPath;
    UPROPERTY(BlueprintReadWrite, Category="PIE") FVector Value = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct WOOLOOPIE_API FWoolooPIEDebugKeyStep
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="PIE") float OffsetSeconds = 0.f;
    UPROPERTY(BlueprintReadWrite, Category="PIE") FName Key;
};

USTRUCT(BlueprintType)
struct WOOLOOPIE_API FWoolooPIEResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PIE") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="PIE") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="PIE") FString Json;
};
