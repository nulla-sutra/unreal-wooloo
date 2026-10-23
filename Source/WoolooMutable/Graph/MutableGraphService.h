#pragma once

#include "WoolooMutable/Graph/MutableGraphTypes.h"

struct FCompileCallbackParams;

/** Native authoring operations shared by the reflected tools. No Python, MCP or character configuration. */
class WOOLOOMUTABLE_API FMutableGraphService
{
public:
    static TArray<FMutableToolNodeType> ListNodeTypes();
    static FMutableToolResult DescribeObject(const FString& AssetPath, const FString& OwnerMetadataKey,
                                             FMutableToolGraph& Graph, TArray<FMutableToolPin>& Pins,
                                             FString& Owner, bool& bCanApply,
                                             bool bRequireReady = true);
    static FMutableToolResult CreateObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                           const FString& OwnerMetadataKey, const FString& ExpectedOwner);
    static FMutableToolResult ApplyGraph(const FString& AssetPath, const FString& AllowedPackageRoot,
                                         const FString& OwnerMetadataKey, const FString& ExpectedOwner,
                                         const FMutableToolGraph& Graph);
    static FMutableToolResult CompileObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                            const FString& OwnerMetadataKey, const FString& ExpectedOwner,
                                            FCompileCallbackParams& Flags, bool& bCallbackReceived);
    static FMutableToolResult GetParameters(const FString& AssetPath, TArray<FMutableToolParameter>& Parameters,
                                            TArray<FString>& Components, bool& bOutOfDate);
    static FMutableToolResult SaveObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                         const FString& OwnerMetadataKey, const FString& ExpectedOwner);
};
