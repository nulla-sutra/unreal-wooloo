#pragma once

#include "WoolooAssets/Tools/AssetToolTypes.h"

class USkeleton;
class USkeletalMesh;

namespace Wooloo::Assets
{
FWoolooImportResult Import(const FString& SourceFilename, const FString& DestinationPackage,
    EWoolooImportKind Kind, const FString& AllowedPackageRoot, const FString& SkeletonPackage,
    const FString& NewSkeletonPackage, const TMap<FName, FString>& MaterialOverrides, bool bSRGB);

// Internal production helpers shared by import and preview authoring.
namespace Production
{
bool Ready(FString& Error);
bool Scope(const FString& Package, const FString& Root, FString& Error);
bool Unoccupied(const FString& Package, FString& Error);
bool Save(UObject* Asset, FString& Error);
FString ObjectPath(const FString& Package);
bool Compatible(const USkeleton* Skeleton, const USkeletalMesh* Mesh, FString& Error);
}
}
