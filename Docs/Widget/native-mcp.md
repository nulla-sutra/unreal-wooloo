# Native Widget layout tools

The editor-only `WoolooWidget` module registers
`WoolooWidget.WoolooWidgetToolset` with Unreal's official Toolset Registry.
It uses UMG, UMGEditor, BlueprintGraph and UnrealEd. It has no dependency on
WoolooBlueprint or any consuming gameplay plugin.

Build the host Editor target and restart the editor before discovering the new
toolset. Follow the [native server discovery instructions](../StateTree/native-mcp.md)
and inspect the registered tool definitions before invoking them. Loading this
module only registers tools; it does not author assets, start the server, or run tests.

## DescribeWidgetLayout

`DescribeWidgetLayout(AssetPath)` accepts an existing WidgetBlueprint's long
package name, such as `/Game/UI/WBP_Child`. It synchronously returns
`FWoolooWidgetResult { bSuccess, Message, Json }`.

The JSON contains:

- `local`: local WidgetTree/root, owning Blueprint/class and compile status.
- `ancestors`: the parent chain, with each layout's source and root.
- `source_mode`: `Local`, `Inherit` or `Empty`, derived from current authoring data.
- `source_layout`: the first source hierarchy with a root. Ancestor Blueprint
  sources take precedence over their potentially stale generated trees.
- `compiled_layout`: the native generated class's layout owner/root, availability,
  ownership comparison and `may_be_stale` flag. Matching owners do not prove that
  every compiled widget property is current.
- `named_slot_bindings`: the local, independent Named Slot content assignments.
- `diagnostics`: competing roots, missing layouts and source/compiled disagreement.
- `changed` and `blocking_issues`: false/empty for a successful read.

Inspection does not instantiate UserWidgets, repair data, explicitly compile, or
save. Loading an unloaded asset uses normal Unreal loading, including any native
compile-on-load behavior. `bSuccess` means inspection succeeded, not that the
layout is valid. Invalid paths and non-WidgetBlueprint assets return failure.

Unreal has no separate layout-inheritance switch: a local root supplies the local
hierarchy; a rootless child searches its ancestors. Parent and child roots can
produce a compiler **Note**, rather than an error or warning, even though Unreal
does not support these competing hierarchies. A clean error count alone cannot
validate layout inheritance.

## SetWidgetLayout

`SetWidgetLayout(AssetPath, AllowedPackageRoot, Change)` uses the same result
envelope. `AllowedPackageRoot` is a narrow content directory such as `/Game/UI`,
not an entire mount. Writes reject PIE, off-thread calls, engine-owned files,
read-only packages, redirected asset paths, and packages outside this directory.

`FWoolooWidgetLayoutChange` fields:

| Field | Contract |
| --- | --- |
| `Mode` | `Inherit` or `Local`; defaults to Inherit |
| `RootWidgetClassPath` | Full concrete UWidget class path for Local; empty for Inherit |
| `RootWidgetName` | Exact root object name for Local; None for Inherit |
| `bDiscardLocalRoot` | Explicit permission to remove an existing local root **and its descendants**; defaults to false |

Examples, using the reflected argument names:

```json
{
  "AssetPath": "/Game/UI/WBP_Child",
  "AllowedPackageRoot": "/Game/UI",
  "Change": { "Mode": "Inherit", "bDiscardLocalRoot": true }
}
```

```json
{
  "AssetPath": "/Game/UI/WBP_Base",
  "AllowedPackageRoot": "/Game/UI",
  "Change": {
    "Mode": "Local",
    "RootWidgetClassPath": "/Script/UMG.CanvasPanel",
    "RootWidgetName": "LayoutRoot"
  }
}
```

Inherit requires exactly one ancestor hierarchy with a root. Local requires no
ancestor root. Multiple ancestor roots are reported for the caller to resolve;
the tool does not modify a parent asset. Local creates a fresh root with class
defaults. Replacing a root removes its old subtree; it does not migrate properties,
children or slots to the replacement. Selecting the same root class/name, or
selecting an already inherited layout, is an idempotent no-op after validation.

Before removal, the tool checks the target Blueprint's standard graph references,
property/delegate bindings, animation bindings, desired focus, retained widget
navigation and native BindWidget properties. It reports blockers instead of
deleting the referencing logic. Custom Blueprint extensions can own opaque
bindings, so their presence blocks subtree removal in this first implementation.
The tool does not remove those extensions or pretend to validate their schemas.
It does not scan or rewrite references in other assets; explicitly compile
dependent widgets after a structural change.

Independent Named Slot content remains owned by the child's WidgetTree. A switch
is rejected if a preserved assignment targets an absent, instance-only or already
filled inherited slot, shares widgets with the discarded subtree, or conflicts
with inherited names. Slot inspection reads current ancestor sources, so unsaved
edits are not hidden behind old compiled slot availability. Local cannot retain
assignments that have no inherited slot provider.

Validation finishes before starting the transaction. Rejected operations return
`changed=false` and `blocking_issues`, without deleting widgets, graph nodes,
bindings or animations. Successful edits use the native WidgetTree, transactional
object rename and Blueprint variable bookkeeping. Unreal's structural notification
refreshes the skeleton; **full Blueprint compilation and saving remain explicit**.
The result includes the post-edit source snapshot and the separate compiled snapshot.
There is no C++ compilation, gameplay run, file save or source-control operation.

Compile/save can use the existing
[WoolooBlueprint tools](../Blueprint/native-mcp.md); they are separate MCP calls,
not a C++ module dependency. Full compilation must succeed before treating the
generated layout as current. Undo/Redo belongs to the editor's normal transaction
history, including removed root identity and independent Named Slot content.

## Verification after building

`Source/WoolooWidget/Tests/WidgetLayoutTests.cpp` registers the explicit Automation
group `Wooloo.Widget.Layout` in Debug/Development editor builds. It creates unique
transient in-memory fixture packages; it does not save assets or run gameplay.
The cases cover:

- Multi-level source inheritance and stale compiled ownership, with read-only inspection.
- Discard opt-in, inherited layout selection, Named Slot identity, Undo/Redo, and explicit Blueprint compilation.
- Graph, property and animation dependency rejection without deleting their data.
- Missing or occupied Named Slots, including unsaved ancestor changes.
- Local root creation, no-op requests, same-name replacement, self-nesting and package scope rejection.

Run this group explicitly in a disposable editor session: its transactions append
to the normal Undo history, which is intentionally not reset by the fixture.
Then inspect the registered MCP schemas and exercise both tools on an approved
scratch WidgetBlueprint. Compile, save, reopen, and inspect the inherited layout
and Named Slot content in the editor. Also check dirty parent classes, invalid
names/classes, custom binding extensions, engine plugin content and read-only assets.

These C++ tests, module compilation and editor/MCP integration checks have **not**
been run during authoring. Static source inspection is not a substitute for them.
