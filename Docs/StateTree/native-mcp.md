# Native StateTree tools

The editor-only `WoolooStateTree` module registers
`WoolooStateTree.WoolooStateTreeToolset` with Unreal's Toolset Registry.
It has no consuming-plugin names, paths, ownership keys or recipes.

Build and reload the host Editor target first. Start the official server using
`ModelContextProtocol.StartServer`, connect the client to its reported endpoint,
and refresh with `ModelContextProtocol.RefreshTools` when needed. Use
`list_toolsets`, `describe_toolset`, then `call_tool` with the discovered schema.
Writing C++ does not by itself connect an MCP client or hot-load reflected types.

## Operations

- DescribeStateTree: read flat state/parent IDs, native task and condition types,
  instance property ImportText values, root parameter GUID and property bindings.
- CreateStateTree: explicit long package path and native schema class path. Uses
  the native factory, refuses existing packages, and creates an unsaved asset.
- EditStateTree: one FWoolooStateTreeEdit. Returns the changed entity's GUID.
- CompileStateTree: native editor subsystem compilation, with tokenized diagnostics.
- SaveStateTree: only the named package; no dependency save or source control.

All writes require AllowedPackageRoot to be a narrow directory, such as
`/Game/ToolsExample`. `/Game` itself, sibling directories and engine content are
rejected. Stop PIE before authoring. Caller approval and content ownership remain
the responsibility of the integration invoking the tools.

## Edit fields

| Operation | Fields |
| --- | --- |
| EnsureObjectParameter | Name, TypePath (UClass); existing incompatible parameters are rejected |
| AddState | Name; SourceId parent or invalid for a root; optional StateId |
| RemoveState | StateId; removes that subtree from the hierarchy |
| AddTask / AddEnterCondition | StateId, TypePath (UScriptStruct), optional NodeId/Name, Properties |
| AddTransitionCondition | StateId, TransitionId, TypePath, optional NodeId/Name, Properties |
| SetInstanceProperties | NodeId, Properties |
| RemoveNode | NodeId |
| SetTransition | StateId, optional TransitionId, Trigger, TargetType, TargetStateId, Priority, EventTag |
| RemoveTransition | StateId, TransitionId |
| BindProperty | SourceId (invalid selects global parameters), SourcePath, NodeId target, TargetPath |
| RemoveBinding | NodeId target, TargetPath |

SetTransition edits an existing transition by GUID or adds one. Enum strings use
native short names such as OnTick, OnStateSucceeded, GotoState, Succeeded and Normal.
TargetStateId is required for GotoState. EventTag must already be registered.

Properties is a map of top-level editable instance fields to UE ImportText strings.
For nested configuration, supply its full struct literal as one field value. The
service stages and validates every field before committing to the live instance.
It edits native struct instance data, not the node template or Blueprint UObject
instance data. Native node instance-change callbacks receive a relative property
event; the service does not fabricate a Tasks/Conditions array chain without indices.
Native parameter creation and node instance initialization are reused directly.
Bindings resolve source/target IDs and property paths, check source availability
in the execution path and validate native property compatibility before writing.
Successful edits notify an existing native editor view model to refresh its view.

Each call is an editor transaction. Failed validation leaves live property values
unchanged; use editor Undo for an applied edit. Compilation is explicit and can
perform the native compiler's own validation/normalization. Do not treat a read or
previous compiled state as proof that subsequent edits compile.

## Editor verification after building

In an approved disposable content scope: create a tree, add an Object parameter,
add a native task and condition, set instance values, bind the parameter, and
author success/failure transitions. Compile and save explicitly, reopen, and inspect
the parameter type and values. Verify malformed imports do not partially apply,
unknown GUIDs fail, sibling/engine paths fail, and Undo restores an applied edit.
Validate task/condition property writes with the debugger attached to check that
no invalid StateTreeNodeBase cast is triggered. These checks have not been run.
