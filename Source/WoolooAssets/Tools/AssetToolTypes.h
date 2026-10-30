#pragma once

#include "CoreMinimal.h"
#include "AssetToolTypes.generated.h"

UENUM(BlueprintType)
enum class EWoolooImportKind : uint8
{
    SkeletalMesh,
    Animation,
    Texture2D
};

USTRUCT(BlueprintType)
struct WOOLOOASSETS_API FWoolooImportResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString SkeletonPath;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> CreatedPackages;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> SavedPackages;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct WOOLOOASSETS_API FWoolooPreviewResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString CollectionPath;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> CreatedPackages;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> SavedPackages;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct WOOLOOASSETS_API FWoolooReimportResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bModifiedInMemory = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString BackupDirectory;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString BeforeSHA256;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString AfterSHA256;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> SavedPackages;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct WOOLOOASSETS_API FWoolooPreviewUpdateResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString CollectionPath;
    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bModifiedInMemory = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString BackupDirectory;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString BeforeSHA256;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString AfterSHA256;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> SavedPackages;
    UPROPERTY(BlueprintReadOnly, Category="Assets") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct WOOLOOASSETS_API FWoolooInspectionResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Assets") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Assets") FString Json;
};

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
