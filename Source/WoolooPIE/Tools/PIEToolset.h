#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooPIE/Tools/PIEToolTypes.h"
#include "PIEToolset.generated.h"

UCLASS(Hidden)
class WOOLOOPIE_API UWoolooPIEToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** Queue in-process PIE for the current map with temporary settings. Confirm startup with DescribeSession. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="PIE")
    static FWoolooPIEResult StartSession(const FString& ExpectedMapPackage, EWoolooPIESessionMode Mode, int32 NumPlayers);

    /** One read-only snapshot of every in-process PIE world and its players. Does not start PIE. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="PIE")
    static FWoolooPIEResult DescribeSession();

    /** Submit one frame of native Enhanced Input. Success means submitted, not ability/network success. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="PIE")
    static FWoolooPIEResult InjectInputAction(const FWoolooPIEPlayerTarget& Target,
        const FString& InputActionPath, const FVector& Value);
};
