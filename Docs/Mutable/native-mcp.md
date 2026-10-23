# Mutable MCP and Python tools

`WoolooMutable` is an Editor module providing native Mutable authoring operations.
Its `UMutableToolset` derives from `UToolsetDefinition` and exposes seven static
`BlueprintCallable` / `AICallable` functions.

The module loads at `PostEngineInit`, registers its class with
`UToolsetRegistry::RegisterToolsetClass`, and unregisters it during shutdown.
Unreal's official MCP discovers **`WoolooMutable.MutableToolset`** through that
registry. Python calls the same facade as `unreal.MutableToolset`.

## Tool contract

All asset paths are long package paths without an object suffix, such as
`/Game/Example/CO_Example`. Inspect the discovered schema before invoking a tool.

| Tool | Inputs | Behavior |
| --- | --- | --- |
| `ListNodeTypes` | None | Lists the eight supported node classes and accepted property names, types, and defaults. |
| `DescribeObject` | `AssetPath`, `OwnerMetadataKey` | Reads the selected ownership tag, dirty state, nodes, properties, GUIDs, layout, live pins, and connections. |
| `CreateObject` | `AssetPath`, `AllowedPackageRoot`, `OwnerMetadataKey`, `ExpectedOwner` | Creates an unsaved CO using Mutable's native factory and tags the new asset. An existing matching owned CO is reused without modification. |
| `ApplyGraph` | Write inputs above, then `Graph` | Replaces the complete graph in memory after validation and participates in editor undo. Does not compile or save. |
| `CompileObject` | Write inputs above | Synchronously compiles an explicitly selected owned root CO and returns this request's callback flags. Does not save. |
| `GetParameters` | `AssetPath` | Reads compiled parameter names, types, scalar defaults, named options, and output components. |
| `SaveObject` | Write inputs above | Saves only the selected owned CO package. Does not save dependencies or request source-control checkout. |

`DescribeObject` and all four write operations require an explicit
`OwnerMetadataKey`. The key must be non-empty, must not be `None`, and must fit
the native `FName` length limit. The toolset does not select a metadata namespace
for the caller.

For writes, `ExpectedOwner` must be non-empty and match the value stored under
the supplied key. `AllowedPackageRoot` defines the writable content boundary.
Existing unowned or differently owned assets are rejected rather than adopted.
Read-only inspection may return an empty owner and never establishes ownership.

Results contain success, message, path, and dirty/created/saved state. Graph,
compilation, and parameter responses add their corresponding structured data.

## Complete graph updates

`ApplyGraph` takes `FMutableToolGraph`, containing node and link arrays.
Each node has `Id`, `ClassPath`, `Guid`, `X`, `Y`, and `Name`/`Value`
property pairs. Links contain `FromNode`, `FromPin`, `ToNode`, and `ToPin`.

Use classes and properties returned by `ListNodeTypes`, and live pin names from
`DescribeObject`. String/name values are raw text; asset references and structs
use Unreal ImportText syntax. Preserve existing node GUIDs and binding identifiers.

The service stages and validates a complete replacement before assigning Source.
Omitted nodes and links are removed. Invalid connections, parent cycles, and
missing parent graph roots are rejected. A described graph is suitable for
round-trip editing only when `bCanApply` is true; unsupported nodes, custom pin
defaults, orphaned pins, and split pins can prevent a lossless replacement.

## Compilation and parameters

Resource compilation operates on a root CO. Save child COs first so registry
discovery can find them. Success requires this request's callback to report a
completed compilation without request failure, errors, or a skipped request.
Warnings are reported separately; detailed messages remain in `LogMutable`.

`GetParameters` rejects busy, uncompiled, or stale roots, including graphs edited
by the toolset but not successfully compiled. It reads defaults through a
transient COI without requesting mesh generation. Bool/Int/Float defaults are
returned as text; other types remain listed without a claimed scalar default.

Calls run synchronously on the game thread. Issue resource compilations serially
and allow enough client time for the graph size. Compilation can write Mutable's
normal derived data and gathered references.

## Python example

After building and restarting the editor, inspect an existing CO explicitly:

```python
import unreal

snapshot = unreal.MutableToolset.describe_object(
    "/Game/Example/CO_Example",
    "Example.Mutable.Owner",
)
```

Replace the example path and metadata key with the caller's values. Python write
calls use the input order shown in the tool table. The caller retains its own
recipe validation, signatures, and batch orchestration.

## Enable and verify

1. Enable Wooloo, build the host project's Editor target, and fully restart the
   editor so its reflected declarations are loaded.
2. Start Unreal's native server with `ModelContextProtocol.StartServer`.
   Connect to the endpoint reported by the editor.
3. If discovery preceded module registration, run
   `ModelContextProtocol.RefreshTools`. Use `list_toolsets`, then
   `describe_toolset` for `WoolooMutable.MutableToolset`, then `call_tool`
   according to the returned schema.
4. Begin with `ListNodeTypes` and `DescribeObject` on an existing CO.
5. In an explicitly approved scratch scope, verify creation, complete graph
   application, child saving, root compilation, parameter inspection, and root
   saving. Check wrong owner values, missing or invalid metadata keys, sibling
   directory boundaries, invalid links, and editor undo.

The descriptor enables Mutable, Toolset Registry, and Unreal MCP only for Editor
targets. The native service uses UE 5.8 hierarchy/compile-status interfaces from
`CustomizableObject/Internal`; its module rules expose that header directory
without changing engine source.

