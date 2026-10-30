#include "WoolooAssets/Assets/ReimportService.h"
#include "WoolooAssets/Assets/AssetWriteGuard.h"
#include "WoolooAssets/Assets/ImportService.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "AssetImportTask.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/ReimportFbxAnimSequenceFactory.h"
#include "Factories/ReimportFbxSkeletalMeshFactory.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace Wooloo::Assets
{
namespace ReimportDetails
{
bool BindMaterials(USkeletalMesh* Mesh, const TMap<FName, UMaterialInterface*>& Materials,
    const TMap<FName, FName>& Renames, FString& Error)
{
    const auto& Slots = Mesh->GetMaterials();
    if (Slots.Num() != Materials.Num())
    {
        Error = TEXT("MaterialOverrides must cover exactly the new FBX material slots.");
        return false;
    }
    TSet<FName> Sources, FinalImported, FinalDisplay;
    for (const auto& Slot : Slots)
    {
        const auto Source = Slot.ImportedMaterialSlotName;
        if ((Source.IsNone() && Slots.Num() != 1) || Sources.Contains(Source) || !Materials.Contains(Source))
        {
            Error = TEXT("Missing/ambiguous imported slot mapping: ") + Source.ToString();
            return false;
        }
        Sources.Add(Source);
        const auto* Rename = Renames.Find(Source);
        const auto Imported = Rename ? *Rename : Source;
        const auto Display = Rename ? *Rename : Slot.MaterialSlotName;
        if ((Rename && Rename->IsNone()) || FinalImported.Contains(Imported) || FinalDisplay.Contains(Display))
        {
            Error = TEXT("Explicit material slot names must be non-empty and cannot collide.");
            return false;
        }
        FinalImported.Add(Imported);
        FinalDisplay.Add(Display);
    }
    for (const auto& Rename : Renames)
    {
        if (Sources.Contains(Rename.Key)) continue;
        Error = TEXT("MaterialSlotRenames contains an unknown imported slot: ") + Rename.Key.ToString();
        return false;
    }
    for (auto& Slot : Mesh->GetMaterials())
    {
        const auto Source = Slot.ImportedMaterialSlotName;
        Slot.MaterialInterface = Materials.FindChecked(Source);
        if (const auto* Rename = Renames.Find(Source))
        {
            Slot.MaterialSlotName = *Rename;
            Slot.ImportedMaterialSlotName = *Rename;
        }
    }
    if (auto* Data = Cast<UFbxSkeletalMeshImportData>(Mesh->GetAssetImportData()))
    {
        // Keep native section/material history aligned with the explicitly renamed slots.
        for (auto& Name : Data->ImportMaterialOriginalNameData)
            if (const auto* Rename = Renames.Find(Name)) Name = *Rename;
        for (auto& LOD : Data->ImportMeshLodData)
            for (auto& Name : LOD.SectionOriginalMaterialName)
                if (const auto* Rename = Renames.Find(Name)) Name = *Rename;
    }
    return true;
}
}

namespace
{
USkeleton* ReimportSkeleton(UObject* Asset)
{
    if (auto* Mesh = Cast<USkeletalMesh>(Asset)) return Mesh->GetSkeleton();
    if (auto* Sequence = Cast<UAnimSequence>(Asset)) return Sequence->GetSkeleton();
    return nullptr;
}

void BindReimportSkeleton(UObject* Asset, USkeleton* Skeleton)
{
    if (auto* Mesh = Cast<USkeletalMesh>(Asset)) Mesh->SetSkeleton(Skeleton);
    else if (auto* Sequence = Cast<UAnimSequence>(Asset)) Sequence->SetSkeleton(Skeleton);
}

UAssetImportData* ReimportData(UObject* Asset)
{
    const auto* Property = FindFProperty<FObjectPropertyBase>(Asset->GetClass(), TEXT("AssetImportData"));
    return Property ? Cast<UAssetImportData>(Property->GetObjectPropertyValue_InContainer(Asset)) : nullptr;
}

bool SameReimportReference(const FReferenceSkeleton& A, const FReferenceSkeleton& B)
{
    if (A.GetNum() != B.GetNum()) return false;
    for (auto Index = 0; Index < A.GetNum(); ++Index)
        if (A.GetBoneName(Index) != B.GetBoneName(Index) || A.GetParentIndex(Index) != B.GetParentIndex(Index)
            || !A.GetRefBonePose()[Index].Equals(B.GetRefBonePose()[Index], 1.e-6)) return false;
    return true;
}

bool ReimportAnimationSettingsMatch(const UAnimSequence* A, const UAnimSequence* B, FString& Error)
{
    for (const auto* Name : {TEXT("BoneCompressionSettings"), TEXT("CurveCompressionSettings"),
        TEXT("CompressionErrorThresholdScale"), TEXT("bAllowFrameStripping"), TEXT("Interpolation"),
        TEXT("bEnableRootMotion"), TEXT("RootMotionRootLock"), TEXT("bForceRootLock"),
        TEXT("bUseNormalizedRootMotionScale"), TEXT("RetargetSource")})
    {
        const auto* Property = FindFProperty<FProperty>(A->GetClass(), Name);
        if (Property && Property->Identical_InContainer(A, B)) continue;
        Error = TEXT("Reimport changed an animation playback/compression setting, or its native property is unavailable: ") + FString(Name);
        return false;
    }
    return true;
}

bool OrdinaryReimportTarget(UObject* Asset, FString& Error)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Asset->GetOutermost(), Objects, EGetObjectsFlags::None);
    for (const auto* Object : Objects)
    {
        if (Object == Asset || !Object->IsAsset()) continue;
        Error = TEXT("The target package contains another asset; refusing a package-wide overwrite.");
        return false;
    }
    if (const auto* Mesh = Cast<USkeletalMesh>(Asset))
    {
        if (Mesh->IsCompiling())
        {
            Error = TEXT("Wait for the target mesh's native compilation to finish.");
            return false;
        }
        const auto* Model = Mesh->GetImportedModel();
        if (!Model || Model->LODModels.Num() != 1 || !Mesh->GetMorphTargets().IsEmpty()
            || !Mesh->GetMeshClothingAssets().IsEmpty() || !Mesh->GetSkinWeightProfiles().IsEmpty()
            || Mesh->HasCustomActorReimportFactory() || !Cast<UFbxSkeletalMeshImportData>(ReimportData(Asset)))
        {
            Error = TEXT("Only a single-LOD native FBX skeletal mesh without cloth, morphs, skin profiles or custom reimport is supported.");
            return false;
        }
        return true;
    }
    if (const auto* Sequence = Cast<UAnimSequence>(Asset))
    {
        if (Sequence->IsValidAdditive() || !Sequence->IsDataModelValid()
            || !Sequence->IsBoneCompressedDataValid() || Sequence->IsCompressedDataOutOfDate()
            || !Cast<UFbxAnimSequenceImportData>(ReimportData(Asset)))
        {
            Error = TEXT("Animation reimport requires a ready non-additive native FBX sequence with its raw model.");
            return false;
        }
        return true;
    }
    Error = TEXT("Only native FBX SkeletalMesh and AnimSequence assets can be reimported.");
    return false;
}

bool NativeReimportIsolated(UObject* Asset, const FString& SourceFilename,
    const TMap<FName, UMaterialInterface*>& Materials, const TMap<FName, FName>& Renames, FString& Error)
{
    auto* OriginalSkeleton = ReimportSkeleton(Asset);
    TStrongObjectPtr<USkeleton> Probe(DuplicateObject<USkeleton>(OriginalSkeleton, GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), USkeleton::StaticClass())));
    if (!Probe.IsValid())
    {
        Error = TEXT("Could not isolate the existing Skeleton.");
        return false;
    }
    Probe->ClearFlags(RF_Public | RF_Standalone);
    Probe->SetFlags(RF_Transient);
    TStrongObjectPtr<UFbxFactory> Factory;
    FReimportHandler* Handler = nullptr;
    auto* Mesh = Cast<USkeletalMesh>(Asset);
    if (Mesh)
    {
        auto* NativeFactory = NewObject<UReimportFbxSkeletalMeshFactory>();
        Factory.Reset(NativeFactory);
        Handler = NativeFactory;
    }
    else
    {
        auto* NativeFactory = NewObject<UReimportFbxAnimSequenceFactory>();
        Factory.Reset(NativeFactory);
        Handler = NativeFactory;
    }
    auto* Manager = FReimportManager::Instance();
    ON_SCOPE_EXIT
    {
        Factory->SetAssetImportTask(nullptr);
        Manager->UnregisterHandler(*Handler);
    };
    TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
    Task->bAutomated = true;
    Task->bAsync = false;
    Task->bSave = false;
    auto* Options = NewObject<UFbxImportUI>(Task.Get());
    Options->bAutomatedImportShouldDetectType = false;
    Options->bImportMaterials = false;
    Options->bImportTextures = false;
    Options->bCreatePhysicsAsset = false;
    Options->bImportMesh = Mesh != nullptr;
    Options->bImportAnimations = Mesh == nullptr;
    Options->bImportAsSkeletal = true;
    Options->bResetToFbxOnMaterialConflict = true;
    Options->Skeleton = Probe.Get();
    Options->MeshTypeToImport = Mesh ? FBXIT_SkeletalMesh : FBXIT_Animation;
    Options->OriginalImportType = Options->MeshTypeToImport;
    if (Mesh)
    {
        auto* Data = CastChecked<UFbxSkeletalMeshImportData>(ReimportData(Asset));
        Options->SkeletalMeshImportData = Data;
        Data->bConvertScene = true;
        Data->bConvertSceneUnit = true;
        Data->bForceFrontXAxis = false;
        Data->bUseT0AsRefPose = false;
        Data->bUpdateSkeletonReferencePose = false;
        Data->bImportMeshesInBoneHierarchy = false;
        Data->ImportTranslation = FVector::ZeroVector;
        Data->ImportRotation = FRotator::ZeroRotator;
        Data->ImportUniformScale = 1.f;
        Data->NormalImportMethod = FBXNIM_ImportNormalsAndTangents;
        Data->ImportContentType = EFBXImportContentType::FBXICT_All;
    }
    else
    {
        auto* Data = CastChecked<UFbxAnimSequenceImportData>(ReimportData(Asset));
        Options->AnimSequenceImportData = Data;
        Data->bConvertScene = true;
        Data->bConvertSceneUnit = true;
        Data->bForceFrontXAxis = false;
        Data->bUseDefaultSampleRate = false;
        Data->CustomSampleRate = 0;
        Data->AnimationLength = FBXALIT_ExportedTime;
        Data->bPreserveLocalTransform = true;
        Data->bImportBoneTracks = true;
        Data->bImportMeshesInBoneHierarchy = false;
        Data->bImportCustomAttribute = false;
        Data->bSnapToClosestFrameBoundary = false;
        Data->ImportTranslation = FVector::ZeroVector;
        Data->ImportRotation = FRotator::ZeroRotator;
        Data->ImportUniformScale = 1.f;
    }
    Task->Options = Options;
    Factory->SetAssetImportTask(Task.Get());
    Handler->SetAutomatedReimport(true);
    BindReimportSkeleton(Asset, Probe.Get());
    Handler->SetReimportPaths(Asset, SourceFilename, 0);
    // The generic manager can migrate legacy import data to Interchange even
    // with a supplied handler. Invoke the explicit native FBX factory instead,
    // keeping this operation's reviewed options and synchronous result.
    const auto bImported = Handler->Reimport(Asset, 0) == EReimportResult::Succeeded;
    BindReimportSkeleton(Asset, OriginalSkeleton);
    if (!bImported || !SameReimportReference(OriginalSkeleton->GetReferenceSkeleton(), Probe->GetReferenceSkeleton()))
    {
        Error = TEXT("Native FBX reimport failed or tried to change the isolated Skeleton tree/reference.");
        return false;
    }
    if (Mesh)
    {
        if (!Production::Compatible(OriginalSkeleton, Mesh, Error)) return false;
        if (!ReimportDetails::BindMaterials(Mesh, Materials, Renames, Error)) return false;
    }
    Asset->PostEditChange();
    // This finishes only the reimported asset's native derived data, never C++ compilation.
    TArray<UObject*> ImportedObjects = {Asset};
    FAssetCompilingManager::Get().FinishCompilationForObjects(ImportedObjects);
    if (!Asset->HasAnyFlags(RF_Transient) && !Asset->GetOutermost()->HasAnyFlags(RF_Transient))
        GEditor->BroadcastObjectReimported(Asset);
    return true;
}
}

FWoolooReimportResult Reimport(const FString& AssetPath, const FString& SourceFilename,
    const FString& AllowedPackageRoot, const FString& ExpectedAssetSHA256,
    const TMap<FName, FString>& MaterialOverrides, const TMap<FName, FName>& MaterialSlotRenames)
{
    FWoolooReimportResult Out;
    Out.AssetPath = AssetPath;
    FString Filename, SourceHash;
    if (!WriteGuard::CleanEditor(Out.Message)
        || !WriteGuard::CheckTarget(AssetPath, AllowedPackageRoot, ExpectedAssetSHA256,
            Filename, Out.BeforeSHA256, Out.Message)) return Out;
    if (FPaths::IsRelative(SourceFilename) || !FPaths::FileExists(SourceFilename)
        || !FPaths::GetExtension(SourceFilename).Equals(TEXT("fbx"), ESearchCase::IgnoreCase))
    {
        Out.Message = TEXT("Supply an existing absolute FBX source filename.");
        return Out;
    }
    auto* Asset = LoadObject<UObject>(nullptr, *Production::ObjectPath(AssetPath));
    if (!Asset)
    {
        Out.Message = TEXT("The saved target asset could not be loaded.");
        return Out;
    }
    if (!OrdinaryReimportTarget(Asset, Out.Message)) return Out;
    auto* Skeleton = ReimportSkeleton(Asset);
    if (!Skeleton || !WriteGuard::CleanEditor(Out.Message))
    {
        if (Out.Message.IsEmpty()) Out.Message = TEXT("The existing asset requires a clean Skeleton.");
        return Out;
    }
    const auto* Animation = Cast<UAnimSequence>(Asset);
    if ((Animation && (!MaterialOverrides.IsEmpty() || !MaterialSlotRenames.IsEmpty()))
        || (!Animation && MaterialOverrides.IsEmpty()))
    {
        Out.Message = TEXT("Animation material/rename maps must be empty; meshes need a complete explicit imported-slot map.");
        return Out;
    }
    TMap<FName, UMaterialInterface*> Materials;
    for (const auto& Entry : MaterialOverrides)
    {
        auto* Material = Entry.Value.IsEmpty() ? nullptr
            : LoadObject<UMaterialInterface>(nullptr, *Production::ObjectPath(Entry.Value));
        if (!Entry.Value.IsEmpty() && !Material)
        {
            Out.Message = TEXT("Each material slot needs an existing Material package, or an explicit empty value.");
            return Out;
        }
        Materials.Add(Entry.Key, Material);
    }
    if (!WriteGuard::HashFile(SourceFilename, SourceHash, Out.Message)
        || !WriteGuard::CleanEditor(Out.Message)
        || !WriteGuard::Backup(Filename, Out.BeforeSHA256, TEXT("ReimportBackups"), Out.BackupDirectory, Out.Message)) return Out;
    const auto ScratchName = TEXT("/Temp/WoolooReimport/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> Scratch(CreatePackage(*ScratchName));
    Scratch->SetFlags(RF_Transient);
    TStrongObjectPtr<UObject> Preview(DuplicateObject(Asset, Scratch.Get(), Asset->GetFName()));
    if (!Preview.IsValid())
    {
        Out.Message = TEXT("Could not create a transient validation copy.");
        return Out;
    }
    Preview->ClearFlags(RF_Public | RF_Standalone);
    Preview->SetFlags(RF_Transient);
    ON_SCOPE_EXIT { Scratch->SetDirtyFlag(false); };
    if (!NativeReimportIsolated(Preview.Get(), SourceFilename, Materials, MaterialSlotRenames, Out.Message)
        || !OrdinaryReimportTarget(Preview.Get(), Out.Message)) return Out;
    Scratch->SetDirtyFlag(false);
    if (Animation && !ReimportAnimationSettingsMatch(Animation, CastChecked<UAnimSequence>(Preview.Get()), Out.Message)) return Out;
    if (!WriteGuard::CleanEditor(Out.Message)
        || !WriteGuard::Unchanged(SourceFilename, SourceHash, Out.Message)
        || !WriteGuard::Unchanged(Filename, Out.BeforeSHA256, Out.Message)) return Out;
    Out.bModifiedInMemory = true;
    Out.Warnings.Add(TEXT("The target was modified in memory. Original backup retained; inspect state before any retry. No rollback was attempted."));
    Asset->Modify();
    if (!NativeReimportIsolated(Asset, SourceFilename, Materials, MaterialSlotRenames, Out.Message)
        || !OrdinaryReimportTarget(Asset, Out.Message)) return Out;
    if (Animation && !ReimportAnimationSettingsMatch(Animation, CastChecked<UAnimSequence>(Preview.Get()), Out.Message)) return Out;
    if (!WriteGuard::OnlyTargetDirty(Asset, Out.Message)
        || !WriteGuard::Unchanged(SourceFilename, SourceHash, Out.Message)
        || !WriteGuard::Unchanged(Filename, Out.BeforeSHA256, Out.Message)
        || !Production::Save(Asset, Out.Message)) return Out;
    Out.SavedPackages.Add(AssetPath);
    if (!WriteGuard::HashFile(Filename, Out.AfterSHA256, Out.Message)) return Out;
    FString SavedFilename, SavedHash;
    if (!WriteGuard::CheckTarget(AssetPath, AllowedPackageRoot, Out.AfterSHA256, SavedFilename, SavedHash, Out.Message)
        || Asset->GetOutermost()->IsDirty())
    {
        if (Out.Message.IsEmpty()) Out.Message = TEXT("The saved target is still dirty.");
        return Out;
    }
    Out.bSuccess = true;
    Out.Warnings.Reset();
    Out.Message = TEXT("Reimported and saved the requested asset; Skeleton protected and original backup retained.");
    return Out;
}
}
