# Native asset migration tools

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
