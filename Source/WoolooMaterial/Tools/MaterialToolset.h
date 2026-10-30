#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooMaterial/Tools/MaterialToolTypes.h"
#include "MaterialToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOMATERIAL_API UWoolooMaterialToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Create and save a new native surface material. Existing assets are never overwritten.
     * Empty texture paths use BaseColor / Opaque. OpacityMaskTexture uses Alpha and selects Masked.
     * Reuses a texture sample when both paths identify the same texture. No inferred PBR conversions. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Material")
    static FWoolooMaterialResult CreateSurfaceMaterial(const FString& AssetPath, const FString& AllowedPackageRoot,
        const FLinearColor& BaseColor, const FString& BaseColorTexture, const FString& OpacityMaskTexture,
        float Roughness, bool bTwoSided, bool bUsedWithSkeletalMesh);

    /** Create an unsaved MPC with native AssetTools and the native collection factory.
     * Requires a narrow AllowedPackageRoot and stopped PIE; existing packages are never overwritten.
     * Supply at least one scalar or vector parameter so the native buffer layout is initialized.
     * Returns JSON with success, asset_path, created, dirty and message. No save or source-control action. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Material")
    static FString CreateMaterialParameterCollection(const FString& AssetPath, const FString& AllowedPackageRoot,
        const TMap<FName, float>& ScalarParameters, const TMap<FName, FLinearColor>& VectorParameters);
};
