#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "MaterialToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOMATERIAL_API UWoolooMaterialToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Create an unsaved MPC with native AssetTools and the native collection factory.
     * Requires a narrow AllowedPackageRoot and stopped PIE; existing packages are never overwritten.
     * Supply at least one scalar or vector parameter so the native buffer layout is initialized.
     * Returns JSON with success, asset_path, created, dirty and message. No save or source-control action. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Material")
    static FString CreateMaterialParameterCollection(const FString& AssetPath, const FString& AllowedPackageRoot,
        const TMap<FName, float>& ScalarParameters, const TMap<FName, FLinearColor>& VectorParameters);
};
