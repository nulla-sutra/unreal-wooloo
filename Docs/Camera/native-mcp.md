# Native camera tools

The editor-only `WoolooCamera` module owns Camera Rig authoring capabilities.
It depends on Unreal's `GameplayCameras` runtime interfaces and native editor
transactions. Material and MPC authoring stays in `WoolooMaterial`; asset
migration stays in `WoolooAssets`. No consuming-plugin classes, paths or recipes
are embedded in this module.

The module registers `WoolooCamera.WoolooCameraToolset` with the official Toolset
Registry at `PostEngineInit`. After an explicitly requested Editor build and
restart, start `ModelContextProtocol.StartServer`, refresh tools if needed, and
inspect `list_toolsets` and `describe_toolset` before using `call_tool`. Adding
source alone does not expose the tool in an already-running editor.

## ReplaceCameraRigNode

```cpp
static FString ReplaceCameraRigNode(
    const FString& AssetPath,
    const FString& AllowedPackageRoot,
    const FString& NodePath,
    const FString& NewNodeClassPath,
    const TMap<FString, FString>& Properties,
    const FString& NodeComment);
```

- `AssetPath`: existing Camera Rig package name without an object suffix, for
  example `/Example/Camera/Rigs/CR_Example`. Redirectors are not followed for edits.
- `AllowedPackageRoot`: explicit mounted directory below a mount point, for
  example `/Example/Camera/Rigs`. Entire mounts and engine-owned content are refused.
- `NodePath`: exact, case-sensitive UObject path relative to that Rig, as returned
  by `Node->GetPathName(Rig)`. It is neither a graph GUID nor a display title.
- `NewNodeClassPath`: full object path of a concrete `UCameraNode` subclass.
- `Properties`: top-level editable field names and native `ImportText` values.
  Struct fields use Unreal text such as `(Value=12.5)`. Unknown fields, nested
  property paths, case aliases, incomplete imports, instanced ownership and
  non-editable fields are refused. `bIsEnabled` is reserved for state preservation.
- `NodeComment`: replacement node comment. An empty string clears it.

Example with a caller-provided mount and a previously inspected node path:

```cpp
UWoolooCameraToolset::ReplaceCameraRigNode(
    TEXT("/Example/Camera/Rigs/CR_Example"),
    TEXT("/Example/Camera/Rigs"),
    TEXT("FieldOfViewCameraNode_0"),
    TEXT("/Script/GameplayCameras.ClippingPlanesCameraNode"),
    {{TEXT("NearPlane"), TEXT("(Value=12.5)")}},
    TEXT("Set camera clipping planes"));
```

The returned `FString` contains JSON fields `success`, `asset_path`,
`old_node_path`, `new_node_path`, `dirty`, and `message`. Both node paths are
Rig-relative. On refusal, `new_node_path` is empty and `message` explains the
reason. `dirty` reports the resolved Rig package's current state, including any
pre-existing edits; it is false when no Rig was resolved. Success leaves the Rig
dirty and needing a native build. Retain the new path for subsequent operations;
the old path is not an alias for the replacement.

## Replacement boundary

Replacement requires the editor game thread, stopped PIE, no active transaction,
and a closed target Camera Rig asset editor. The native toolkit owns a separate
cached `UEdGraph`; its graph rebuild method is not an exported public interface.
The tool refuses an open target editor instead of leaving a stale graph editable.
It does not close or reopen windows. Open the Rig after replacement to inspect
the updated graph; close it again before using the tool for another replacement.
Keep that editor closed while undoing or redoing the replacement as well, then
reopen it to reconstruct the view from the restored objects. This first version
provides asset-data Undo/Redo, not synchronization of an already-open graph cache.

Both the original and configured replacement must be leaf nodes. Empty child
slots are allowed. The original must belong to the Rig and be registered in its
native node-tree graph. A disconnected registered leaf is supported. All direct
and array execution slots referencing the node are preserved in place, including
duplicate slots and surrounding null entries. The Rig root is supported as an
incoming slot. Custom child storage, incompatible slot types, and an old node
owning other registered graph objects are refused.

Data connections to or from the old node and exposed parameter bindings prevent
replacement. Native recursive reflection also checks Rig-owned objects for
references outside supported execution slots and node-tree registration. The
tool does not perform project-wide reference fixup or migrate custom native,
non-reflected storage. Persistent external references to Rig-internal nodes are
outside this operation's contract. Transient editor objects are not migrated.

All property imports are staged on a transient candidate before the Rig is
modified. The new node starts with its class defaults and receives only explicit
properties, the original enabled state and graph coordinates, and `NodeComment`.
Other old-node values are not copied, even for a same-class replacement. Graph
object references cannot be introduced through `Properties`. Class-specific
`Within` requirements that prevent transient staging are unsupported.

One native transaction changes the Rig's execution references and object-tree
registration. The new UObject retains the original outer and receives a unique
name. The old UObject remains intact for Undo; no class or material is deleted.
Existing comment boxes, other graph objects, parameters and unrelated data
connections are preserved. The tool does not lay out the whole graph; verify
the replacement's size, connections and comment fit in the existing group when
inspecting the asset.

No C++ compilation, Camera Rig build, package save, source-control operation or
PIE launch is requested. Building and saving require separate actions. A later
normal save invokes `UCameraRigAsset::PreSave`, which natively builds the Rig;
save should therefore not be treated as a build-free operation.

## Verification

Explicit Automation cases under `Wooloo.Camera.ReplaceRigNode` cover array and
root replacement, disconnected leaves, slot order and duplicates, default values,
comments and position, unrelated connections, native registration, Undo/Redo,
and refusal of invalid scope, nodes, classes, property text and bindings without
Rig mutation. Fixtures use unique unsaved packages and preserve the user's Undo
history. No test asset is saved or built. Test code is guarded by
`WITH_DEV_AUTOMATION_TESTS` and Debug/Development.

Run those cases only after a separately authorized build and test request.
For integration verification, inspect the live MCP schema, replace a leaf on an
approved fixture Rig with its editor closed, open it to inspect connections and
layout, close it before exercising Undo/Redo, reopen to inspect the result, and
separately request its native build/save.
These checks have not been executed merely by adding the source files.
