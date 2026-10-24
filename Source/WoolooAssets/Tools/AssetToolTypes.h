#pragma once

#include "CoreMinimal.h"
#include "AssetToolTypes.generated.h"

USTRUCT(BlueprintType)
struct WOOLOOASSETS_API FWoolooAssetMove
{
    GENERATED_BODY()

    /** Long package names without object suffixes. Assets are never overwritten. */
    UPROPERTY(BlueprintReadWrite, Category="Assets") FString SourcePackage;
    UPROPERTY(BlueprintReadWrite, Category="Assets") FString DestinationPackage;
};

USTRUCT(BlueprintType)
struct WOOLOOASSETS_API FWoolooAssetResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> Issues;
    /** Disk-registry referencers outside the selected source/redirector packages. */
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> ReferencerPackages;
    /** Destinations confirmed on disk after a move, or redirector packages confirmed removed. */
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> CompletedPackages;
    /** Referencers explicitly saved by redirector repair, including partial repair results. */
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> SavedReferencerPackages;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> RemainingRedirectors;
};
