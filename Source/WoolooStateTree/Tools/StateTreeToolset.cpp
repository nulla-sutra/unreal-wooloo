#include "WoolooStateTree/Tools/StateTreeToolset.h"
#include "WoolooStateTree/StateTree/StateTreeService.h"

FWoolooStateTreeResult UWoolooStateTreeToolset::DescribeStateTree(const FString& AssetPath)
{ return Wooloo::StateTree::Describe(AssetPath); }
FWoolooStateTreeResult UWoolooStateTreeToolset::CreateStateTree(const FString& AssetPath,
    const FString& AllowedPackageRoot, const FString& SchemaClassPath)
{ return Wooloo::StateTree::Create(AssetPath, AllowedPackageRoot, SchemaClassPath); }
FWoolooStateTreeResult UWoolooStateTreeToolset::EditStateTree(const FString& AssetPath,
    const FString& AllowedPackageRoot, const FWoolooStateTreeEdit& Edit)
{ return Wooloo::StateTree::Edit(AssetPath, AllowedPackageRoot, Edit); }
FWoolooStateTreeResult UWoolooStateTreeToolset::CompileStateTree(const FString& AssetPath,
    const FString& AllowedPackageRoot)
{ return Wooloo::StateTree::Compile(AssetPath, AllowedPackageRoot); }
FWoolooStateTreeResult UWoolooStateTreeToolset::SaveStateTree(const FString& AssetPath,
    const FString& AllowedPackageRoot)
{ return Wooloo::StateTree::Save(AssetPath, AllowedPackageRoot); }
