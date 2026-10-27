#pragma once

#include "CoreMinimal.h"
#include "WidgetToolTypes.generated.h"

UENUM(BlueprintType)
enum class EWoolooWidgetLayoutMode : uint8
{
    Inherit,
    Local
};

USTRUCT(BlueprintType)
struct WOOLOOWIDGET_API FWoolooWidgetLayoutChange
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="Widget") EWoolooWidgetLayoutMode Mode = EWoolooWidgetLayoutMode::Inherit;
    /** Full concrete UWidget class path; required for Local and empty for Inherit. */
    UPROPERTY(BlueprintReadWrite, Category="Widget") FString RootWidgetClassPath;
    /** Exact object name; required for Local and None for Inherit. */
    UPROPERTY(BlueprintReadWrite, Category="Widget") FName RootWidgetName;
    /** Explicitly permit removing the old root and its descendants, subject to dependency checks. */
    UPROPERTY(BlueprintReadWrite, Category="Widget") bool bDiscardLocalRoot = false;
};

USTRUCT(BlueprintType)
struct WOOLOOWIDGET_API FWoolooWidgetResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Widget") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Widget") FString Message;
    /** Layout snapshot with diagnostics; failed edits include blocking issues and changed=false. */
    UPROPERTY(BlueprintReadOnly, Category="Widget") FString Json;
};
