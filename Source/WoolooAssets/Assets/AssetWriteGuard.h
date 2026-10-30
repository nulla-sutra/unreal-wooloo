#pragma once

#include "CoreMinimal.h"

namespace Wooloo::Assets::WriteGuard
{
// Internal guards shared by the existing-asset writers. No save, checkout or retry is implicit.
bool CleanEditor(FString& Error);
bool HashFile(const FString& Filename, FString& Hash, FString& Error);
bool CheckTarget(const FString& Package, const FString& Root, const FString& ExpectedHash,
    FString& Filename, FString& Hash, FString& Error);
bool Unchanged(const FString& Filename, const FString& ExpectedHash, FString& Error);
bool Backup(const FString& Filename, const FString& ExpectedHash, const FString& Category,
    FString& Directory, FString& Error);
bool OnlyTargetDirty(const UObject* Target, FString& Error);
}
