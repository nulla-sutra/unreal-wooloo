#include "WoolooAssets/Tools/AssetToolset.h"
#include "WoolooAssets/Assets/AssetService.h"

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
