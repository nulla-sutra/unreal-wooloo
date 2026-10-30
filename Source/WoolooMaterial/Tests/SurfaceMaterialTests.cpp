#if WITH_DEV_AUTOMATION_TESTS && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

#include "WoolooMaterial/Tools/MaterialToolset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooSurfaceMaterialTest, "Wooloo.Material.Surface.CreateSavedAndRejectInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooSurfaceMaterialTest::RunTest(const FString& Parameters)
{
    const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto Mount = FString(TEXT("/WoolooSurfaceFixture/"));
    const auto Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/WoolooMaterial/Fixtures") / Id) + TEXT("/");
    const auto Root = Mount + Id;
    const auto Path = Root + TEXT("/M_Surface");
    FPackageName::RegisterMountPoint(Mount, Directory);
    TestFalse(TEXT("Reject non-finite roughness before creating any package"), UWoolooMaterialToolset::CreateSurfaceMaterial(
        Root + TEXT("/M_Invalid"), Root, FLinearColor::White, TEXT(""), TEXT(""),
        std::numeric_limits<float>::quiet_NaN(), false, true).bSuccess);
    TestFalse(TEXT("Reject a missing texture before creating the material"), UWoolooMaterialToolset::CreateSurfaceMaterial(
        Root + TEXT("/M_MissingTexture"), Root, FLinearColor::White, Root + TEXT("/T_Missing"), TEXT(""), .7f, false, true).bSuccess);
    const FLinearColor Color(.2f, .4f, .6f, 1.f);
    const auto Result = UWoolooMaterialToolset::CreateSurfaceMaterial(Path, Root, Color, TEXT(""), TEXT(""), .7f, true, true);
    auto* Material = LoadObject<UMaterial>(nullptr, *(Path + TEXT(".M_Surface")));
    TestTrue(TEXT("Creates and saves the exact new material"), Result.bSuccess && Result.bCreated && Result.bSaved);
    if (Material)
    {
        TestTrue(TEXT("Two-sided and SkeletalMesh usage are explicit"), Material->TwoSided && Material->GetUsageByFlag(MATUSAGE_SkeletalMesh));
        TestEqual(TEXT("No mask means opaque"), Material->GetBlendMode(), BLEND_Opaque);
        const auto* Constant = Cast<UMaterialExpressionConstant3Vector>(Material->GetEditorOnlyData()->BaseColor.Expression);
        TestTrue(TEXT("Native graph preserves the requested color"), Constant && Constant->Constant.Equals(Color));
        TestFalse(TEXT("Does not overwrite saved material"), UWoolooMaterialToolset::CreateSurfaceMaterial(
            Path, Root, FLinearColor::Black, TEXT(""), TEXT(""), .1f, false, false).bSuccess);
        FAssetRegistryModule::AssetDeleted(Material);
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->SetFlags(RF_Transient);
        Material->GetOutermost()->SetDirtyFlag(false);
        const auto Filename = FPackageName::LongPackageNameToFilename(Path, TEXT(".uasset"));
        if (FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), Directory))
            IFileManager::Get().Delete(*Filename, false, true);
    }
    FPackageName::UnRegisterMountPoint(Mount, Directory);
    return Result.bSuccess;
}

#endif
