#pragma once

#include "WoolooWidget/Tools/WidgetToolTypes.h"

namespace Wooloo::Widget
{
    // Module-private implementation; the reflected toolset is the supported public entry point.
    FWoolooWidgetResult Describe(const FString& AssetPath);
    FWoolooWidgetResult SetLayout(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FWoolooWidgetLayoutChange& Change);
}
