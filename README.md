# Wooloo

Wooloo is a standalone Unreal Engine editor plugin for reusable MCP capabilities.
It uses Unreal's official Toolset Registry and Model Context Protocol plugins.
Capabilities are organized into separate modules by domain.

## Modules

| Module            | Type / loading phase    | Responsibility                                                                                       |
|-------------------|-------------------------|------------------------------------------------------------------------------------------------------|
| `WoolooMutable`   | Editor / PostEngineInit | Native Mutable graph and asset operations, shared reflected types, and seven MCP/Python tools        |
| `WoolooStateTree` | Editor / PostEngineInit | Native StateTree parameters, instance data, hierarchy, transitions, bindings, compilation and saving |
| `WoolooBlueprint` | Editor / PostEngineInit | Blueprint graph inspection, targeted edits, layout/comments, compilation and saving                  |
| `WoolooAssets`    | Editor / PostEngineInit | Explicit asset-move planning, native batch moves, redirector inspection and native reference fixup   |
| `WoolooWidget`    | Editor / PostEngineInit | WidgetBlueprint layout ownership inspection, validated local/inherited root changes and Undo        |
| `WoolooMaterial`  | Editor / PostEngineInit | Material-domain tools; native creation of unsaved Material Parameter Collections                    |
| `WoolooCamera`    | Editor / PostEngineInit | Camera Rig leaf-node replacement with native execution slots, graph ownership and Undo              |
| `WoolooPIE`       | Editor / PostEngineInit | Read-only multi-world PIE snapshots and explicit local-player single-frame Enhanced Input injection |

The Mutable toolset is `WoolooMutable.MutableToolset`. Python uses the same
reflected facade through `unreal.MutableToolset`.

The additional toolsets are `WoolooStateTree.WoolooStateTreeToolset` and
`WoolooBlueprint.WoolooBlueprintToolset`. Their native services do not depend on
consuming gameplay plugins. See [StateTree tools](Docs/StateTree/native-mcp.md)
and [Blueprint tools](Docs/Blueprint/native-mcp.md).

`WoolooAssets.WoolooAssetToolset` exposes the [asset migration tools](Docs/Assets/native-mcp.md).
It reuses Unreal's AssetTools and Asset Registry, with no consuming-plugin paths
or migration recipes in the module.

`WoolooWidget.WoolooWidgetToolset` exposes the [Widget layout tools](Docs/Widget/native-mcp.md).
It uses native UMG editor APIs to inspect source/compiled layout ownership and
switch between local and inherited layouts without rewriting graph logic.

`WoolooMaterial.WoolooMaterialToolset` exposes the [material tools](Docs/Material/native-mcp.md).
It creates unsaved MPCs with native factories and caller-supplied scopes and
defaults. Material-domain operations remain separate from asset migration and
camera authoring; there are no consuming-plugin dependencies or conventions.

`WoolooCamera.WoolooCameraToolset` exposes the [camera tools](Docs/Camera/native-mcp.md).
It replaces registered Camera Rig leaf nodes with explicit properties, preserving
execution slots, enabled state and position. Bindings and unsupported references
are refused; native transactions support Undo. The Rig remains unsaved and needs
a build.

## Integration

`WoolooPIE.WoolooPIEToolset` exposes the [native PIE diagnostics tools](Docs/PIE/native-mcp.md).
It identifies worlds by their engine context handles and verifies exact Controller/Pawn paths.
Input goes through Enhanced Input on the normal game loop. Call its input tool directly
through Unreal's official MCP server; Python and editor-script bridges are refused.
This module is included only in Debug, DebugGame and Development Editor targets.

1. Place the plugin under the host project's Plugins directory.
2. Enable `Wooloo` in the project descriptor or a consuming plugin descriptor.
   For an editor-only dependency, use `"TargetAllowList": ["Editor"]`.
3. Build the host project's Editor target and fully restart the editor.
4. Start Unreal's official MCP server and discover the registered toolset using
   the [Mutable tool guide](Docs/Mutable/native-mcp.md).

The plugin descriptor declares Editor-only dependencies on `Mutable`, `StateTree`,
`EnhancedInput`, `GameplayAbilities`, `PropertyBindingUtils`, `GameplayCameras`, `ToolsetRegistry`, and
`ModelContextProtocol`. Wooloo has no runtime module. C++ consumers declare the
owning capability module as a dependency; MCP and Python consumers use that
module's reflected tools.

The module registers its toolset during startup and unregisters it during
shutdown. Server startup and asset operations are explicit actions.

## Ownership contract

Mutable callers supply `OwnerMetadataKey` for ownership inspection and all write
operations. They also supply `AllowedPackageRoot` and `ExpectedOwner` for writes.
The metadata key and owner value are caller-owned conventions.

The tools validate the key, package scope, and expected owner before writes.
Creation tags only newly created assets. Graph replacement, resource compilation,
and saving are separate operations. Recipes, batch workflows, asset paths, and
integration policies belong to consumers.

Blueprint and StateTree edits are targeted by graph/node/state IDs and require an
explicit narrow `AllowedPackageRoot`. They reject paths outside that directory,
engine-owned content, and writes during PIE. Creation, edits, compilation and saving
are separate actions. Read tools do not repair subjects. Instance values are staged
before imports; no generic array edit-chain notifications are fabricated.

Asset migration callers supply exact source/destination package pairs, narrow
allowed roots, protected roots and an explicit list of approved referencer packages.
Read-only planning does not load assets. Native rename/fixup may save referencers,
update collections and use configured source control. Authoring requires a clean
editor and stopped PIE; it does not build C++, run gameplay, or automatically start MCP.

Widget layout writes require a narrow `AllowedPackageRoot` and explicit permission
to discard an existing root subtree. Dependency and Named Slot checks precede the
transaction. Structural notification refreshes the Blueprint skeleton; full
compilation and saving remain separate actions. Custom binding extensions block
subtree removal until their dependencies can be inspected.

Camera Rig replacement requires a narrow `AllowedPackageRoot`, an exact Rig-relative
node path, stopped PIE and a closed target asset editor. Properties are validated
on a transient candidate before changing the Rig. New nodes use new-class defaults
plus explicit values; material setup and consumer migration remain separate.

## Source layout

- `Source/WoolooMutable/Graph/`: native graph service and shared graph types.
- `Source/WoolooMutable/Tools/`: reflected tools and result envelopes.
- `Source/WoolooMutable/WoolooMutable.cpp`: toolset registration lifecycle.
- `Docs/Mutable/`: API, integration, and editor verification instructions.
- `Source/WoolooStateTree/`: StateTree native service and reflected tool facade.
- `Source/WoolooBlueprint/`: Blueprint native graph service and reflected tool facade.
- `Docs/StateTree/` and `Docs/Blueprint/`: tool contracts and editor verification.
- `Source/WoolooAssets/`: native AssetTools adapter and reflected tool facade.
- `Docs/Assets/`: asset migration contracts and editor verification.
- `Source/WoolooWidget/`: UMG layout service, reflected tools and explicit Automation cases.
- `Docs/Widget/`: layout inheritance contracts and editor verification.
- `Source/WoolooMaterial/`: native material service, reflected tools and explicit Automation cases.
- `Docs/Material/`: material tool contracts and editor verification.
- `Source/WoolooCamera/`: native Camera Rig service, reflected tool and explicit Automation cases.
- `Docs/Camera/`: leaf replacement contract, limitations and editor verification.
- `Source/WoolooPIE/`: native PIE selection, snapshots, one-frame input tools and explicit Automation cases.
- `Docs/PIE/`: native MCP connection, targeting, input contracts and multiplayer verification.

Headers and implementations stay together by feature. The UE 5.8 integration
reads native hierarchy and compilation interfaces from
`CustomizableObject/Internal`; the module rules add that include path.
Review this integration point when upgrading Unreal.

