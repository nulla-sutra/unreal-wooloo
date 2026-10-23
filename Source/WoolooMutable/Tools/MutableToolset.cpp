#include "WoolooMutable/Tools/MutableToolset.h"

#include "WoolooMutable/Graph/MutableGraphService.h"
#include "MuCO/CustomizableObject.h"

TArray<FMutableToolNodeType> UMutableToolset::ListNodeTypes()
{
    return FMutableGraphService::ListNodeTypes();
}

FMutableToolGraphResult UMutableToolset::DescribeObject(const FString& AssetPath, const FString& OwnerMetadataKey)
{
    FMutableToolGraphResult Response;
    Response.Result = FMutableGraphService::DescribeObject(
        AssetPath, OwnerMetadataKey, Response.Graph, Response.Pins, Response.Owner, Response.bCanApply);
    return Response;
}

FMutableToolResult UMutableToolset::CreateObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                               const FString& OwnerMetadataKey, const FString& ExpectedOwner)
{
    return FMutableGraphService::CreateObject(AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner);
}

FMutableToolGraphResult UMutableToolset::ApplyGraph(const FString& AssetPath, const FString& AllowedPackageRoot,
                                                  const FString& OwnerMetadataKey, const FString& ExpectedOwner,
                                                  const FMutableToolGraph& Graph)
{
    FMutableToolGraphResult Response;
    Response.Result = FMutableGraphService::ApplyGraph(
        AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Graph);
    if (Response.Result.bSuccess)
    {
        // Capture the just-written graph even if editor callbacks have queued an automatic compile.
        Response.Result = FMutableGraphService::DescribeObject(
            AssetPath, OwnerMetadataKey, Response.Graph, Response.Pins, Response.Owner, Response.bCanApply, false);
        if (Response.Result.bSuccess)
        {
            Response.Result.Message = TEXT("Complete graph replaced in memory. Compile and save are separate operations.");
        }
    }
    return Response;
}

FMutableToolCompileResult UMutableToolset::CompileObject(const FString& AssetPath,
                                                       const FString& AllowedPackageRoot,
                                                       const FString& OwnerMetadataKey, const FString& ExpectedOwner)
{
    FMutableToolCompileResult Response;
    FCompileCallbackParams Flags;
    Response.Result = FMutableGraphService::CompileObject(
        AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner, Flags, Response.bCallbackReceived);
    Response.bRequestFailed = Flags.bRequestFailed;
    Response.bErrors = Flags.bErrors;
    Response.bWarnings = Flags.bWarnings;
    Response.bSkipped = Flags.bSkipped;
    Response.bCompiled = Flags.bCompiled;
    return Response;
}

FMutableToolParametersResult UMutableToolset::GetParameters(const FString& AssetPath)
{
    FMutableToolParametersResult Response;
    Response.Result = FMutableGraphService::GetParameters(
        AssetPath, Response.Parameters, Response.Components, Response.bOutOfDate);
    return Response;
}

FMutableToolResult UMutableToolset::SaveObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                             const FString& OwnerMetadataKey, const FString& ExpectedOwner)
{
    return FMutableGraphService::SaveObject(AssetPath, AllowedPackageRoot, OwnerMetadataKey, ExpectedOwner);
}
