# Wooloo

Wooloo is a standalone Unreal Engine editor plugin for reusable MCP capabilities.
It uses Unreal's official Toolset Registry and Model Context Protocol plugins.
Capabilities are organized into separate modules by domain.

## Modules

| Module | Type / loading phase | Responsibility |
| --- | --- | --- |
| `WoolooMutable` | Editor / PostEngineInit | Native Mutable graph and asset operations, shared reflected types, and seven MCP/Python tools |

The Mutable toolset is `WoolooMutable.MutableToolset`. Python uses the same
reflected facade through `unreal.MutableToolset`.

## Integration

1. Place the plugin under the host project's Plugins directory.
2. Enable `Wooloo` in the project descriptor or a consuming plugin descriptor.
   For an editor-only dependency, use `"TargetAllowList": ["Editor"]`.
3. Build the host project's Editor target and fully restart the editor.
4. Start Unreal's official MCP server and discover the registered toolset using
   the [Mutable tool guide](Docs/Mutable/native-mcp.md).

The plugin descriptor declares Editor-only dependencies on `Mutable`,
`ToolsetRegistry`, and `ModelContextProtocol`. Wooloo has no runtime module.
C++ consumers of the native service must also declare a `WoolooMutable` module
dependency; MCP and Python consumers use its reflected tools.

The module registers its toolset during startup and unregisters it during
shutdown. Server startup and asset operations are explicit actions.

## Ownership contract

Callers supply `OwnerMetadataKey` for ownership inspection and all write
operations. They also supply `AllowedPackageRoot` and `ExpectedOwner` for writes.
The metadata key and owner value are caller-owned conventions.

The tools validate the key, package scope, and expected owner before writes.
Creation tags only newly created assets. Graph replacement, resource compilation,
and saving are separate operations. Recipes, batch workflows, asset paths, and
integration policies belong to consumers.

## Source layout

- `Source/WoolooMutable/Graph/`: native graph service and shared graph types.
- `Source/WoolooMutable/Tools/`: reflected tools and result envelopes.
- `Source/WoolooMutable/WoolooMutable.cpp`: toolset registration lifecycle.
- `Docs/Mutable/`: API, integration, and editor verification instructions.

Headers and implementations stay together by feature. The UE 5.8 integration
reads native hierarchy and compilation interfaces from
`CustomizableObject/Internal`; the module rules add that include path.
Review this integration point when upgrading Unreal.

