#pragma once

#include "CoreMinimal.h"
#include "MutableGraphTypes.generated.h"

// Shared graph specifications and operation results for the native service and reflected tools.
USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolProperty
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Name;

    /** Raw text for FString/FName; Unreal reference syntax for asset properties. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Value;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolNode
{
    GENERATED_BODY()

    /** Unique UObject name within this graph. Connections use this ID. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Id;

    /** A class path returned by ListNodeTypes. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString ClassPath;

    /** Preserve existing GUIDs when editing; empty generates a new GUID. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Guid;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    int32 X = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    int32 Y = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FMutableToolProperty> Properties;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolLink
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString FromNode;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString FromPin;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString ToNode;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString ToPin;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolGraph
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FMutableToolNode> Nodes;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FMutableToolLink> Links;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolPin
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString NodeId;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Name;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Direction;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Category;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolPropertyInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Name;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Type;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString DefaultValue;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolNodeType
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString ClassPath;

    /** Only these properties can be supplied to ApplyGraph. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FMutableToolPropertyInfo> Properties;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bSuccess = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Message;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString AssetPath;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bDirty = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bCreated = false;

    /** True only after this call explicitly saved the target package. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bSaved = false;
};

USTRUCT(BlueprintType)
struct WOOLOOMUTABLE_API FMutableToolParameter
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Name;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString Type;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bMultidimensional = false;

    /** Scalar defaults are returned as text; other parameter types have no value here. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    bool bHasDefaultValue = false;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString DefaultValue;

    /** Default named choice for an Int parameter; not an enum display index. */
    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    FString DefaultOption;

    UPROPERTY(BlueprintReadWrite, Category = "Mutable")
    TArray<FString> Options;
};
