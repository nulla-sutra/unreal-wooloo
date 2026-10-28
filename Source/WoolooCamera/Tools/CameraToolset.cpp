#include "WoolooCamera/Tools/CameraToolset.h"
#include "WoolooCamera/Graph/CameraRigService.h"

FString UWoolooCameraToolset::ReplaceCameraRigNode(const FString& AssetPath, const FString& AllowedPackageRoot,
    const FString& NodePath, const FString& NewNodeClassPath,
    const TMap<FString, FString>& Properties, const FString& NodeComment)
{
    return Wooloo::Camera::ReplaceRigNode(AssetPath, AllowedPackageRoot, NodePath,
        NewNodeClassPath, Properties, NodeComment);
}
