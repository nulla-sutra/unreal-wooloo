#include "WoolooAssets/Assets/AssetWriteGuard.h"
#include "WoolooAssets/Assets/ImportService.h"

#include "Algo/AllOf.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "String/BytesToHex.h"
#include "UObject/Package.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace Wooloo::Assets::WriteGuard
{
bool CleanEditor(FString& Error)
{
    if (!Production::Ready(Error)) return false;
    TArray<UPackage*> Dirty;
    FEditorFileUtils::GetDirtyContentPackages(Dirty);
    FEditorFileUtils::GetDirtyWorldPackages(Dirty);
    if (Dirty.IsEmpty()) return true;
    Error = TEXT("Save or discard existing editor changes before updating assets.");
    return false;
}

bool HashFile(const FString& Filename, FString& Hash, FString& Error)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Filename))
    {
        Error = TEXT("Cannot read file for SHA-256: ") + Filename;
        return false;
    }
    uint8 Digest[SHA256_DIGEST_LENGTH];
    if (!SHA256(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Digest))
    {
        Error = TEXT("Native SHA-256 failed: ") + Filename;
        return false;
    }
    Hash = BytesToHex(Digest, SHA256_DIGEST_LENGTH).ToLower();
    return true;
}

bool CheckTarget(const FString& Package, const FString& Root, const FString& ExpectedHash,
    FString& Filename, FString& Hash, FString& Error)
{
    if (!Production::Scope(Package, Root, Error)) return false;
    if (ExpectedHash.Len() != SHA256_DIGEST_LENGTH * 2
        || !Algo::AllOf(ExpectedHash, [](TCHAR Character) { return FChar::IsHexDigit(Character); }))
    {
        Error = TEXT("An explicit 64-digit reviewed SHA-256 is required.");
        return false;
    }
    Filename = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(
        Package, FPackageName::GetAssetPackageExtension()));
    if (!FPaths::FileExists(Filename) || IFileManager::Get().IsReadOnly(*Filename))
    {
        Error = TEXT("Target must be a saved writable .uasset: ") + Package;
        return false;
    }
    // A single-file guard cannot protect a split package or its external bulk payload.
    TArray<FString> Companions;
    IFileManager::Get().FindFiles(Companions, *(FPaths::ChangeExtension(Filename, TEXT("*"))), true, false);
    if (Companions.Num() != 1 || Companions[0] != FPaths::GetCleanFilename(Filename))
    {
        Error = TEXT("Only a single-file .uasset without companion files is supported: ") + Package;
        return false;
    }
    if (!HashFile(Filename, Hash, Error)) return false;
    if (!Hash.Equals(ExpectedHash, ESearchCase::IgnoreCase))
    {
        Error = TEXT("Target SHA-256 no longer matches the reviewed file: ") + Package;
        return false;
    }
    return true;
}

bool Unchanged(const FString& Filename, const FString& ExpectedHash, FString& Error)
{
    FString Actual;
    if (!HashFile(Filename, Actual, Error)) return false;
    if (Actual.Equals(ExpectedHash, ESearchCase::IgnoreCase)) return true;
    Error = TEXT("The file changed during preflight; refusing further writes: ") + Filename;
    return false;
}

bool Backup(const FString& Filename, const FString& ExpectedHash, const FString& Category,
    FString& Directory, FString& Error)
{
    if (!Unchanged(Filename, ExpectedHash, Error)) return false;
    Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/WoolooAssets")
        / Category / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const auto Destination = Directory / FPaths::GetCleanFilename(Filename);
    if (!IFileManager::Get().MakeDirectory(*Directory, true)
        || IFileManager::Get().Copy(*Destination, *Filename, false) != COPY_OK
        || !Unchanged(Destination, ExpectedHash, Error))
    {
        if (Error.IsEmpty()) Error = TEXT("Could not create and verify the original file backup.");
        return false;
    }
    return true;
}

bool OnlyTargetDirty(const UObject* Target, FString& Error)
{
    TArray<UPackage*> Dirty;
    FEditorFileUtils::GetDirtyContentPackages(Dirty);
    FEditorFileUtils::GetDirtyWorldPackages(Dirty);
    for (const auto* Package : Dirty)
    {
        if (Package == Target->GetOutermost()) continue;
        Error = TEXT("An unexpected package became dirty; refusing to save: ") + Package->GetName();
        return false;
    }
    return true;
}
}
