# Native Blueprint tools

The editor-only `WoolooBlueprint` module registers
`WoolooBlueprint.WoolooBlueprintToolset`. It depends on native Blueprint editor
APIs, EnhancedInput, InputBlueprintNodes, AssetRegistry and AnimGraph. It has no
dependency on any consuming gameplay plugin or GA superclass. EnhancedInput is
declared as an Editor-only plugin dependency.

Build/reload the host Editor target, start Unreal's official MCP server and
discover the toolset. See the [StateTree guide](../StateTree/native-mcp.md) for
server discovery. Tool definitions must be inspected before invocation.

## Operations

- CreateAnimationBlueprintTemplate: create an unsaved, Skeleton-independent
  `UAnimBlueprint` with `UAnimInstance` as its parent, using the native
  `UAnimBlueprintFactory`. Refuses existing packages both in memory and on disk.
- DescribeBlueprint: read all graphs including interface implementations; returns
  node GUIDs/classes/titles, pin names/directions/defaults/links, layout and comments,
  local variables, parent class and compile status.
- EditBlueprint: apply one explicit FWoolooBlueprintEdit within AllowedPackageRoot.
- CompileBlueprint: validate Enhanced Input event pins, then explicitly compile
  the Blueprint and return compiler messages. Validation does not repair nodes.
- SaveBlueprint: save only the requested package, with no source-control action.

Writes reject PIE, engine content and paths outside a narrow caller-supplied
content directory. Edit, describe, compile and save target existing Blueprint
assets. This is not a general UObject property editor and does not implement C++
compilation.

## Creating an animation Blueprint template

`CreateAnimationBlueprintTemplate(AssetPath, AllowedPackageRoot)` accepts a new
long package path and a narrow writable directory containing it. For example:

```json
{
  "assetPath": "/Game/Examples/Animation/ABP_Common",
  "allowedPackageRoot": "/Game/Examples/Animation"
}
```

The operation registers the animation compiler module and calls the native
animation Blueprint factory with `bTemplate=true` and no Skeleton. The factory
performs its normal initial Blueprint setup; this is not a C++ compilation.
There is no explicit follow-up compile, package save, source-control operation,
Skeleton creation, animation assignment or consumer-specific graph authoring.
The resulting instance belongs to its new package and participates in the normal
editor UObject lifecycle. Existing packages are never reused or adopted.

The existing `FWoolooBlueprintResult` reports success and a message. On success,
`Json` includes `asset_path`, `created`, `dirty`, `is_template`, `parent_class`
and `target_skeleton` (`"None"` when unassigned). On failure, inspect `Message`;
if the native factory fails after allocating a package, that package may remain
in memory and must not be blindly retried or overwritten.

Populate the template through existing Blueprint/animation tools, then explicitly
call `CompileBlueprint` and `SaveBlueprint` for that package. The tool does not
create Animation Layer Interfaces or bind role-specific layers.

## Edit fields

| Operation | Fields |
| --- | --- |
| AddNode | GraphName, ClassPath, optional NodeId, Properties, FunctionPath, X/Y |
| RemoveNode / MoveNode | GraphName, NodeId; X/Y for MoveNode |
| ConnectPins / DisconnectPins | GraphName, NodeId/PinName, TargetNodeId/TargetPinName |
| SetPinDefault | GraphName, NodeId, PinName, Value; unconnected input pins only |
| SetNodeProperties | GraphName, NodeId, Properties; targeted reflected fields |
| SetComment | GraphName, optional NodeId, Value (title), X/Y, Width/Height, CommentColor |
| SetClassDefaults | Properties; a previously compiled generated class is required |
| RemoveInterface | ClassPath; removes its implementations using native editor utilities |
| RemoveFunction | FunctionName; ordinary function graphs only |
| RemoveVariable | VariableName; local member variables only, using native editor removal |

Use GUIDs from DescribeBlueprint, not generated object names. ConnectPins asks the
native graph schema; replacing links or inserting conversions requires explicit
bAllowBreakingLinks. Fields use UE ImportText values and are staged before writes.
Imports reject transient, read-only and instanced-reference fields. Ordinary
properties must be editor-editable; the three native async factory fields and
Enhanced Input event InputAction field listed below are exempt. Nested object
instances are not created through import.
Pin defaults are parsed and validated with the native K2 schema before setting;
object/class references must resolve successfully.

AddNode loads a UEdGraphNode class supplied by the caller. The class's owning
editor module must already be enabled/loaded. For a UK2Node_CallFunction, supply
its reflected UFunction path in FunctionPath. For native latent task nodes, supply
the factory's ProxyFactoryClass, ProxyFactoryFunctionName and ProxyClass in
Properties. These fields are set before AllocateDefaultPins; the service does not
hardcode factories or task types. To change an async factory, add a configured
replacement node, reconnect its described pins, then remove the old node. Existing
async factory fields cannot be changed in place because their pins depend on the
complete factory configuration. Describe the graph again after property edits
before further connection edits.

For an Enhanced Input event, use ClassPath
`/Script/InputBlueprintNodes.K2Node_EnhancedInputAction` and supply a valid
InputAction asset in Properties using UE ImportText syntax. The action must be
assigned before AllocateDefaultPins: native expansion requires an InputAction
output pin that a blank node does not create. Never create a blank input event
and later assign its action through a generic UObject property editor. AddNode
rejects a missing action and duplicate events for the same action. Existing
InputAction fields cannot be changed through SetNodeProperties; explicitly remove
the old event, add its configured replacement and reconnect the described pins.
CompileBlueprint rejects missing/orphaned data outputs or an InputAction output
that does not match the asset, before entering the native compiler. This check
applies to the Wooloo compile operation; other compile entry points do not use it.

Comments and coordinates are explicit rather than auto-claimed layout quality.
Arrange left-to-right flow, clear responsibility groups, short links, aligned
nodes and bounded comments. A graph read validates structure but does not replace
visual inspection in the editor. Compilation and saving are separate calls.

## Editor verification after building

The Development/Debug automation test
`Wooloo.Blueprint.AnimationBlueprintTemplate.CreationAndGuards` creates a uniquely
named in-memory fixture and checks the template flag, generic parent, absent
Skeleton, metadata, lack of a disk save, scope rejection and duplicate refusal.
It unregisters its fixture without deleting any files. It has not been run in
the authoring session. After building, run this test in the editor. Separately,
in an approved disposable content scope, populate a template with an input pose,
compile and save it, reopen it and verify it can be used by a Linked Anim Graph.
Confirm that creating at the saved package path is refused. Creation alone does
not verify the later authored graph or its cross-Skeleton use.

Use an approved scratch Blueprint: add a native call/task node, inspect its pins,
connect and disconnect, change defaults, move nodes, create Chinese comment boxes,
compile, save and reopen. Verify native interface graphs are included and removed
without creating duplicate functions. Check invalid imports, node GUIDs, pin types,
scope boundaries and editor Undo. Visually inspect the actual graph. These checks
have not been run in the authoring session.

For Enhanced Input, create an event with an explicit action, inspect all four data
outputs (ActionValue, ElapsedSeconds, TriggeredSeconds and InputAction), connect
Started/Completed/Canceled, then compile, save and reopen before moving to the next
asset. Check that missing actions, duplicate events and in-place action changes
are rejected. For a known incomplete event, CompileBlueprint must report the
node GUID and missing pin without compiling, modifying or saving the asset.
