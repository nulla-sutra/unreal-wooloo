#pragma once

#include "WoolooMutable/Graph/MutableGraphTypes.h"
#include "MutableToolResults.generated.h"

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolGraphResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FMutableToolResult Result;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Owner;

    /** False if the graph contains unsupported nodes or non-property pin defaults. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bCanApply = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FMutableToolGraph Graph;

    /** Live pin names after Mutable's property-dependent allocation. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FMutableToolPin> Pins;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolCompileResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FMutableToolResult Result;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bCallbackReceived = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bRequestFailed = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bErrors = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bWarnings = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bSkipped = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bCompiled = false;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolParametersResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FMutableToolResult Result;

    /** A compiled model may exist while its graph or referenced assets are out of date. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bOutOfDate = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FMutableToolParameter> Parameters;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FString> Components;
};
