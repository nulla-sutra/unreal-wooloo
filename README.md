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

## Integration

1. Place the plugin under the host project's Plugins directory.
2. Enable `Wooloo` in the project descriptor or a consuming plugin descriptor.
   For an editor-only dependency, use `"TargetAllowList": ["Editor"]`.
3. Build the host project's Editor target and fully restart the editor.
4. Start Unreal's official MCP server and discover the registered toolset using
   the [Mutable tool guide](Docs/Mutable/native-mcp.md).

The plugin descriptor declares Editor-only dependencies on `Mutable`, `StateTree`,
`PropertyBindingUtils`, `ToolsetRegistry`, and `ModelContextProtocol`. Wooloo has no
runtime module. C++ consumers declare the owning capability module as a dependency;
MCP and Python consumers use that module's reflected tools.

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

Headers and implementations stay together by feature. The UE 5.8 integration
reads native hierarchy and compilation interfaces from
`CustomizableObject/Internal`; the module rules add that include path.
Review this integration point when upgrading Unreal.

