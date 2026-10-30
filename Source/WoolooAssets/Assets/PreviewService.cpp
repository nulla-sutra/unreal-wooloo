#include "WoolooAssets/Assets/PreviewService.h"
#include "WoolooAssets/Assets/ImportService.h"
#include "WoolooAssets/Assets/AssetWriteGuard.h"

#include "Animation/Skeleton.h"
#include "AssetToolsModule.h"
#include "Engine/PreviewMeshCollection.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/DataAssetFactory.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace Wooloo::Assets
{
FWoolooPreviewResult CreatePreview(const FString& CollectionPackage, const FString& SkeletonPackage,
    const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot,
    bool bSetAsSkeletonDefault, const FString& PreviewMeshPackage)
{
    FWoolooPreviewResult Out;
    Out.CollectionPath = CollectionPackage;
    if (!Production::Ready(Out.Message) || !Production::Scope(CollectionPackage, AllowedPackageRoot, Out.Message)
        || !Production::Unoccupied(CollectionPackage, Out.Message)) return Out;
    auto* Skeleton = LoadObject<USkeleton>(nullptr, *Production::ObjectPath(SkeletonPackage));
    if (!Skeleton || MeshPackages.IsEmpty())
    {
        Out.Message = TEXT("Supply an existing Skeleton and a non-empty mesh list.");
        return Out;
    }
    USkeletalMesh* PreviewMesh = nullptr;
    if (bSetAsSkeletonDefault)
    {
        if (!Production::Scope(SkeletonPackage, AllowedPackageRoot, Out.Message)) return Out;
        PreviewMesh = LoadObject<USkeletalMesh>(nullptr, *Production::ObjectPath(PreviewMeshPackage));
        if (Skeleton->GetOutermost()->IsDirty() || !PreviewMesh || PreviewMesh->GetSkeleton() != Skeleton)
        {
            Out.Message = TEXT("Default preview requires a clean Skeleton and its own existing preview mesh.");
            return Out;
        }
    }
    TArray<USkeletalMesh*> Meshes;
    TSet<FString> Unique;
    for (const auto& Package : MeshPackages)
    {
        const auto Key = Package.ToLower();
        auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *Production::ObjectPath(Package));
        if (!Mesh || !Mesh->GetSkeleton() || Unique.Contains(Key))
        {
            Out.Message = TEXT("Mesh list contains a missing, wrong-type or duplicate asset: ") + Package;
            return Out;
        }
        if (!Production::Compatible(Skeleton, Mesh, Out.Message)) return Out;
        Unique.Add(Key);
        Meshes.Add(Mesh);
    }
    TStrongObjectPtr<UDataAssetFactory> Factory(NewObject<UDataAssetFactory>());
    Factory->DataAssetClass = UPreviewMeshCollection::StaticClass();
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    auto* Collection = Cast<UPreviewMeshCollection>(Tools.CreateAsset(FPackageName::GetShortName(CollectionPackage),
        FPackageName::GetLongPackagePath(CollectionPackage), UPreviewMeshCollection::StaticClass(), Factory.Get()));
    if (!Collection)
    {
        Out.Message = TEXT("Native data asset factory did not create the PMC.");
        return Out;
    }
    Out.CreatedPackages.Add(CollectionPackage);
    Collection->Skeleton = Skeleton;
    for (auto* Mesh : Meshes) Collection->SkeletalMeshes.Emplace(Mesh);
    Collection->PostEditChange();
    if (!Production::Save(Collection, Out.Message)) return Out;
    Out.SavedPackages.Add(CollectionPackage);
    if (bSetAsSkeletonDefault)
    {
        // Updating defaults is explicit; existing PMC arrays and private rigs are untouched.
        Skeleton->SetPreviewMesh(PreviewMesh);
        Skeleton->SetAdditionalPreviewSkeletalMeshes(Collection);
        Skeleton->MarkPackageDirty();
        if (!Production::Save(Skeleton, Out.Message)) return Out;
        Out.SavedPackages.Add(SkeletonPackage);
    }
    Out.bSuccess = true;
    Out.Message = TEXT("Created and saved the preview collection.");
    return Out;
}

FWoolooPreviewUpdateResult UpdatePreview(const FString& CollectionPackage,
    const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot,
    const FString& ExpectedCollectionSHA256)
{
    FWoolooPreviewUpdateResult Out;
    Out.CollectionPath = CollectionPackage;
    FString Filename;
    if (!WriteGuard::CleanEditor(Out.Message)
        || !WriteGuard::CheckTarget(CollectionPackage, AllowedPackageRoot, ExpectedCollectionSHA256,
            Filename, Out.BeforeSHA256, Out.Message)) return Out;
    auto* Collection = LoadObject<UPreviewMeshCollection>(nullptr, *Production::ObjectPath(CollectionPackage));
    if (!Collection || !Collection->Skeleton || MeshPackages.IsEmpty())
    {
        Out.Message = TEXT("Supply a saved PMC with a Skeleton and a non-empty complete mesh list.");
        return Out;
    }
    TArray<USkeletalMesh*> Meshes;
    TSet<FString> Unique;
    for (const auto& Package : MeshPackages)
    {
        if (!FPackageName::IsValidLongPackageName(Package) || Unique.Contains(Package.ToLower()))
        {
            Out.Message = TEXT("Invalid or duplicate mesh package: ") + Package;
            return Out;
        }
        auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *Production::ObjectPath(Package));
        if (!Mesh || !Mesh->GetSkeleton() || Mesh->IsCompiling())
        {
            Out.Message = TEXT("Mesh is missing, has no Skeleton or is still compiling: ") + Package;
            return Out;
        }
        if (!Production::Compatible(Collection->Skeleton, Mesh, Out.Message)) return Out;
        Unique.Add(Package.ToLower());
        Meshes.Add(Mesh);
    }
    if (!WriteGuard::CleanEditor(Out.Message)
        || !WriteGuard::Backup(Filename, Out.BeforeSHA256, TEXT("PreviewBackups"), Out.BackupDirectory, Out.Message)
        || !WriteGuard::Unchanged(Filename, Out.BeforeSHA256, Out.Message)) return Out;
    Out.bModifiedInMemory = true;
    Collection->Modify();
    Collection->SkeletalMeshes.Reset();
    for (auto* Mesh : Meshes) Collection->SkeletalMeshes.Emplace(Mesh);
    Collection->PostEditChange();
    Collection->MarkPackageDirty();
    if (!WriteGuard::OnlyTargetDirty(Collection, Out.Message)
        || !WriteGuard::Unchanged(Filename, Out.BeforeSHA256, Out.Message)
        || !Production::Save(Collection, Out.Message)) return Out;
    Out.SavedPackages.Add(CollectionPackage);
    if (!WriteGuard::HashFile(Filename, Out.AfterSHA256, Out.Message)) return Out;
    Out.bSuccess = true;
    Out.Message = TEXT("Updated and saved only the requested PMC; original backup retained.");
    return Out;
}
}
