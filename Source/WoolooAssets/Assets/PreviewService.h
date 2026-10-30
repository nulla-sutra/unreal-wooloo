#pragma once
#include "WoolooAssets/Tools/AssetToolTypes.h"

namespace Wooloo::Assets
{
FWoolooPreviewResult CreatePreview(const FString& CollectionPackage, const FString& SkeletonPackage,
    const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot,
    bool bSetAsSkeletonDefault, const FString& PreviewMeshPackage);

FWoolooPreviewUpdateResult UpdatePreview(const FString& CollectionPackage,
    const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot,
    const FString& ExpectedCollectionSHA256);
}
