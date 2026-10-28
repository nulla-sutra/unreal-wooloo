#pragma once

#include "CoreMinimal.h"

namespace Wooloo::Material
{
FString CreateParameterCollection(const FString& AssetPath, const FString& AllowedPackageRoot,
    const TMap<FName, float>& ScalarParameters, const TMap<FName, FLinearColor>& VectorParameters);
}
