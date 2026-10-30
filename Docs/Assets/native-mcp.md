# Native asset migration tools

## Import, preview and inspection

`WoolooAssets.WoolooAssetToolset` also provides the following production tools.
They use native editor APIs, contain no consuming-project naming conventions,
and never start PIE, build C++, or execute Python.

- `ImportAsset(SourceFilename, DestinationPackage, Kind, AllowedPackageRoot,
  SkeletonPackage, NewSkeletonPackage, MaterialOverrides, bSRGB)` imports one
  new SkeletalMesh, Animation or Texture2D. Paths are package names without an
  object suffix. FBX uses scene/unit conversion, zero added transform and no
  physics asset generation. Materials are supplied explicitly by imported
  slot name, rather than automatically reconstructed from FBX shaders. Texture
  input supports PNG/JPG/TGA. Existing targets are refused in memory and on disk.
  The tool requires a clean editor and saves only its new assets. Its result
  lists created/saved packages and the resulting Skeleton path.
- `CreatePreviewMeshCollection(CollectionPackage, SkeletonPackage, MeshPackages,
  AllowedPackageRoot, bSetAsSkeletonDefault, PreviewMeshPackage)` creates and
  saves a new PMC. Common bone parent chains must match; private bones and their
  Skeletons remain module-owned. Setting defaults is explicit, requires a clean
  Skeleton inside the allowed root, and additionally saves only that Skeleton.
  Without that opt-in, existing preview settings are unchanged.
- `InspectAsset(AssetPath, SampleTimes, BoneNames)` reads Skeleton, SkeletalMesh,
  AnimSequence, PMC, Material or Texture2D. `Json` contains reference local TRS,
  material/resource references, source files and type-specific settings. Animation
  sampling accepts up to 128 explicit seconds and returns component-space TRS
  in centimeters, with XYZW quaternions. An empty bone list samples all bones.
  Sampling requires ready native compressed data and a non-additive sequence;
  pending compression is reported, never forced or replaced by raw evaluation.
  This tool does not save, repair or alter compression settings.
- `InspectAnimationSamples(AssetPath, SampleTimes, BoneNames)` reads both native
  raw and compressed bone transforms at the same explicit times (at most 128).
  `Json.samples[].poses[bone]` contains `raw_local`, `compressed_local`,
  `raw_component`, `compressed_component` and `has_raw_track`. Units are cm and
  quaternions are XYZW. `bones` contains local reference transforms and parents;
  `raw_tracks`, frame rate, duration, configured/active codec metadata and editable
  scalar compression settings support diagnosis. Untracked bones explicitly
  retain reference transforms. Missing raw/current compressed data, forced raw
  evaluation, additive sequences and invalid times/bones fail without fallback,
  rebuilding or saving. This tool reports measurements, not a consumer's pass/fail
  tolerances. Raw extraction includes native transform curves on tracked bones.
  When changing compression settings through another editor adapter, send the
  native property-change notification for that property to invalidate/rebuild
  the sequence's derived data. A pointer assignment or generic `PostEditChange`
  can leave the configured settings different from the active cached codec.
  Check `active_bone_codec` as well as the configured settings before accepting
  a compression comparison; this read-only tool never rebuilds that cache.
- `ReimportAsset(AssetPath, SourceFilename, AllowedPackageRoot,
  ExpectedAssetSHA256, MaterialOverrides, MaterialSlotRenames)` explicitly updates one existing FBX
  SkeletalMesh/AnimSequence. It requires a saved writable single-file `.uasset`,
  the caller's reviewed SHA-256, stopped PIE and a clean editor. Meshes must have
  one LOD and no morphs, cloth, skin-weight profiles or custom reimport factory.
  Animations must be ready and non-additive. Mesh material maps must cover exactly
  the new imported slots; an empty material value explicitly clears a slot.
  Both maps use original imported slot names. `MaterialSlotRenames` optionally
  assigns explicit display/imported names and updates native material/section
  history. Empty/colliding names and unknown source slots are refused before
  binding. `None` is accepted only for a sole unnamed native FBX default slot;
  it is not a wildcard. The caller supplies any desired alias. Animations require
  both maps empty and preserve playback/compression settings.
  The original bytes are verified and backed up under
  `Saved/Automation/WoolooAssets/ReimportBackups/<id>/`. A transient validation
  reimport completes before the target is modified. Both passes use the explicit
  native FBX Reimport Factory and a transient Skeleton, with normal scene/unit
  conversion, no added transforms and no Skeleton extension/reference update,
  physics, material or texture creation. Generic ReimportManager conversion to
  Interchange is avoided so the reviewed FBX options are honored. Only the target
  is saved, after checking that preflight files stayed unchanged and no unrelated
  content/map package became dirty. Native asset derived data may be finished for
  this asset; this is not C++ compilation. Results include `bModifiedInMemory`,
  `BackupDirectory`, before/after hashes and exact `SavedPackages`.
- `UpdatePreviewMeshCollection(CollectionPackage, MeshPackages,
  AllowedPackageRoot, ExpectedCollectionSHA256)` replaces the complete mesh list
  of one existing single-file PMC. It uses the same clean-editor, narrow-scope,
  reviewed-SHA and verified-backup guards. Backups go to
  `Saved/Automation/WoolooAssets/PreviewBackups/<id>/`. The PMC's existing Skeleton
  stays assigned; public parent chains are checked and private Skeleton ownership
  is preserved. Empty/duplicate/invalid mesh lists fail before mutation. It saves
  only the PMC, retaining Skeleton default previews. Results include
  `bModifiedInMemory`, backup path, before/after hashes and exact saves.

Skeleton inspection reads `PreviewSkeletalMesh` and
`AdditionalPreviewSkeletalMeshes` as serialized native soft properties. Package
fields remain `preview_mesh` / `preview_collection`; full object paths are also
reported. Valid unloaded references are not mistaken for null pointers, and the
inspection does not resolve their soft caches.

Existing-asset writers never roll back or retry automatically. A failure can
leave the target modified in memory or already saved; inspect the result and
backup before taking another action. They do not check out files, save unrelated
packages, edit engine code, start PIE, compile C++, or restart the editor.
SHA-256 uses the engine-bundled OpenSSL library declared privately in the module.

The FBX importer operates against a transient duplicate of a reused Skeleton.
A changed bone tree or reference pose is refused before binding the imported
asset to the original Skeleton. A factory-created, never-saved Skeleton is
duplicated to an explicitly requested new package before publication, avoiding
redirectors and unrelated referencer saves. No existing Skeleton is extended.
Failures can leave new unsaved objects or a subset of saved new packages;
inspect the result and editor state rather than blindly retrying or overwriting.

The Automation group `Wooloo.Assets.Production` includes native PNG import/save,
overwrite and scope refusals, private-bone preview ownership, and read-only
inspection, stale-hash/dirty/split-package reimport refusals, guarded PMC updates,
unresolved preview paths, explicit unnamed-slot aliases/collision refusals and
paired raw/compressed animation sampling. Fixture
files are generated under `Saved/Automation/WoolooAssets/`.
These tests are Debug/Development-only and require explicit execution approval.
Adding sources does not load the tools: an approved Editor build/restart and
live `describe_toolset` check are required before using them.

The editor-only `WoolooAssets` module registers
`WoolooAssets.WoolooAssetToolset` with Unreal's official Toolset Registry.
It adapts existing Asset Registry and AssetTools operations. The caller owns the
asset mapping, allowed roots, protected roots, approvals and migration records.
There are no project-specific paths or persistent Python entry points here.

Build the host Editor target and fully restart the editor before using this module.
Start `ModelContextProtocol.StartServer`, refresh with
`ModelContextProtocol.RefreshTools`, and discover the toolset's live schema.
Python can call the same reflected facade as `unreal.WoolooAssetToolset`.
Writing these sources does not load the module into an existing editor.

## Operations

| Tool | Behavior |
| --- | --- |
| `PlanMoves` | Read-only validation of exact `SourcePackage` / `DestinationPackage` pairs; returns disk-registry `ReferencerPackages` outside the source set. No asset loads, saves or repairs. |
| `MoveAssets` | Repeats planning and checks `ApprovedReferencerPackages`, then calls native `IAssetTools::RenameAssets` once for the explicit batch. |
| `InspectRedirectors` | Read-only inspection of explicit old package names and their referencers. Missing old packages are ignored; existing non-redirector packages are rejected. |
| `FixupRedirectors` | Repeats redirector inspection and approval checks, replaces soft paths through native AssetTools, saves exact referencers and safely deletes unreferenced redirectors. |

Package names have no object suffix: `/Example/Characters/Hero/SKM_Hero`, not
`/Example/Characters/Hero/SKM_Hero.SKM_Hero`. Allowed roots must be directories
below a mounted content root, such as `/Example/Characters`; entire mounts such
as `/Game` or `/Example` are rejected. Paths under `ProtectedPackageRoots` are
excluded from both moves and referencer saves, even inside an allowed root.
Engine content is always excluded. Move sources must contain exactly one saved
asset; redirectors and maps are not move sources. Map referencers are supported
by the native rename/fixup behavior.
Source and destination must use the same content mount, avoiding native
cross-plugin moves that can change additional dependencies' public visibility.

Preflight rejects duplicate destinations, chained/case-only renames, existing
destinations on disk or in memory, read-only files and incomplete registry discovery.
Authoring requires the editor game thread, stopped PIE and no dirty content or
map packages. Dirty-package refusal protects editor changes because native
rename/fixup also examines dirty packages when replacing soft object paths.
Loads that dirty packages stop the operation before native authoring begins.

`ApprovedReferencerPackages` contains the exact packages returned by a fresh
planning/inspection call. New referencers cause authoring to stop so the caller
can review the additional saves. Referencers inside the selected move/redirector
set are implicit. The registry view covers saved package references; it cannot
find string literals in code, external manifests or files. The consumer must
update those separately.

## Native side effects and completion

Native rename updates references and saves renamed assets, referencers and any
redirectors it needs to retain. It is not a staging-only or transactional operation.
`CompletedPackages` reports destinations whose loaded asset is in the expected
package, exists on disk and is clean. A failure can leave a partial batch; inspect
the result and disk state before planning only the remaining moves. There is no
automatic rollback, overwrite or retry.

Redirector repair is a separate explicit write. This engine's
`IAssetTools::FixupReferencers` opens a modal report, so the adapter uses public
non-modal native primitives: `RenameReferencingSoftObjectPaths`, exact-package
`UPackage::SavePackage`, collection redirect notification, and safe deletion after
disk/memory/undo reference checks. It never force-replaces loaded references or
clears the undo buffer to remove a redirector. Unresolved references retain it.
`SavedReferencerPackages` reports explicit saves even if a later step fails.
Configured source-control behavior (checkout/add/delete/status) of native move/delete remains native;
these tools do not commit, submit or push. Read-only files must be made writable
before authoring. `RemainingRedirectors` and `Issues` report incomplete repair;
the adapter never force-deletes files to claim success. An empty remaining list
only confirms removal of the selected redirector packages, not validation of all
external strings or runtime behavior.

## Editor verification

After compiling/restarting, inspect the registered schema and use `PlanMoves` on
a reviewed fixture before authoring. Check that an occupied destination, duplicate
destination, protected referencer, engine path or dirty editor package causes a
refusal without saving. Verify an explicit successful move keeps asset contents,
hard/soft references and graph layout, then inspect and repair only its redirectors.
Review all touched packages and collection/source-control changes. Runtime tests
and C++ builds are separate user actions.
