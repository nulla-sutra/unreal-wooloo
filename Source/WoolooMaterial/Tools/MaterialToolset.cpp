#include "WoolooMaterial/Tools/MaterialToolset.h"
#include "WoolooMaterial/Parameters/MaterialParameterCollectionService.h"

FString UWoolooMaterialToolset::CreateMaterialParameterCollection(const FString& AssetPath,
    const FString& AllowedPackageRoot, const TMap<FName, float>& ScalarParameters,
    const TMap<FName, FLinearColor>& VectorParameters)
{
    return Wooloo::Material::CreateParameterCollection(AssetPath, AllowedPackageRoot,
        ScalarParameters, VectorParameters);
}
