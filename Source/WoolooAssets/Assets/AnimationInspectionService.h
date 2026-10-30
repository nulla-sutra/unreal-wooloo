#pragma once

#include "WoolooAssets/Tools/AssetToolTypes.h"

namespace Wooloo::Assets
{
FWoolooInspectionResult InspectAnimation(const FString& AssetPath,
    const TArray<float>& SampleTimes, const TArray<FName>& BoneNames);
}
