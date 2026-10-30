#include "WoolooMaterial/Materials/SurfaceMaterialService.h"

#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace Wooloo::Material
{
FWoolooMaterialResult CreateSurface(const FString& AssetPath, const FString& AllowedPackageRoot,
    const FLinearColor& BaseColor, const FString& BaseColorTexture, const FString& OpacityMaskTexture,
    float Roughness, bool bTwoSided, bool bUsedWithSkeletalMesh)
{
    FWoolooMaterialResult Out;
    Out.AssetPath = AssetPath;
    auto Root = AllowedPackageRoot;
    Root.RemoveFromEnd(TEXT("/"));
    FString Filename;
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
    {
        Out.Message = TEXT("Requires the editor game thread with PIE stopped.");
        return Out;
    }
    if (!FPackageName::IsValidLongPackageName(Root)
        || Root.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1) == INDEX_NONE
        || !FPackageName::IsValidLongPackageName(AssetPath)
        || !AssetPath.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase)
        || !FPackageName::TryConvertLongPackageNameToFilename(AssetPath, Filename)
        || FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), FPaths::ConvertRelativePathToFull(FPaths::EngineDir())))
    {
        Out.Message = TEXT("Destination must be inside a narrow mounted AllowedPackageRoot, outside engine content.");
        return Out;
    }
    if (FPackageName::DoesPackageExist(AssetPath) || FindPackage(nullptr, *AssetPath))
    {
        Out.Message = TEXT("Material destination already exists on disk or in memory.");
        return Out;
    }
    if (!FMath::IsFinite(Roughness) || Roughness < 0.f || Roughness > 1.f
        || !FMath::IsFinite(BaseColor.R) || !FMath::IsFinite(BaseColor.G)
        || !FMath::IsFinite(BaseColor.B) || !FMath::IsFinite(BaseColor.A)
        || BaseColor.R < 0.f || BaseColor.G < 0.f || BaseColor.B < 0.f || BaseColor.A < 0.f)
    {
        Out.Message = TEXT("Supply finite non-negative colors and roughness in [0, 1].");
        return Out;
    }
    const auto LoadTexture = [](const FString& Path)
    {
        return Path.IsEmpty() ? nullptr : LoadObject<UTexture2D>(nullptr,
            *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
    };
    auto* ColorTexture = LoadTexture(BaseColorTexture);
    auto* MaskTexture = LoadTexture(OpacityMaskTexture);
    if ((!BaseColorTexture.IsEmpty() && !ColorTexture) || (!OpacityMaskTexture.IsEmpty() && !MaskTexture))
    {
        Out.Message = TEXT("Texture paths must reference existing Texture2D assets.");
        return Out;
    }
    TStrongObjectPtr<UMaterialFactoryNew> Factory(NewObject<UMaterialFactoryNew>());
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    auto* Material = Cast<UMaterial>(Tools.CreateAsset(FPackageName::GetShortName(AssetPath),
        FPackageName::GetLongPackagePath(AssetPath), UMaterial::StaticClass(), Factory.Get()));
    if (!Material)
    {
        Out.Message = TEXT("Native material factory failed.");
        return Out;
    }
    Out.bCreated = true;
    Material->TwoSided = bTwoSided;
    Material->BlendMode = MaskTexture ? BLEND_Masked : BLEND_Opaque;
    UMaterialExpressionTextureSample* ColorSample = nullptr;
    auto bConnected = true;
    const auto TextureSample = [Material](UTexture2D* Texture, int32 Y)
    {
        auto* Node = CastChecked<UMaterialExpressionTextureSample>(UMaterialEditingLibrary::CreateMaterialExpression(
            Material, UMaterialExpressionTextureSample::StaticClass(), -500, Y));
        Node->Texture = Texture;
        Node->AutoSetSampleType();
        return Node;
    };
    if (ColorTexture)
    {
        ColorSample = TextureSample(ColorTexture, -150);
        ColorSample->Desc = TEXT("基础颜色");
        bConnected &= UMaterialEditingLibrary::ConnectMaterialProperty(ColorSample, TEXT("RGB"), MP_BaseColor);
    }
    else
    {
        auto* Color = CastChecked<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(
            Material, UMaterialExpressionConstant3Vector::StaticClass(), -500, -150));
        Color->Constant = BaseColor;
        Color->Desc = TEXT("基础颜色");
        bConnected &= UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT(""), MP_BaseColor);
    }
    if (MaskTexture)
    {
        auto* Mask = MaskTexture == ColorTexture ? ColorSample : TextureSample(MaskTexture, 200);
        Mask->Desc = Mask == ColorSample ? TEXT("基础颜色与透明遮罩") : TEXT("透明遮罩");
        bConnected &= UMaterialEditingLibrary::ConnectMaterialProperty(Mask, TEXT("A"), MP_OpacityMask);
    }
    auto* Rough = CastChecked<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(
        Material, UMaterialExpressionConstant::StaticClass(), -200, 200));
    Rough->R = Roughness;
    Rough->Desc = TEXT("表面粗糙度");
    bConnected &= UMaterialEditingLibrary::ConnectMaterialProperty(Rough, TEXT(""), MP_Roughness);
    if (!bConnected)
    {
        Out.Message = TEXT("Native material connections failed; the new material remains unsaved.");
        return Out;
    }
    if (bUsedWithSkeletalMesh) Material->SetMaterialUsage(MATUSAGE_SkeletalMesh);
    Material->PostEditChange();
    const auto Errors = UMaterialEditingLibrary::RecompileMaterial(Material);
    if (!Errors.IsEmpty())
    {
        Out.Warnings = Errors;
        Out.Message = TEXT("Material compilation reported errors; the new material remains unsaved.");
        return Out;
    }
    Filename += FPackageName::GetAssetPackageExtension();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Out.bSaved = UPackage::SavePackage(Material->GetOutermost(), Material, *Filename, Args);
    Out.bSuccess = Out.bSaved;
    Out.Message = Out.bSaved ? TEXT("Created and saved the surface material.") : TEXT("Failed to save the new material.");
    return Out;
}
}
