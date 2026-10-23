#include "WoolooBlueprint/Tools/BlueprintToolset.h"
#include "WoolooBlueprint/Graph/BlueprintGraphService.h"

FWoolooBlueprintResult UWoolooBlueprintToolset::DescribeBlueprint(const FString& AssetPath)
{ return Wooloo::Blueprint::Describe(AssetPath); }
FWoolooBlueprintResult UWoolooBlueprintToolset::EditBlueprint(const FString& AssetPath,
    const FString& AllowedPackageRoot, const FWoolooBlueprintEdit& Edit)
{ return Wooloo::Blueprint::Edit(AssetPath, AllowedPackageRoot, Edit); }
FWoolooBlueprintResult UWoolooBlueprintToolset::CompileBlueprint(const FString& AssetPath, const FString& AllowedPackageRoot)
{ return Wooloo::Blueprint::Compile(AssetPath, AllowedPackageRoot); }
FWoolooBlueprintResult UWoolooBlueprintToolset::SaveBlueprint(const FString& AssetPath, const FString& AllowedPackageRoot)
{ return Wooloo::Blueprint::Save(AssetPath, AllowedPackageRoot); }
