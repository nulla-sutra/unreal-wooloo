#if WITH_DEV_AUTOMATION_TESTS && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

#include "WoolooAssets/Tools/AssetToolset.h"
#include "WoolooAssets/Assets/AssetWriteGuard.h"
#include "WoolooAssets/Assets/ReimportService.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/PreviewMeshCollection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
struct FProductionFixture
{
    FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString Mount = TEXT("/WoolooProductionFixture/");
    FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/WoolooAssets/Fixtures") / Id) + TEXT("/");
    FString Root = Mount + Id;
    TArray<TStrongObjectPtr<UObject>> Assets;
    TArray<FString> Files;
    TArray<FString> BackupFiles;

    FProductionFixture()
    {
        FPackageName::RegisterMountPoint(Mount, Directory);
    }

    void Track(UObject* Asset)
    {
        if (!Asset) return;
        Assets.Emplace(Asset);
        const auto Package = Asset->GetOutermost()->GetName();
        for (const auto* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk")})
            Files.Add(FPackageName::LongPackageNameToFilename(Package, Extension));
    }

    ~FProductionFixture()
    {
        for (auto& Asset : Assets)
        {
            FAssetRegistryModule::AssetDeleted(Asset.Get());
            Asset->ClearFlags(RF_Public | RF_Standalone);
            Asset->SetFlags(RF_Transient);
            Asset->GetOutermost()->SetDirtyFlag(false);
        }
        // Only exact fixture files under Saved are removed, never a recursive Content cleanup.
        for (const auto& File : Files)
            if (FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(File), Directory))
                IFileManager::Get().Delete(*File, false, true);
        const auto BackupRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/WoolooAssets/PreviewBackups"));
        for (const auto& File : BackupFiles)
            if (FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(File), BackupRoot))
                IFileManager::Get().Delete(*File, false, true);
        FPackageName::UnRegisterMountPoint(Mount, Directory);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooNativeImportTest, "Wooloo.Assets.Production.ImportTextureAndRefuseOverwrite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooNativeImportTest::RunTest(const FString& Parameters)
{
    FProductionFixture Fixture;
    IFileManager::Get().MakeDirectory(*Fixture.Directory, true);
    TArray64<uint8> PNG;
    const TArray<FColor> Pixels = {FColor::White};
    FImageUtils::PNGCompressImageArray(1, 1, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PNG);
    const auto Source = Fixture.Directory / TEXT("Pixel.png");
    Fixture.Files.Add(Source);
    if (!TestTrue(TEXT("Write generated fixture PNG"), FFileHelper::SaveArrayToFile(PNG, *Source))) return false;
    const auto Path = Fixture.Root + TEXT("/T_Pixel");
    const auto Result = UWoolooAssetToolset::ImportAsset(Source, Path, EWoolooImportKind::Texture2D,
        Fixture.Root, TEXT(""), TEXT(""), {}, false);
    auto* Texture = LoadObject<UTexture2D>(nullptr, *(Path + TEXT(".T_Pixel")));
    Fixture.Track(Texture);
    if (!TestTrue(TEXT("Native texture import and save succeed"), Result.bSuccess)) return false;
    TestTrue(TEXT("Reports the exact saved package"), Result.SavedPackages.Contains(Path));
    TestTrue(TEXT("Persists requested linear color space"), Texture && !Texture->SRGB);
    TestFalse(TEXT("Never overwrites an existing destination"), UWoolooAssetToolset::ImportAsset(
        Source, Path, EWoolooImportKind::Texture2D, Fixture.Root, TEXT(""), TEXT(""), {}, true).bSuccess);
    TestFalse(TEXT("Cannot write outside the allowed scope"), UWoolooAssetToolset::ImportAsset(
        Source, TEXT("/Engine/T_Forbidden"), EWoolooImportKind::Texture2D, Fixture.Root, TEXT(""), TEXT(""), {}, true).bSuccess);
    FString Hash, Error;
    const auto Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
    if (!TestTrue(TEXT("Fingerprint saved fixture"), Wooloo::Assets::WriteGuard::HashFile(Filename, Hash, Error))) return false;
    const auto FBX = Fixture.Directory / TEXT("Unsupported.fbx");
    Fixture.Files.Add(FBX);
    FFileHelper::SaveStringToFile(TEXT("Not an FBX; wrong-type refusal must happen first"), *FBX);
    const auto WrongHash = UWoolooAssetToolset::ReimportAsset(Path, FBX, Fixture.Root, FString::ChrN(64, TEXT('0')), {}, {});
    TestFalse(TEXT("Stale hash is refused before mutation"), WrongHash.bSuccess || WrongHash.bModifiedInMemory);
    TestTrue(TEXT("Stale hash does not create a backup"), WrongHash.BackupDirectory.IsEmpty());
    const auto WrongType = UWoolooAssetToolset::ReimportAsset(Path, FBX, Fixture.Root, Hash, {}, {});
    TestFalse(TEXT("FBX reimport never overwrites a texture"), WrongType.bSuccess || WrongType.bModifiedInMemory);
    TestTrue(TEXT("Wrong type fails before backup/native import"), WrongType.BackupDirectory.IsEmpty());
    Texture->MarkPackageDirty();
    const auto Dirty = UWoolooAssetToolset::ReimportAsset(Path, FBX, Fixture.Root, Hash, {}, {});
    TestFalse(TEXT("Dirty editor is refused"), Dirty.bSuccess || Dirty.bModifiedInMemory);
    Texture->GetOutermost()->SetDirtyFlag(false);
    const auto Companion = FPaths::ChangeExtension(Filename, TEXT("uexp"));
    Fixture.Files.Add(Companion);
    FFileHelper::SaveStringToFile(TEXT("external payload fixture"), *Companion);
    const auto Split = UWoolooAssetToolset::ReimportAsset(Path, FBX, Fixture.Root, Hash, {}, {});
    TestFalse(TEXT("Split packages are refused without mutation"), Split.bSuccess || Split.bModifiedInMemory);
    TestTrue(TEXT("Refusal identifies the single-file constraint"), Split.Message.Contains(TEXT("single-file")));
    TestTrue(TEXT("Refused writes leave original bytes intact"), Wooloo::Assets::WriteGuard::Unchanged(Filename, Hash, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooPreviewProductionTest, "Wooloo.Assets.Production.PreviewKeepsPrivateBonesAndDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooPreviewProductionTest::RunTest(const FString& Parameters)
{
    FProductionFixture Fixture;
    const auto SkeletonPath = Fixture.Root + TEXT("/SKEL_Public");
    auto* Skeleton = NewObject<USkeleton>(CreatePackage(*SkeletonPath), TEXT("SKEL_Public"), RF_Public | RF_Standalone);
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
    }
    Fixture.Track(Skeleton);
    const auto MeshPath = Fixture.Root + TEXT("/SKM_Module");
    auto* Mesh = NewObject<USkeletalMesh>(CreatePackage(*MeshPath), TEXT("SKM_Module"), RF_Public | RF_Standalone);
    {
        FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
        Modifier.Add(FMeshBoneInfo(TEXT("Private"), TEXT("Private"), 0), FTransform(FVector(0., 0., 1.)));
    }
    Fixture.Track(Mesh);
    const auto PrivatePath = Fixture.Root + TEXT("/SKEL_Private");
    auto* PrivateSkeleton = NewObject<USkeleton>(CreatePackage(*PrivatePath), TEXT("SKEL_Private"), RF_Public | RF_Standalone);
    {
        FReferenceSkeletonModifier Modifier(PrivateSkeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
        Modifier.Add(FMeshBoneInfo(TEXT("Private"), TEXT("Private"), 0), FTransform(FVector(0., 0., 1.)));
    }
    Mesh->SetSkeleton(PrivateSkeleton);
    Fixture.Track(PrivateSkeleton);
    const auto Path = Fixture.Root + TEXT("/PMC_Module");
    const auto Result = UWoolooAssetToolset::CreatePreviewMeshCollection(Path, SkeletonPath,
        {MeshPath}, Fixture.Root, false, TEXT(""));
    auto* Collection = LoadObject<UPreviewMeshCollection>(nullptr, *(Path + TEXT(".PMC_Module")));
    Fixture.Track(Collection);
    if (!TestTrue(TEXT("Create PMC with a private-bone module"), Result.bSuccess)) return false;
    TestEqual(TEXT("Public Skeleton never acquires the module private bone"), Skeleton->GetReferenceSkeleton().GetNum(), 1);
    TestTrue(TEXT("Module retains its own Skeleton"), Mesh->GetSkeleton() == PrivateSkeleton);
    TestTrue(TEXT("Explicit Skeleton binding is saved"), Collection && Collection->Skeleton == Skeleton);
    TestNull(TEXT("No implicit default preview change"), Skeleton->GetAdditionalPreviewSkeletalMeshes());
    TestFalse(TEXT("Duplicate PMC cannot replace user settings"), UWoolooAssetToolset::CreatePreviewMeshCollection(
        Path, SkeletonPath, {MeshPath}, Fixture.Root, false, TEXT("")).bSuccess);
    const auto Inspection = UWoolooAssetToolset::InspectAsset(Path, {}, {});
    TestTrue(TEXT("Inspection succeeds without dirtying the PMC"), Inspection.bSuccess && !Collection->GetOutermost()->IsDirty());
    TestFalse(TEXT("Reject sampling a non-animation"), UWoolooAssetToolset::InspectAsset(Path, {0.f}, {}).bSuccess);
    const auto SecondPath = Fixture.Root + TEXT("/SKM_Second");
    auto* Second = DuplicateObject<USkeletalMesh>(Mesh, CreatePackage(*SecondPath), TEXT("SKM_Second"));
    Fixture.Track(Second);
    for (const auto& Asset : Fixture.Assets) Asset->GetOutermost()->SetDirtyFlag(false);
    FString Hash, Error;
    const auto Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
    if (!TestTrue(TEXT("Fingerprint saved PMC"), Wooloo::Assets::WriteGuard::HashFile(Filename, Hash, Error))) return false;
    TestFalse(TEXT("Empty update cannot erase the outfit"), UWoolooAssetToolset::UpdatePreviewMeshCollection(
        Path, {}, Fixture.Root, Hash).bSuccess);
    TestFalse(TEXT("Duplicate update is refused"), UWoolooAssetToolset::UpdatePreviewMeshCollection(
        Path, {MeshPath, MeshPath}, Fixture.Root, Hash).bSuccess);
    TestFalse(TEXT("Stale PMC hash is refused"), UWoolooAssetToolset::UpdatePreviewMeshCollection(
        Path, {SecondPath}, Fixture.Root, FString::ChrN(64, TEXT('0'))).bSuccess);
    const auto Update = UWoolooAssetToolset::UpdatePreviewMeshCollection(Path, {SecondPath}, Fixture.Root, Hash);
    if (!TestTrue(TEXT("Exact PMC update succeeds"), Update.bSuccess)) return false;
    TestTrue(TEXT("Only the PMC is saved"), Update.SavedPackages == TArray<FString>{Path});
    TestEqual(TEXT("Existing entry is replaced by the full requested list"), Collection->SkeletalMeshes.Num(), 1);
    TestTrue(TEXT("New entry retains private Skeleton"), Collection->SkeletalMeshes[0].SkeletalMesh.Get() == Second
        && Second->GetSkeleton() == PrivateSkeleton);
    TestTrue(TEXT("Public Skeleton remains clean and unchanged"), !Skeleton->GetOutermost()->IsDirty()
        && Skeleton->GetReferenceSkeleton().GetNum() == 1 && !Skeleton->GetAdditionalPreviewSkeletalMeshes());
    const auto Backup = Update.BackupDirectory / FPaths::GetCleanFilename(Filename);
    Fixture.BackupFiles.Add(Backup);
    TestTrue(TEXT("Backup has exactly the reviewed original bytes"), Wooloo::Assets::WriteGuard::Unchanged(Backup, Hash, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooReimportSlotsTest, "Wooloo.Assets.Production.ExplicitDefaultMaterialSlot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooReimportSlotsTest::RunTest(const FString& Parameters)
{
    FProductionFixture Fixture;
    const auto Path = Fixture.Root + TEXT("/SKM_Slots");
    auto* Mesh = NewObject<USkeletalMesh>(CreatePackage(*Path), TEXT("SKM_Slots"), RF_Public | RF_Standalone);
    Fixture.Track(Mesh);
    auto* Data = NewObject<UFbxSkeletalMeshImportData>(Mesh);
    Mesh->SetAssetImportData(Data);
    Mesh->GetMaterials().Emplace(nullptr, true, false, NAME_None, NAME_None);
    Data->ImportMaterialOriginalNameData.Add(NAME_None);
    Data->ImportMeshLodData.AddDefaulted();
    Data->ImportMeshLodData[0].SectionOriginalMaterialName.Add(NAME_None);
    FString Error;
    const auto Requested = FName(TEXT("RequestedSlot"));
    TestTrue(TEXT("An explicit name binds the sole unnamed native FBX default slot"),
        Wooloo::Assets::ReimportDetails::BindMaterials(Mesh, {{NAME_None, nullptr}}, {{NAME_None, Requested}}, Error));
    TestEqual(TEXT("Display name comes from the parameter"), Mesh->GetMaterials()[0].MaterialSlotName, Requested);
    TestEqual(TEXT("Imported slot name comes from the same parameter"), Mesh->GetMaterials()[0].ImportedMaterialSlotName, Requested);
    TestNull(TEXT("Explicit empty material stays empty"), Mesh->GetMaterials()[0].MaterialInterface.Get());
    TestEqual(TEXT("Native material history matches"), Data->ImportMaterialOriginalNameData[0], Requested);
    TestEqual(TEXT("Native section history matches"), Data->ImportMeshLodData[0].SectionOriginalMaterialName[0], Requested);
    TestFalse(TEXT("Unknown rename source is refused"), Wooloo::Assets::ReimportDetails::BindMaterials(
        Mesh, {{Requested, nullptr}}, {{TEXT("Unknown"), TEXT("Other")}}, Error));
    TestEqual(TEXT("Failed rename cannot partially mutate slots"), Mesh->GetMaterials()[0].MaterialSlotName, Requested);
    Mesh->GetMaterials().Emplace(nullptr, true, false, TEXT("Second"), TEXT("Second"));
    TestFalse(TEXT("Final names cannot collide"), Wooloo::Assets::ReimportDetails::BindMaterials(Mesh,
        {{Requested, nullptr}, {TEXT("Second"), nullptr}}, {{Requested, TEXT("Second")}}, Error));
    Mesh->GetMaterials()[0].MaterialSlotName = NAME_None;
    Mesh->GetMaterials()[0].ImportedMaterialSlotName = NAME_None;
    TestFalse(TEXT("None is not a wildcard for multi-slot meshes"), Wooloo::Assets::ReimportDetails::BindMaterials(Mesh,
        {{NAME_None, nullptr}, {TEXT("Second"), nullptr}}, {{NAME_None, Requested}}, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooPreviewSoftPathTest, "Wooloo.Assets.Production.UnloadedPreviewPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooPreviewSoftPathTest::RunTest(const FString& Parameters)
{
    FProductionFixture Fixture;
    const auto Path = Fixture.Root + TEXT("/SKEL_Preview");
    auto* Skeleton = NewObject<USkeleton>(CreatePackage(*Path), TEXT("SKEL_Preview"), RF_Public | RF_Standalone);
    Fixture.Track(Skeleton);
    const auto MeshPath = Fixture.Root + TEXT("/SKM_NotLoaded");
    const auto PMCPath = Fixture.Root + TEXT("/PMC_NotLoaded");
    auto* Preview = FindFProperty<FSoftObjectProperty>(USkeleton::StaticClass(), TEXT("PreviewSkeletalMesh"));
    auto* Additional = FindFProperty<FSoftObjectProperty>(USkeleton::StaticClass(), TEXT("AdditionalPreviewSkeletalMeshes"));
    if (!TestNotNull(TEXT("Native preview property"), Preview) || !TestNotNull(TEXT("Native PMC property"), Additional)) return false;
    Preview->SetPropertyValue_InContainer(Skeleton, FSoftObjectPtr(FSoftObjectPath(MeshPath + TEXT(".SKM_NotLoaded"))));
    Additional->SetPropertyValue_InContainer(Skeleton, FSoftObjectPtr(FSoftObjectPath(PMCPath + TEXT(".PMC_NotLoaded"))));
    Skeleton->GetOutermost()->SetDirtyFlag(false);
    const auto Inspection = UWoolooAssetToolset::InspectAsset(Path, {}, {});
    TSharedPtr<FJsonObject> Data;
    if (!TestTrue(TEXT("Inspect soft paths without loading them"), Inspection.bSuccess
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Inspection.Json), Data))) return false;
    TestEqual(TEXT("Reports serialized main path"), Data->GetStringField(TEXT("preview_mesh")), MeshPath);
    TestEqual(TEXT("Reports serialized PMC path"), Data->GetStringField(TEXT("preview_collection")), PMCPath);
    TestNull(TEXT("Preview cache remains unresolved"), Preview->GetPropertyValue_InContainer(Skeleton).Get());
    TestNull(TEXT("Collection cache remains unresolved"), Additional->GetPropertyValue_InContainer(Skeleton).Get());
    TestFalse(TEXT("Inspection never dirties Skeleton"), Skeleton->GetOutermost()->IsDirty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWoolooAnimationSamplesTest, "Wooloo.Assets.Production.RawAndCompressedAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWoolooAnimationSamplesTest::RunTest(const FString& Parameters)
{
    FProductionFixture Fixture;
    const auto SkeletonPath = Fixture.Root + TEXT("/SKEL_Animation");
    auto* Skeleton = NewObject<USkeleton>(CreatePackage(*SkeletonPath), TEXT("SKEL_Animation"), RF_Public | RF_Standalone);
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
        Modifier.Add(FMeshBoneInfo(TEXT("Unanimated"), TEXT("Unanimated"), 0), FTransform(FVector(0., 3., 0.)));
    }
    Fixture.Track(Skeleton);
    const auto Path = Fixture.Root + TEXT("/AS_Samples");
    auto* Sequence = NewObject<UAnimSequence>(CreatePackage(*Path), TEXT("AS_Samples"), RF_Public | RF_Standalone);
    Fixture.Track(Sequence);
    Sequence->SetSkeleton(Skeleton);
    auto& Controller = Sequence->GetController();
    Controller.InitializeModel();
    Controller.OpenBracket(FText::FromString(TEXT("Generate animation inspection fixture")), false);
    Controller.SetFrameRate(FFrameRate(30, 1), false);
    Controller.SetNumberOfFrames(FFrameNumber(2), false);
    Controller.AddBoneCurve(TEXT("Root"), false);
    Controller.SetBoneTrackKeys(TEXT("Root"),
        TArray<FVector3f>{FVector3f::ZeroVector, FVector3f(10.f, 0.f, 0.f), FVector3f(20.f, 0.f, 0.f)},
        TArray<FQuat4f>{FQuat4f::Identity, FQuat4f::Identity, FQuat4f::Identity},
        TArray<FVector3f>{FVector3f::OneVector, FVector3f::OneVector, FVector3f::OneVector}, false);
    Controller.CloseBracket(false);
    // Fixture authoring explicitly builds native compressed data. The inspection must not do so.
    Sequence->CacheDerivedDataForCurrentPlatform();
    Sequence->WaitOnExistingCompression();
    Sequence->GetOutermost()->SetDirtyFlag(false);
    const auto* Settings = Sequence->BoneCompressionSettings.Get();
    const auto Result = UWoolooAssetToolset::InspectAnimationSamples(Path, {1.f / 30.f}, {});
    TSharedPtr<FJsonObject> Data;
    if (!TestTrue(TEXT("Inspect native raw and compressed fixture"), Result.bSuccess
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Json), Data))) return false;
    const auto Poses = Data->GetArrayField(TEXT("samples"))[0]->AsObject()->GetObjectField(TEXT("poses"));
    const auto Root = Poses->GetObjectField(TEXT("Root"));
    const auto RawX = Root->GetObjectField(TEXT("raw_local"))->GetArrayField(TEXT("translation"))[0]->AsNumber();
    const auto CompressedX = Root->GetObjectField(TEXT("compressed_local"))->GetArrayField(TEXT("translation"))[0]->AsNumber();
    TestTrue(TEXT("Raw model is sampled at the requested frame"), FMath::Abs(RawX - 10.) < .0001);
    TestTrue(TEXT("Compressed model is sampled at the requested frame"), FMath::Abs(CompressedX - 10.) < .01);
    const auto Missing = Poses->GetObjectField(TEXT("Unanimated"));
    TestFalse(TEXT("Missing track is explicit"), Missing->GetBoolField(TEXT("has_raw_track")));
    TestEqual(TEXT("Missing track retains reference translation"),
        Missing->GetObjectField(TEXT("raw_local"))->GetArrayField(TEXT("translation"))[1]->AsNumber(), 3.);
    TestTrue(TEXT("Component output includes animated parent"), FMath::Abs(
        Missing->GetObjectField(TEXT("raw_component"))->GetArrayField(TEXT("translation"))[0]->AsNumber() - 10.) < .0001);
    TestFalse(TEXT("Unknown bone is refused"), UWoolooAssetToolset::InspectAnimationSamples(Path, {0.f}, {TEXT("Unknown")}).bSuccess);
    TestFalse(TEXT("Outside time is refused"), UWoolooAssetToolset::InspectAnimationSamples(Path, {-1.f}, {}).bSuccess);
    TestFalse(TEXT("Duplicate bones are refused"), UWoolooAssetToolset::InspectAnimationSamples(Path, {0.f}, {TEXT("Root"), TEXT("Root")}).bSuccess);
    TestTrue(TEXT("No settings, frame count or dirty flag changed"), Settings == Sequence->BoneCompressionSettings.Get()
        && Sequence->GetDataModel()->GetNumberOfFrames() == 2 && !Sequence->GetOutermost()->IsDirty());
    return true;
}

#endif
