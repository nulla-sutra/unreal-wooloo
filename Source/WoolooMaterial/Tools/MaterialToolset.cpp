#include "WoolooMaterial/Tools/MaterialToolset.h"
#include "WoolooMaterial/Parameters/MaterialParameterCollectionService.h"
#include "WoolooMaterial/Materials/SurfaceMaterialService.h"

FWoolooMaterialResult UWoolooMaterialToolset::CreateSurfaceMaterial(const FString& AssetPath,
    const FString& AllowedPackageRoot, const FLinearColor& BaseColor, const FString& BaseColorTexture,
    const FString& OpacityMaskTexture, float Roughness, bool bTwoSided, bool bUsedWithSkeletalMesh)
{
    return Wooloo::Material::CreateSurface(AssetPath, AllowedPackageRoot, BaseColor,
        BaseColorTexture, OpacityMaskTexture, Roughness, bTwoSided, bUsedWithSkeletalMesh);
}

FString UWoolooMaterialToolset::CreateMaterialParameterCollection(const FString& AssetPath,
    const FString& AllowedPackageRoot, const TMap<FName, float>& ScalarParameters,
    const TMap<FName, FLinearColor>& VectorParameters)
{
    return Wooloo::Material::CreateParameterCollection(AssetPath, AllowedPackageRoot,
        ScalarParameters, VectorParameters);
}
