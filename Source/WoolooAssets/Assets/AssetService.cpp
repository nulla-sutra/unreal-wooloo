#include "WoolooAssets/Assets/AssetService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "CollectionManagerModule.h"
#include "CoreGlobals.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "ICollectionManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace Wooloo::Assets
{
namespace
{
FString Directory(FString Root)
{
    Root.RemoveFromEnd(TEXT("/"));
    return Root;
}

bool Under(const FString& Package, const FString& Root)
{
    const auto Normalized = Directory(Root);
    return Package.Equals(Normalized, ESearchCase::IgnoreCase)
        || Package.StartsWith(Normalized + TEXT("/"), ESearchCase::IgnoreCase);
}

bool Protected(const FString& Package, const TArray<FString>& Roots)
{
    return Roots.ContainsByPredicate([&](const auto& Root) { return Under(Package, Root); });
}

bool InScope(const FString& Package, const TArray<FString>& Roots)
{
    return Roots.ContainsByPredicate([&](const auto& Root)
    {
        return Package.StartsWith(Directory(Root) + TEXT("/"), ESearchCase::IgnoreCase);
    });
}

FString Mount(const FString& Package)
{
    const auto End = Package.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    return End == INDEX_NONE ? FString() : Package.Left(End);
}

void ValidateScopes(const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots,
    FWoolooAssetResult& Out)
{
    if (Roots.IsEmpty()) Out.Issues.Add(TEXT("Supply at least one narrow AllowedPackageRoot."));
    for (const auto& Root : Roots)
    {
        const auto Path = Directory(Root);
        if (!FPackageName::IsValidLongPackageName(Path) || Path.Find(TEXT("/"), ESearchCase::CaseSensitive,
            ESearchDir::FromStart, 1) == INDEX_NONE)
        {
            Out.Issues.Add(TEXT("Allowed roots must be content directories below a mount point: ") + Root);
        }
    }
    for (const auto& Root : ProtectedRoots)
    {
        if (!FPackageName::IsValidLongPackageName(Directory(Root), true))
            Out.Issues.Add(TEXT("Invalid protected package root: ") + Root);
    }
}

bool WritablePackage(const FString& Package, const TArray<FString>& ProtectedRoots,
    FWoolooAssetResult& Out)
{
    FString Filename;
    if (!FPackageName::IsValidLongPackageName(Package)
        || !FPackageName::TryConvertLongPackageNameToFilename(Package, Filename)
        || FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),
            FPaths::ConvertRelativePathToFull(FPaths::EngineDir()))
        || Protected(Package, ProtectedRoots))
    {
        Out.Issues.Add(TEXT("Invalid, engine-owned or protected package: ") + Package);
        return false;
    }
    if (FPackageName::DoesPackageExist(Package, &Filename) && IFileManager::Get().IsReadOnly(*Filename))
    {
        Out.Issues.Add(TEXT("Make the package writable before authoring: ") + Package);
        return false;
    }
    return true;
}

bool Ready(FWoolooAssetResult& Out)
{
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
        Out.Issues.Add(TEXT("Asset operations require the editor game thread with PIE stopped."));
    if (FAssetRegistryModule::GetRegistry().IsLoadingAssets())
        Out.Issues.Add(TEXT("Wait for Asset Registry discovery to finish before planning or authoring."));
    if (!Out.Issues.IsEmpty()) Out.Message = TEXT("Editor or Asset Registry preflight failed.");
    return Out.Issues.IsEmpty();
}

bool CleanEditor(FWoolooAssetResult& Out)
{
    // Native rename/fixup also visits dirty packages when replacing soft references.
    TArray<UPackage*> Dirty;
    FEditorFileUtils::GetDirtyWorldPackages(Dirty);
    FEditorFileUtils::GetDirtyContentPackages(Dirty);
    for (const auto* Package : Dirty)
        Out.Issues.AddUnique(TEXT("Save or discard editor changes before authoring: ") + Package->GetName());
    return Out.Issues.IsEmpty();
}

TArray<FAssetData> PackageAssets(const FString& Package)
{
    TArray<FAssetData> Data;
    FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(FName(*Package), Data, true);
    return Data;
}

void Referencers(const TSet<FName>& Packages, const TArray<FString>& ProtectedRoots, FWoolooAssetResult& Out)
{
    for (const auto Package : Packages)
    {
        TArray<FName> Refs;
        FAssetRegistryModule::GetRegistry().GetReferencers(Package, Refs);
        for (const auto Ref : Refs)
        {
            if (!Packages.Contains(Ref))
                Out.ReferencerPackages.AddUnique(Ref.ToString());
        }
    }
    Out.ReferencerPackages.Sort();
    for (const auto& Package : Out.ReferencerPackages)
        WritablePackage(Package, ProtectedRoots, Out);
}

void Approved(const TArray<FString>& Packages, FWoolooAssetResult& Out)
{
    TSet<FName> Names;
    for (const auto& Package : Packages) Names.Add(FName(*Package));
    for (const auto& Package : Out.ReferencerPackages)
    {
        if (!Names.Contains(FName(*Package)))
            Out.Issues.Add(TEXT("Referencer was not explicitly approved for native saves: ") + Package);
    }
}

void Finish(FWoolooAssetResult& Out, const FString& Success, const FString& Failure)
{
    Out.bSuccess = Out.Issues.IsEmpty();
    Out.Message = Out.bSuccess ? Success : Failure;
}

bool Redirector(const FString& Package)
{
    const auto Data = PackageAssets(Package);
    return Data.Num() == 1 && Data[0].IsRedirector();
}
}

FWoolooAssetResult PlanMoves(const TArray<FWoolooAssetMove>& Moves,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots)
{
    FWoolooAssetResult Out;
    if (!Ready(Out)) return Out;
    ValidateScopes(Roots, ProtectedRoots, Out);
    if (Moves.IsEmpty()) Out.Issues.Add(TEXT("No asset moves supplied."));
    TSet<FName> Sources;
    TSet<FName> Destinations;
    for (const auto& Move : Moves)
    {
        for (const auto& Package : { Move.SourcePackage, Move.DestinationPackage })
        {
            WritablePackage(Package, ProtectedRoots, Out);
            if (!InScope(Package, Roots)) Out.Issues.Add(TEXT("Package is outside AllowedPackageRoots: ") + Package);
        }
        const FName Source(*Move.SourcePackage);
        const FName Destination(*Move.DestinationPackage);
        // Cross-plugin moves can make additional dependency packages public in native rename.
        if (!Mount(Move.SourcePackage).Equals(Mount(Move.DestinationPackage), ESearchCase::IgnoreCase))
            Out.Issues.Add(TEXT("Source and destination must use the same content mount: ") + Move.SourcePackage);
        if (Source == Destination || Sources.Contains(Source) || Destinations.Contains(Destination))
            Out.Issues.Add(TEXT("Repeated source/destination or case-only rename: ") + Move.SourcePackage);
        Sources.Add(Source);
        Destinations.Add(Destination);
        const auto Data = PackageAssets(Move.SourcePackage);
        if (Data.Num() != 1 || Data[0].IsRedirector()
            || Data[0].AssetClassPath == FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(TEXT("World")))
            || !FPackageName::DoesPackageExist(Move.SourcePackage))
        {
            Out.Issues.Add(TEXT("Source must contain one saved non-redirector, non-map asset: ") + Move.SourcePackage);
        }
        if (!PackageAssets(Move.DestinationPackage).IsEmpty()
            || FPackageName::DoesPackageExist(Move.DestinationPackage)
            || FindPackage(nullptr, *Move.DestinationPackage))
        {
            Out.Issues.Add(TEXT("Destination already exists on disk or in memory: ") + Move.DestinationPackage);
        }
    }
    for (const auto Name : Destinations)
    {
        if (Sources.Contains(Name)) Out.Issues.Add(TEXT("Chained moves require separate reviewed batches: ") + Name.ToString());
    }
    // Invalid paths must not become queries that expand the intended write boundary.
    if (Out.Issues.IsEmpty()) Referencers(Sources, ProtectedRoots, Out);
    Finish(Out, TEXT("Read-only move preflight passed; review ReferencerPackages before authoring."),
        TEXT("Move preflight failed; nothing was loaded, moved or saved."));
    return Out;
}

FWoolooAssetResult Move(const TArray<FWoolooAssetMove>& Moves,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots,
    const TArray<FString>& ApprovedReferencers)
{
    auto Out = PlanMoves(Moves, Roots, ProtectedRoots);
    Approved(ApprovedReferencers, Out);
    if (!Out.Issues.IsEmpty() || !CleanEditor(Out))
    {
        Finish(Out, {}, TEXT("Move refused before authoring."));
        return Out;
    }
    TArray<TStrongObjectPtr<UObject>> Loaded;
    TArray<FAssetRenameData> Renames;
    for (const auto& Mapping : Moves)
    {
        const auto Data = PackageAssets(Mapping.SourcePackage);
        auto* Asset = Data[0].GetAsset();
        if (!Asset || Asset->GetOutermost()->GetFName() != FName(*Mapping.SourcePackage))
        {
            Out.Issues.Add(TEXT("Could not load the exact source asset: ") + Mapping.SourcePackage);
            break;
        }
        Loaded.Emplace(Asset);
        Renames.Emplace(TWeakObjectPtr<UObject>(Asset), FPackageName::GetLongPackagePath(Mapping.DestinationPackage),
            FPackageName::GetShortName(Mapping.DestinationPackage), false, false);
    }
    if (!Out.Issues.IsEmpty() || !CleanEditor(Out))
    {
        Finish(Out, {}, TEXT("Source loading failed or dirtied packages; no rename was attempted."));
        return Out;
    }
    auto& Tools = FAssetToolsModule::GetModule().Get();
    TGuardValue<bool> Unattended(GIsRunningUnattendedScript, true);
    const auto bRenamed = Tools.RenameAssets(Renames);
    for (int32 Index = 0; Index < Moves.Num(); ++Index)
    {
        const auto& Mapping = Moves[Index];
        auto* Asset = Loaded[Index].Get();
        // Disk existence alone is insufficient after a partial native rename/save failure.
        if (Asset->GetOutermost()->GetFName() == FName(*Mapping.DestinationPackage)
            && FPackageName::DoesPackageExist(Mapping.DestinationPackage) && !Asset->GetOutermost()->IsDirty())
        {
            Out.CompletedPackages.Add(Mapping.DestinationPackage);
        }
        else
        {
            Out.Issues.Add(TEXT("Native move/save did not complete: ") + Mapping.SourcePackage);
        }
        if (Redirector(Mapping.SourcePackage)) Out.RemainingRedirectors.Add(Mapping.SourcePackage);
    }
    if (!bRenamed) Out.Issues.Add(TEXT("Native RenameAssets reported failure; partial results are not rolled back."));
    Finish(Out, TEXT("Native move/save completed. Inspect remaining redirectors before a separate fixup."),
        TEXT("Native move/save had failures; inspect CompletedPackages before planning a retry."));
    return Out;
}

FWoolooAssetResult InspectRedirectors(const TArray<FString>& Packages,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots)
{
    FWoolooAssetResult Out;
    if (!Ready(Out)) return Out;
    ValidateScopes(Roots, ProtectedRoots, Out);
    TSet<FName> Selected;
    for (const auto& Package : Packages)
    {
        if (!WritablePackage(Package, ProtectedRoots, Out) || !InScope(Package, Roots))
        {
            Out.Issues.Add(TEXT("Redirector package is outside the allowed scope: ") + Package);
            continue;
        }
        if (!FPackageName::DoesPackageExist(Package)) continue;
        if (!Redirector(Package))
        {
            Out.Issues.Add(TEXT("Existing package is not a single redirector: ") + Package);
            continue;
        }
        Selected.Add(FName(*Package));
        Out.RemainingRedirectors.AddUnique(Package);
    }
    if (Out.Issues.IsEmpty()) Referencers(Selected, ProtectedRoots, Out);
    Out.RemainingRedirectors.Sort();
    Finish(Out, TEXT("Read-only redirector inspection completed."), TEXT("Redirector preflight failed; nothing was repaired."));
    return Out;
}

FWoolooAssetResult FixupRedirectors(const TArray<FString>& Packages,
    const TArray<FString>& Roots, const TArray<FString>& ProtectedRoots,
    const TArray<FString>& ApprovedReferencers)
{
    auto Out = InspectRedirectors(Packages, Roots, ProtectedRoots);
    Approved(ApprovedReferencers, Out);
    if (!Out.Issues.IsEmpty() || !CleanEditor(Out))
    {
        Finish(Out, {}, TEXT("Redirector fixup refused before authoring."));
        return Out;
    }
    TArray<UObjectRedirector*> Redirectors;
    TMap<FSoftObjectPath, FSoftObjectPath> RedirectMap;
    for (const auto& Package : Out.RemainingRedirectors)
    {
        auto* Asset = Cast<UObjectRedirector>(PackageAssets(Package)[0].GetAsset());
        if (!Asset || !Asset->DestinationObject)
        {
            Out.Issues.Add(TEXT("Could not load redirector or its destination: ") + Package);
            continue;
        }
        const auto Destination = Asset->DestinationObject->GetOutermost()->GetName();
        if (!WritablePackage(Destination, ProtectedRoots, Out) || !InScope(Destination, Roots))
        {
            Out.Issues.Add(TEXT("Redirector destination is outside AllowedPackageRoots: ") + Destination);
            continue;
        }
        Redirectors.Add(Asset);
        const FSoftObjectPath OldPath(Asset);
        const FSoftObjectPath NewPath(Asset->DestinationObject);
        RedirectMap.Add(OldPath, NewPath);
        if (Cast<UBlueprint>(Asset->DestinationObject))
        {
            // Generated-class and CDO references must follow a Blueprint package move too.
            RedirectMap.Add(FSoftObjectPath(OldPath.ToString() + TEXT("_C")),
                FSoftObjectPath(NewPath.ToString() + TEXT("_C")));
            RedirectMap.Add(FSoftObjectPath(OldPath.GetLongPackageName() + TEXT(".Default__") + OldPath.GetAssetName() + TEXT("_C")),
                FSoftObjectPath(NewPath.GetLongPackageName() + TEXT(".Default__") + NewPath.GetAssetName() + TEXT("_C")));
        }
    }
    TArray<TStrongObjectPtr<UPackage>> LoadedPackages;
    TArray<UPackage*> PackagesToSave;
    for (const auto& Name : Out.ReferencerPackages)
    {
        auto* Package = LoadPackage(nullptr, *Name, LOAD_None);
        if (!Package || Package->GetFName() != FName(*Name))
        {
            Out.Issues.Add(TEXT("Could not load the exact referencer package: ") + Name);
            continue;
        }
        LoadedPackages.Emplace(Package);
        PackagesToSave.Add(Package);
    }
    if (!Out.Issues.IsEmpty() || !CleanEditor(Out))
    {
        Finish(Out, {}, TEXT("Loading failed or dirtied packages; no fixup was attempted."));
        return Out;
    }
    const auto Selected = Out.RemainingRedirectors;
    if (!Redirectors.IsEmpty())
    {
        TGuardValue<bool> Unattended(GIsRunningUnattendedScript, true);
        // The editor's FixupReferencers uses a modal report. Reuse its public, non-modal
        // reference replacement/save/delete primitives without automating editor UI.
        FAssetToolsModule::GetModule().Get().RenameReferencingSoftObjectPaths(PackagesToSave, RedirectMap);
        TArray<FString> SavedFiles;
        for (auto* Package : PackagesToSave)
        {
            FString Filename;
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            if (!FPackageName::DoesPackageExist(Package->GetName(), &Filename)
                || !UPackage::SavePackage(Package, Package->FindAssetInPackage(), *Filename, Args))
            {
                Out.Issues.Add(TEXT("Could not save referencer; redirectors will be retained: ") + Package->GetName());
                continue;
            }
            Out.SavedReferencerPackages.Add(Package->GetName());
            SavedFiles.Add(Filename);
        }
        auto& Registry = FAssetRegistryModule::GetRegistry();
        Registry.ScanFilesSynchronous(SavedFiles, true);
        if (Out.Issues.IsEmpty())
        {
            TArray<UObject*> Unreferenced;
            for (auto* Redirect : Redirectors)
            {
                TArray<FName> Refs;
                Registry.GetReferencers(Redirect->GetOutermost()->GetFName(), Refs);
                bool bReferenced = false;
                bool bUndoReferenced = false;
                ObjectTools::GatherObjectReferencersForDeletion(Redirect, bReferenced, bUndoReferenced);
                if (!Refs.IsEmpty() || bReferenced || bUndoReferenced)
                {
                    Out.Issues.Add(TEXT("Redirector still has disk, memory or undo references: ") + Redirect->GetPathName());
                    continue;
                }
                FText Error;
                if (!FCollectionManagerModule::GetModule().Get().HandleRedirectorDeleted(FSoftObjectPath(Redirect), &Error))
                {
                    Out.Issues.Add(TEXT("Could not update asset collections: ") + Error.ToString());
                    continue;
                }
                Unreferenced.Add(Redirect);
            }
            // Public native safe-delete primitive; reference checks above must precede it.
            if (!Unreferenced.IsEmpty()) ObjectTools::DeleteObjectsUnchecked(Unreferenced);
        }
    }
    Out.RemainingRedirectors.Reset();
    for (const auto& Package : Selected)
    {
        if (FPackageName::DoesPackageExist(Package)) Out.RemainingRedirectors.Add(Package);
        else Out.CompletedPackages.Add(Package);
    }
    if (!Out.RemainingRedirectors.IsEmpty()) Out.Issues.Add(TEXT("Native fixup retained unresolved redirectors; no files were force-deleted."));
    Finish(Out, TEXT("Native fixup completed; selected redirector packages are gone from disk."),
        TEXT("Native fixup was incomplete; inspect RemainingRedirectors and the editor log."));
    return Out;
}
}
