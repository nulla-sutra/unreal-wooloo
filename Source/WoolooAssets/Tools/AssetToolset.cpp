#include "WoolooAssets/Tools/AssetToolset.h"
#include "WoolooAssets/Assets/AssetService.h"
#include "WoolooAssets/Assets/ImportService.h"
#include "WoolooAssets/Assets/PreviewService.h"
#include "WoolooAssets/Assets/InspectionService.h"
#include "WoolooAssets/Assets/AnimationInspectionService.h"
#include "WoolooAssets/Assets/ReimportService.h"

FWoolooImportResult UWoolooAssetToolset::ImportAsset(const FString& SourceFilename,
    const FString& DestinationPackage, EWoolooImportKind Kind, const FString& AllowedPackageRoot,
    const FString& SkeletonPackage, const FString& NewSkeletonPackage,
    const TMap<FName, FString>& MaterialOverrides, bool bSRGB)
{
    return Wooloo::Assets::Import(SourceFilename, DestinationPackage, Kind, AllowedPackageRoot,
        SkeletonPackage, NewSkeletonPackage, MaterialOverrides, bSRGB);
}

FWoolooPreviewResult UWoolooAssetToolset::CreatePreviewMeshCollection(const FString& CollectionPackage,
    const FString& SkeletonPackage, const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot,
    bool bSetAsSkeletonDefault, const FString& PreviewMeshPackage)
{
    return Wooloo::Assets::CreatePreview(CollectionPackage, SkeletonPackage, MeshPackages,
        AllowedPackageRoot, bSetAsSkeletonDefault, PreviewMeshPackage);
}

FWoolooInspectionResult UWoolooAssetToolset::InspectAsset(const FString& AssetPath,
    const TArray<float>& SampleTimes, const TArray<FName>& BoneNames)
{
    return Wooloo::Assets::Inspect(AssetPath, SampleTimes, BoneNames);
}

FWoolooInspectionResult UWoolooAssetToolset::InspectAnimationSamples(const FString& AssetPath,
    const TArray<float>& SampleTimes, const TArray<FName>& BoneNames)
{
    return Wooloo::Assets::InspectAnimation(AssetPath, SampleTimes, BoneNames);
}

FWoolooReimportResult UWoolooAssetToolset::ReimportAsset(const FString& AssetPath, const FString& SourceFilename,
    const FString& AllowedPackageRoot, const FString& ExpectedAssetSHA256,
    const TMap<FName, FString>& MaterialOverrides, const TMap<FName, FName>& MaterialSlotRenames)
{
    return Wooloo::Assets::Reimport(AssetPath, SourceFilename, AllowedPackageRoot, ExpectedAssetSHA256,
        MaterialOverrides, MaterialSlotRenames);
}

FWoolooPreviewUpdateResult UWoolooAssetToolset::UpdatePreviewMeshCollection(const FString& CollectionPackage,
    const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot, const FString& ExpectedCollectionSHA256)
{
    return Wooloo::Assets::UpdatePreview(CollectionPackage, MeshPackages, AllowedPackageRoot, ExpectedCollectionSHA256);
}

FWoolooAssetResult UWoolooAssetToolset::PlanMoves(const TArray<FWoolooAssetMove>& Moves,
    const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots)
{
    return Wooloo::Assets::PlanMoves(Moves, AllowedPackageRoots, ProtectedPackageRoots);
}

FWoolooAssetResult UWoolooAssetToolset::MoveAssets(const TArray<FWoolooAssetMove>& Moves,
    const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots,
    const TArray<FString>& ApprovedReferencerPackages)
{
    return Wooloo::Assets::Move(Moves, AllowedPackageRoots, ProtectedPackageRoots, ApprovedReferencerPackages);
}

FWoolooAssetResult UWoolooAssetToolset::InspectRedirectors(const TArray<FString>& RedirectorPackages,
    const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots)
{
    return Wooloo::Assets::InspectRedirectors(RedirectorPackages, AllowedPackageRoots, ProtectedPackageRoots);
}

FWoolooAssetResult UWoolooAssetToolset::FixupRedirectors(const TArray<FString>& RedirectorPackages,
    const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots,
    const TArray<FString>& ApprovedReferencerPackages)
{
    return Wooloo::Assets::FixupRedirectors(RedirectorPackages, AllowedPackageRoots,
        ProtectedPackageRoots, ApprovedReferencerPackages);
}
