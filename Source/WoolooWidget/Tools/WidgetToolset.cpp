#include "WoolooWidget/Tools/WidgetToolset.h"
#include "WoolooWidget/Layout/WidgetLayoutService.h"

FWoolooWidgetResult UWoolooWidgetToolset::DescribeWidgetLayout(const FString& AssetPath)
{
    return Wooloo::Widget::Describe(AssetPath);
}

FWoolooWidgetResult UWoolooWidgetToolset::SetWidgetLayout(const FString& AssetPath,
    const FString& AllowedPackageRoot, const FWoolooWidgetLayoutChange& Change)
{
    return Wooloo::Widget::SetLayout(AssetPath, AllowedPackageRoot, Change);
}
