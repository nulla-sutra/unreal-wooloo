#pragma once

#include "CoreMinimal.h"

namespace Wooloo::Camera
{
    // Module-private implementation; the reflected toolset is the supported public entry point.
    FString ReplaceRigNode(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FString& NodePath, const FString& NewNodeClassPath,
        const TMap<FString, FString>& Properties, const FString& NodeComment);
}
