#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooWidget/Tools/WidgetToolTypes.h"
#include "WidgetToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOWIDGET_API UWoolooWidgetToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** Read local/ancestor roots, source layout ownership and the separate compiled layout snapshot. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Widget")
    static FWoolooWidgetResult DescribeWidgetLayout(const FString& AssetPath);

    /** Set layout after dependency checks, with Undo. No full Blueprint compilation, save or gameplay run. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Widget")
    static FWoolooWidgetResult SetWidgetLayout(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FWoolooWidgetLayoutChange& Change);
};
