#include "WoolooAssets/Assets/ImportService.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace Wooloo::Assets
{
namespace Production
{
FString ObjectPath(const FString& Package)
{
    return Package + TEXT(".") + FPackageName::GetShortName(Package);
}

bool Ready(FString& Error)
{
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
    {
        Error = TEXT("Requires the editor game thread with PIE stopped.");
        return false;
    }
    if (FAssetRegistryModule::GetRegistry().IsLoadingAssets())
    {
        Error = TEXT("Wait for Asset Registry discovery to finish.");
        return false;
    }
    return true;
}

bool Scope(const FString& Package, const FString& AllowedRoot, FString& Error)
{
    auto Root = AllowedRoot;
    Root.RemoveFromEnd(TEXT("/"));
    FString Filename;
    if (!FPackageName::IsValidLongPackageName(Root)
        || Root.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1) == INDEX_NONE
        || !FPackageName::IsValidLongPackageName(Package)
        || !Package.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase)
        || !FPackageName::TryConvertLongPackageNameToFilename(Package, Filename)
        || FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),
            FPaths::ConvertRelativePathToFull(FPaths::EngineDir())))
    {
        Error = TEXT("Destination must be inside a narrow mounted AllowedPackageRoot, outside engine content.");
        return false;
    }
    return true;
}

bool Unoccupied(const FString& Package, FString& Error)
{
    if (FPackageName::DoesPackageExist(Package) || FindPackage(nullptr, *Package))
    {
        Error = TEXT("Destination already exists on disk or in memory: ") + Package;
        return false;
    }
    return true;
}

bool Save(UObject* Asset, FString& Error)
{
    const auto Filename = FPackageName::LongPackageNameToFilename(
        Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))
    {
        Error = TEXT("Failed to save ") + Asset->GetOutermost()->GetName();
        return false;
    }
    return true;
}

bool Compatible(const USkeleton* Skeleton, const USkeletalMesh* Mesh, FString& Error)
{
    const auto& Public = Skeleton->GetReferenceSkeleton();
    const auto& Part = Mesh->GetRefSkeleton();
    auto Shared = 0;
    for (auto Index = 0; Index < Part.GetNum(); ++Index)
    {
        const auto Name = Part.GetBoneName(Index);
        const auto PublicIndex = Public.FindBoneIndex(Name);
        if (PublicIndex == INDEX_NONE) continue;
        ++Shared;
        const auto Parent = Part.GetParentIndex(Index);
        const auto PublicParent = Public.GetParentIndex(PublicIndex);
        if ((Parent == INDEX_NONE) != (PublicParent == INDEX_NONE)
            || (Parent != INDEX_NONE && Part.GetBoneName(Parent) != Public.GetBoneName(PublicParent)))
        {
            Error = TEXT("Public parent chain differs at ") + Name.ToString();
            return false;
        }
    }
    if (Shared == 0) Error = TEXT("The mesh has no bones from the public Skeleton.");
    return Shared > 0;
}
}

namespace
{
bool SameReference(const FReferenceSkeleton& A, const FReferenceSkeleton& B)
{
    if (A.GetNum() != B.GetNum()) return false;
    for (auto Index = 0; Index < A.GetNum(); ++Index)
        if (A.GetBoneName(Index) != B.GetBoneName(Index) || A.GetParentIndex(Index) != B.GetParentIndex(Index)
            || !A.GetRefBonePose()[Index].Equals(B.GetRefBonePose()[Index], 1.e-6)) return false;
    return true;
}
}

FWoolooImportResult Import(const FString& SourceFilename, const FString& DestinationPackage,
    EWoolooImportKind Kind, const FString& AllowedPackageRoot, const FString& SkeletonPackage,
    const FString& NewSkeletonPackage, const TMap<FName, FString>& MaterialOverrides, bool bSRGB)
{
    FWoolooImportResult Out;
    Out.AssetPath = DestinationPackage;
    if (Kind != EWoolooImportKind::SkeletalMesh && Kind != EWoolooImportKind::Animation
        && Kind != EWoolooImportKind::Texture2D)
    {
        Out.Message = TEXT("Unsupported import kind.");
        return Out;
    }
    if (!Production::Ready(Out.Message) || !Production::Scope(DestinationPackage, AllowedPackageRoot, Out.Message)
        || !Production::Unoccupied(DestinationPackage, Out.Message)) return Out;
    const auto Extension = FPaths::GetExtension(SourceFilename).ToLower();
    const auto bTexture = Kind == EWoolooImportKind::Texture2D;
    if (FPaths::IsRelative(SourceFilename) || !FPaths::FileExists(SourceFilename)
        || (bTexture ? !TArray<FString>{TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("tga")}.Contains(Extension)
                     : Extension != TEXT("fbx")))
    {
        Out.Message = TEXT("Supply an existing absolute FBX or PNG/JPG/TGA filename for the selected kind.");
        return Out;
    }
    if ((bTexture && (!SkeletonPackage.IsEmpty() || !NewSkeletonPackage.IsEmpty() || !MaterialOverrides.IsEmpty()))
        || (Kind == EWoolooImportKind::Animation && (SkeletonPackage.IsEmpty() || !NewSkeletonPackage.IsEmpty() || !MaterialOverrides.IsEmpty()))
        || (Kind == EWoolooImportKind::SkeletalMesh && (SkeletonPackage.IsEmpty() == NewSkeletonPackage.IsEmpty())))
    {
        Out.Message = TEXT("Meshes require exactly one existing/new Skeleton; animations require an existing Skeleton; texture inputs must be texture-only.");
        return Out;
    }
    TArray<UPackage*> Dirty;
    FEditorFileUtils::GetDirtyContentPackages(Dirty);
    FEditorFileUtils::GetDirtyWorldPackages(Dirty);
    if (!Dirty.IsEmpty())
    {
        Out.Message = TEXT("Save or discard existing editor changes before importing assets.");
        return Out;
    }
    if (!NewSkeletonPackage.IsEmpty()
        && (!Production::Scope(NewSkeletonPackage, AllowedPackageRoot, Out.Message)
            || !Production::Unoccupied(NewSkeletonPackage, Out.Message)
            || !Production::Unoccupied(DestinationPackage + TEXT("_Skeleton"), Out.Message))) return Out;
    if (NewSkeletonPackage == DestinationPackage)
    {
        Out.Message = TEXT("Mesh and Skeleton packages must differ.");
        return Out;
    }
    auto* Existing = SkeletonPackage.IsEmpty() ? nullptr : LoadObject<USkeleton>(nullptr, *Production::ObjectPath(SkeletonPackage));
    if (!SkeletonPackage.IsEmpty() && (!Existing || Existing->GetOutermost()->IsDirty()))
    {
        Out.Message = TEXT("The existing Skeleton must exist and be clean.");
        return Out;
    }
    TMap<FName, UMaterialInterface*> Materials;
    for (const auto& Entry : MaterialOverrides)
    {
        auto* Material = LoadObject<UMaterialInterface>(nullptr, *Production::ObjectPath(Entry.Value));
        if (Entry.Key.IsNone() || !Material)
        {
            Out.Message = TEXT("Every material override must name an imported slot and an existing material.");
            return Out;
        }
        Materials.Add(Entry.Key, Material);
    }
    // The native FBX importer may merge bones and set preview state. Give it a
    // transient copy, then accept the result only if that complete tree stayed unchanged.
    TStrongObjectPtr<USkeleton> Probe(Existing ? DuplicateObject<USkeleton>(Existing, GetTransientPackage()) : nullptr);
    if (Probe.IsValid())
    {
        Probe->ClearFlags(RF_Public | RF_Standalone);
        Probe->SetFlags(RF_Transient);
    }
    TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
    Task->Filename = SourceFilename;
    Task->DestinationPath = FPackageName::GetLongPackagePath(DestinationPackage);
    Task->DestinationName = FPackageName::GetShortName(DestinationPackage);
    Task->bAutomated = true;
    Task->bReplaceExisting = false;
    Task->bReplaceExistingSettings = false;
    Task->bSave = false;
    Task->bAsync = false;
    if (bTexture)
    {
        Task->Factory = NewObject<UTextureFactory>();
    }
    else
    {
        auto* Options = NewObject<UFbxImportUI>(Task.Get());
        Options->bAutomatedImportShouldDetectType = false;
        Options->bImportAsSkeletal = true;
        Options->bOverrideFullName = true;
        Options->MeshTypeToImport = Kind == EWoolooImportKind::Animation ? FBXIT_Animation : FBXIT_SkeletalMesh;
        Options->OriginalImportType = Options->MeshTypeToImport;
        Options->bImportMesh = Kind == EWoolooImportKind::SkeletalMesh;
        Options->bImportAnimations = Kind == EWoolooImportKind::Animation;
        Options->OverrideAnimationName = Task->DestinationName;
        Options->bImportMaterials = false;
        Options->bImportTextures = false;
        Options->bCreatePhysicsAsset = false;
        Options->Skeleton = Probe.Get();
        auto* MeshOptions = Options->SkeletalMeshImportData.Get();
        MeshOptions->bConvertScene = true;
        MeshOptions->bConvertSceneUnit = true;
        MeshOptions->bForceFrontXAxis = false;
        MeshOptions->bUseT0AsRefPose = false;
        MeshOptions->bUpdateSkeletonReferencePose = false;
        MeshOptions->bImportMeshesInBoneHierarchy = false;
        MeshOptions->ImportTranslation = FVector::ZeroVector;
        MeshOptions->ImportRotation = FRotator::ZeroRotator;
        MeshOptions->ImportUniformScale = 1.f;
        MeshOptions->NormalImportMethod = FBXNIM_ImportNormalsAndTangents;
        auto* AnimOptions = Options->AnimSequenceImportData.Get();
        AnimOptions->bConvertScene = true;
        AnimOptions->bConvertSceneUnit = true;
        AnimOptions->bForceFrontXAxis = false;
        AnimOptions->bUseDefaultSampleRate = false;
        AnimOptions->ImportTranslation = FVector::ZeroVector;
        AnimOptions->ImportRotation = FRotator::ZeroRotator;
        AnimOptions->ImportUniformScale = 1.f;
        AnimOptions->CustomSampleRate = 0;
        AnimOptions->AnimationLength = FBXALIT_ExportedTime;
        AnimOptions->bPreserveLocalTransform = true;
        AnimOptions->bImportBoneTracks = true;
        AnimOptions->bImportMeshesInBoneHierarchy = false;
        AnimOptions->bImportCustomAttribute = false;
        AnimOptions->bSnapToClosestFrameBoundary = false;
        Task->Options = Options;
        Task->Factory = NewObject<UFbxFactory>();
    }
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    Tools.ImportAssetTasks({Task.Get()});
    auto* Asset = FindObject<UObject>(nullptr, *Production::ObjectPath(DestinationPackage));
    for (auto* Object : Task->GetObjects())
        if (Object && Object->GetOutermost() != GetTransientPackage()) Out.CreatedPackages.AddUnique(Object->GetOutermost()->GetName());
    if (!Asset)
    {
        Out.Message = TEXT("Native factory did not produce the exact requested asset.");
        return Out;
    }
    Out.CreatedPackages.AddUnique(DestinationPackage);
    if (Existing && (!SameReference(Existing->GetReferenceSkeleton(), Probe->GetReferenceSkeleton()) || Existing->GetOutermost()->IsDirty()))
    {
        Out.Message = TEXT("FBX requires a different Skeleton tree/reference; use an independent Skeleton. New imported objects remain unsaved.");
        return Out;
    }
    auto* Mesh = Cast<USkeletalMesh>(Asset);
    auto* Animation = Cast<UAnimSequence>(Asset);
    auto* Texture = Cast<UTexture2D>(Asset);
    if ((bTexture && !Texture) || (Kind == EWoolooImportKind::SkeletalMesh && !Mesh)
        || (Kind == EWoolooImportKind::Animation && !Animation))
    {
        Out.Message = TEXT("Native factory returned an unexpected asset type.");
        return Out;
    }
    USkeleton* CreatedSkeleton = nullptr;
    if (Mesh)
    {
        if (Existing)
        {
            if (!Production::Compatible(Existing, Mesh, Out.Message)) return Out;
            Mesh->SetSkeleton(Existing);
        }
        else
        {
            CreatedSkeleton = Mesh->GetSkeleton();
            if (!CreatedSkeleton || CreatedSkeleton->GetOutermost()->GetName() != DestinationPackage + TEXT("_Skeleton"))
            {
                Out.Message = TEXT("Factory produced an unexpected Skeleton package; refusing further writes.");
                return Out;
            }
            Out.CreatedPackages.AddUnique(CreatedSkeleton->GetOutermost()->GetName());
            if (CreatedSkeleton->GetOutermost()->GetName() != NewSkeletonPackage)
            {
                // This factory-owned Skeleton has never been saved. Duplicate its
                // native data to the exact requested package before publishing it,
                // avoiding redirectors and unrelated referencer/source-control saves.
                auto* TemporarySkeleton = CreatedSkeleton;
                auto* TemporaryPackage = TemporarySkeleton->GetOutermost();
                CreatedSkeleton = DuplicateObject<USkeleton>(TemporarySkeleton,
                    CreatePackage(*NewSkeletonPackage), FName(*FPackageName::GetShortName(NewSkeletonPackage)));
                if (!CreatedSkeleton)
                {
                    Out.Message = TEXT("Could not create the requested Skeleton package.");
                    return Out;
                }
                CreatedSkeleton->ClearFlags(RF_Transient);
                CreatedSkeleton->SetFlags(RF_Public | RF_Standalone);
                FAssetRegistryModule::AssetCreated(CreatedSkeleton);
                Mesh->SetSkeleton(CreatedSkeleton);
                FAssetRegistryModule::AssetDeleted(TemporarySkeleton);
                TemporarySkeleton->ClearFlags(RF_Public | RF_Standalone);
                TemporarySkeleton->SetFlags(RF_Transient);
                TemporaryPackage->SetDirtyFlag(false);
                Out.CreatedPackages.Remove(TemporaryPackage->GetName());
            }
            Out.CreatedPackages.AddUnique(NewSkeletonPackage);
        }
        if (!Materials.IsEmpty())
        {
            TSet<FName> Used;
            for (auto& Slot : Mesh->GetMaterials())
            {
                const auto* Override = Materials.Find(Slot.ImportedMaterialSlotName);
                if (!Override)
                {
                    Out.Message = TEXT("An imported material slot has no explicit override: ") + Slot.ImportedMaterialSlotName.ToString();
                    return Out;
                }
                Slot.MaterialInterface = *Override;
                Used.Add(Slot.ImportedMaterialSlotName);
            }
            if (Used.Num() != Materials.Num())
            {
                Out.Message = TEXT("MaterialOverrides contains unknown imported slot names.");
                return Out;
            }
        }
        Mesh->PostEditChange();
        Out.SkeletonPath = Mesh->GetSkeleton()->GetOutermost()->GetName();
    }
    if (Animation)
    {
        Animation->SetSkeleton(Existing);
        Animation->PostEditChange();
        Out.SkeletonPath = SkeletonPackage;
    }
    if (Texture)
    {
        Texture->SRGB = bSRGB;
        Texture->PostEditChange();
    }
    if (CreatedSkeleton)
    {
        if (!Production::Save(CreatedSkeleton, Out.Message)) return Out;
        Out.SavedPackages.Add(NewSkeletonPackage);
    }
    if (!Production::Save(Asset, Out.Message)) return Out;
    Out.SavedPackages.Add(DestinationPackage);
    Out.bSuccess = true;
    Out.Message = TEXT("Imported and saved the requested new asset.");
    return Out;
}
}
