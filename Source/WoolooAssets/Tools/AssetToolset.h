#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooAssets/Tools/AssetToolTypes.h"
#include "AssetToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOASSETS_API UWoolooAssetToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Import one new FBX mesh/animation or PNG/JPG/TGA texture through native factories.
     * Saves only this operation's new assets. No overwrite or physics generation.
     * Existing Skeletons are isolated during import and may not acquire bones or a new reference pose.
     * MaterialOverrides maps imported slot names to existing material packages.
     * Partial failures report created/saved packages; never retry a write blindly. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooImportResult ImportAsset(const FString& SourceFilename, const FString& DestinationPackage,
        EWoolooImportKind Kind, const FString& AllowedPackageRoot, const FString& SkeletonPackage,
        const FString& NewSkeletonPackage, const TMap<FName, FString>& MaterialOverrides, bool bSRGB);

    /** Create and save a new native PMC. SetAsDefault explicitly also writes the named clean Skeleton.
     * Private module skeletons remain independent. No existing collection is overwritten. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooPreviewResult CreatePreviewMeshCollection(const FString& CollectionPackage,
        const FString& SkeletonPackage, const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot,
        bool bSetAsSkeletonDefault, const FString& PreviewMeshPackage);

    /** Read saved/reference data and optionally sample compressed animation in component space (cm).
     * Supports Skeleton, SkeletalMesh, AnimSequence, PMC, Material and Texture2D.
     * Sampling takes at most 128 times; an empty bone list selects all bones. No repair/save/PIE. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooInspectionResult InspectAsset(const FString& AssetPath,
        const TArray<float>& SampleTimes, const TArray<FName>& BoneNames);

    /** Read native raw and compressed local/component animation samples without rebuilding data.
     * Takes at most 128 times. Missing data, additive sequences and invalid selections are refused.
     * Returns reference transforms, source tracks and compression metadata; cm and XYZW quaternions. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooInspectionResult InspectAnimationSamples(const FString& AssetPath,
        const TArray<float>& SampleTimes, const TArray<FName>& BoneNames);

    /** Native FBX reimport of one clean single-file asset, guarded by its reviewed disk SHA-256.
     * Backs up the original and validates a transient reimport first. Skeletons stay isolated.
     * Mesh materials must cover every new imported slot; an empty value explicitly clears a slot.
     * MaterialSlotRenames explicitly changes both slot names. A sole unnamed default slot may use None as its source key.
     * Partial failures report in-memory modification and exact saves. No automatic rollback/retry. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooReimportResult ReimportAsset(const FString& AssetPath, const FString& SourceFilename,
        const FString& AllowedPackageRoot, const FString& ExpectedAssetSHA256,
        const TMap<FName, FString>& MaterialOverrides, const TMap<FName, FName>& MaterialSlotRenames);

    /** Replace one existing PMC's full mesh list after scope, SHA-256 and compatibility checks.
     * Backs up and saves only that clean single-file PMC, retaining its Skeleton and defaults. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooPreviewUpdateResult UpdatePreviewMeshCollection(const FString& CollectionPackage,
        const TArray<FString>& MeshPackages, const FString& AllowedPackageRoot,
        const FString& ExpectedCollectionSHA256);

    /** Read-only preflight of explicit package moves and their disk-registry referencers.
     * Requires narrow writable roots; protected roots are excluded even when nested inside them.
     * Does not load assets, move files, save packages, or fix references. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult PlanMoves(const TArray<FWoolooAssetMove>& Moves,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots);

    /** Execute the reviewed mapping through native AssetTools::RenameAssets.
     * Native rename also fixes references, saves packages, and may use configured source control.
     * ApprovedReferencerPackages must include every referencer returned by a fresh PlanMoves.
     * Refuses PIE, dirty editor packages, conflicts, read-only files and protected/engine content.
     * May partially succeed; inspect CompletedPackages and Issues before retrying. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult MoveAssets(const TArray<FWoolooAssetMove>& Moves,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots,
        const TArray<FString>& ApprovedReferencerPackages);

    /** Read-only inspection of an explicit list of old package names. Missing packages are ignored.
     * Existing packages must be redirectors within AllowedPackageRoots. Does not repair them. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult InspectRedirectors(const TArray<FString>& RedirectorPackages,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots);

    /** Replace soft paths and resave exact approved referencers through native APIs.
     * Rechecks the approved referencer list and clean editor state before authoring.
     * Deletes redirectors only after disk and memory reference checks, and updates affected collections.
     * Avoids the modal report in AssetTools::FixupReferencers. Never force-deletes files. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult FixupRedirectors(const TArray<FString>& RedirectorPackages,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots,
        const TArray<FString>& ApprovedReferencerPackages);
};
