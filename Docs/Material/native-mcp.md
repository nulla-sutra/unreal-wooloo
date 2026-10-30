# Native material tools

## Standard surface materials

`CreateSurfaceMaterial(AssetPath, AllowedPackageRoot, BaseColor, BaseColorTexture,
OpacityMaskTexture, Roughness, bTwoSided, bUsedWithSkeletalMesh)` creates and saves
one new native Material. Texture arguments are package paths without object
suffixes and must identify existing Texture2D assets. Empty BaseColorTexture
uses the supplied linear RGB constant. A mask connects its Alpha output and
selects Masked; otherwise the result is Opaque. When both inputs use the same
texture, they share one sample node. Roughness must be finite and in [0, 1].

The adapter uses native MaterialEditingLibrary graph/compilation operations,
with explicit node positions and usage flags. It does not infer extra PBR
connections, overwrite existing materials, start PIE or compile C++. Results
report `bCreated` and `bSaved` separately: compilation/save failure can leave the
new material in memory, and must not be retried blindly. Only the requested
new material package is saved; referenced textures are not modified.

`Wooloo.Material.Surface` Automation cases exercise native graph creation/save,
usage/color settings, overwrite protection and invalid input refusal. Fixture
assets live under a temporary mount backed by `Saved/Automation/WoolooMaterial/`.
Compilation, Automation execution, editor restart and live schema verification
remain explicit steps; adding these sources does not execute those steps.

The editor-only `WoolooMaterial` module owns material, material-function and
Material Parameter Collection editing capabilities. Its first tool creates MPCs.
Asset migration remains in `WoolooAssets`; camera authoring belongs to its own
capability domain. This module has no consuming-plugin dependencies or paths.

The module registers `WoolooMaterial.WoolooMaterialToolset` with Unreal's official
Toolset Registry at `PostEngineInit`. Build the host Editor target and restart
the editor, then start `ModelContextProtocol.StartServer` and refresh
`ModelContextProtocol.RefreshTools` if needed. Inspect `list_toolsets` and
`describe_toolset` before invoking `call_tool`. Source changes alone do not load
the module or expose its reflected tool in an already-running editor.

## CreateMaterialParameterCollection

```cpp
static FString CreateMaterialParameterCollection(
    const FString& AssetPath,
    const FString& AllowedPackageRoot,
    const TMap<FName, float>& ScalarParameters,
    const TMap<FName, FLinearColor>& VectorParameters);
```

- `AssetPath` is a long package name without an object suffix, such as
  `/Example/Materials/Collections/MPC_Example`.
- `AllowedPackageRoot` is an explicit directory below a mounted content root,
  such as `/Example/Materials/Collections`. Entire mounts are rejected.
- `ScalarParameters` supplies scalar names and default values.
- `VectorParameters` supplies names and RGBA default values; alpha is retained.

Supply at least one scalar or vector parameter. An empty new collection has no
initialized native parameter-buffer layout; this tool refuses it before creation.

Example C++ invocation, using a content mount provided by the caller:

```cpp
UWoolooMaterialToolset::CreateMaterialParameterCollection(
    TEXT("/Example/Materials/Collections/MPC_Example"),
    TEXT("/Example/Materials/Collections"),
    {{TEXT("Blend"), 0.f}, {TEXT("Scale"), 1.f}},
    {{TEXT("Tint"), FLinearColor(0.2f, 0.3f, 0.4f, 1.f)}});
```

The returned string contains JSON fields `success`, `asset_path`, `created`,
`dirty`, and `message`. `created` and `dirty` describe the asset created by this
invocation, not a pre-existing destination. If the native factory fails after
creating an unexpected object, the response reports that partial creation;
inspect it before retrying. There is no automatic overwrite, retry or deletion.

## Creation boundary

Creation requires the editor game thread, stopped PIE and completed Asset
Registry discovery. Package names, directory boundaries, mounted paths and
engine ownership are validated before the native factory is called. Existing
packages on disk, registered assets and loaded packages, including empty ones,
are refused. The tool never saves unrelated dirty editor assets.

Names must be non-empty and unique across both parameter types, using Unreal's
case-insensitive name identity. Within each type, numeric-suffix aliases that
native MPC editing would silently rename, such as `Value` and `Value0`, are
refused. Numeric suffixes must fit an int32. Defaults must be finite in every
channel. Storage must fit the engine's configured `r.MPC.MaxVectorStorage`
budget: four scalars share a vector slot and each vector uses one slot.

The adapter uses native AssetTools and `UMaterialParameterCollectionFactoryNew`,
then assigns parameters in lexical order with native parameter identifiers.
Native `PreEditChange` / `PostEditChange` notifications initialize the buffer
layout and update world instances. These native notifications may refresh or
recompile open material previews; the tool does not request a C++ build or an
explicit material compilation operation. It does not start PIE or open an editor
tab. The new collection is registered in memory and its package remains dirty.
No package is saved and no source-control operation is requested. Inspect and
save the exact package separately using an existing approved save operation.

## Verification

The explicit Automation cases under `Wooloo.Material.ParameterCollection` cover
native creation, exact scalar/RGBA defaults, unique identifiers and initialized
buffer layout; refusal to overwrite an existing collection or empty package;
and scope, name and non-finite-value rejection before package creation.
Fixtures use unique unsaved packages and remove only their own registry entries.
All test code is guarded by `WITH_DEV_AUTOMATION_TESTS` and Debug/Development.

After an explicitly requested build and editor restart, inspect the registered
live MCP schema. Run the Automation group only when explicitly authorized, then
verify discovery and creation on a separately approved fixture path. Builds,
tests, runtime runs and asset saves are separate operations. These checks have
not been executed merely by adding the source files.
