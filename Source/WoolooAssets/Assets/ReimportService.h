#pragma once

#include "WoolooAssets/Tools/AssetToolTypes.h"

class USkeletalMesh;
class UMaterialInterface;

namespace Wooloo::Assets
{
FWoolooReimportResult Reimport(const FString& AssetPath, const FString& SourceFilename,
    const FString& AllowedPackageRoot, const FString& ExpectedAssetSHA256,
    const TMap<FName, FString>& MaterialOverrides, const TMap<FName, FName>& MaterialSlotRenames);

namespace ReimportDetails
{
// Internal, unreflected binding helper; validates the complete mapping before changing any slot.
bool BindMaterials(USkeletalMesh* Mesh, const TMap<FName, UMaterialInterface*>& Materials,
    const TMap<FName, FName>& Renames, FString& Error);
}
}
