#pragma once
#include "WoolooMaterial/Tools/MaterialToolTypes.h"

namespace Wooloo::Material
{
FWoolooMaterialResult CreateSurface(const FString& AssetPath, const FString& AllowedPackageRoot,
    const FLinearColor& BaseColor, const FString& BaseColorTexture, const FString& OpacityMaskTexture,
    float Roughness, bool bTwoSided, bool bUsedWithSkeletalMesh);
}
