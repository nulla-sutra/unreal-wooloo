#pragma once

#include "WoolooAssets/Tools/AssetToolTypes.h"

namespace Wooloo::Assets
{
FWoolooAssetResult PlanMoves(const TArray<FWoolooAssetMove>& Moves,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots);
FWoolooAssetResult Move(const TArray<FWoolooAssetMove>& Moves,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots,
    const TArray<FString>& ApprovedReferencers);
FWoolooAssetResult InspectRedirectors(const TArray<FString>& Packages,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots);
FWoolooAssetResult FixupRedirectors(const TArray<FString>& Packages,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots,
    const TArray<FString>& ApprovedReferencers);
}
