#pragma once

#include "WoolooBlueprint/Tools/BlueprintToolTypes.h"

namespace Wooloo::Blueprint
{
    FWoolooBlueprintResult CreateAnimationTemplate(const FString& Path, const FString& Root);
    WOOLOOBLUEPRINT_API FWoolooBlueprintResult Describe(const FString& Path);
    WOOLOOBLUEPRINT_API FWoolooBlueprintResult Edit(const FString& Path, const FString& Root, const FWoolooBlueprintEdit& Edit);
    WOOLOOBLUEPRINT_API FWoolooBlueprintResult Compile(const FString& Path, const FString& Root);
    WOOLOOBLUEPRINT_API FWoolooBlueprintResult Save(const FString& Path, const FString& Root);
}
