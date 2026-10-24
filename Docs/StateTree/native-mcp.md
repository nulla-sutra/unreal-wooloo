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

- DescribeStateTree: read flat state/parent IDs, native node/wrapper types,
  node names, instance types/object paths, instance property ImportText values,
  schema context names/types/GUIDs, root parameter GUID and property bindings.
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
| AddTask | StateId, TypePath (task UScriptStruct or concrete UStateTreeTaskBlueprintBase class), optional NodeId/Name, Properties |
| AddEnterCondition | StateId, TypePath (condition UScriptStruct), optional NodeId/Name, Properties |
| AddTransitionCondition | StateId, TransitionId, TypePath, optional NodeId/Name, Properties |
| SetInstanceProperties | NodeId, Properties |
| RemoveNode | NodeId |
| SetTransition | StateId, optional TransitionId, Trigger, TargetType, TargetStateId, Priority, EventTag |
| RemoveTransition | StateId, TransitionId |
| BindProperty | SourceId (invalid selects global parameters), SourcePath (empty binds the whole source), NodeId target, TargetPath |
| RemoveBinding | NodeId target, TargetPath |

SetTransition edits an existing transition by GUID or adds one. Enum strings use
native short names such as OnTick, OnStateSucceeded, GotoState, Succeeded and Normal.
TargetStateId is required for GotoState. EventTag must already be registered.

AddTask accepts native UObject task classes and compiled Blueprint-generated
task class paths ending in `_C`, as well as the existing native task structs.
The class must derive from UStateTreeTaskBlueprintBase, be concrete/current,
and pass the schema's IsClassAllowed check. Wooloo configures the engine's
FStateTreeBlueprintTaskWrapper with that class before calling the native
FStateTreeEditorNode::InitializeAs initializer. This allocates the transactional
instance object with its state as Outer. Do not supply a bare wrapper struct
instead of the task class. Condition creation still requires native structs.

Properties is a map of top-level editable instance fields to UE ImportText strings.
For nested configuration, supply its full struct literal as one field value. The
service stages and validates every field before committing to either struct or
UObject instance data. Only requested properties are copied; the node template,
UObject identity and unrelated values are preserved. Transient, read-only and
instanced-reference properties are rejected. UObject instances receive native
PreEditChange/PostEditChangeChainProperty notifications and participate in Undo.
Native node instance-change callbacks receive a relative property event; the
service does not fabricate a Tasks/Conditions array chain without indices.
Native parameter creation and node instance initialization are reused directly.
Bindings resolve source/target IDs and property paths, check source availability
in the execution path and validate native property compatibility before writing.
For a whole context object binding, use its GUID from DescribeStateTree.context,
an empty SourcePath, and the target node's object property name in TargetPath.
The source class must be assignable to that object property. Whole-struct sources
require an exactly matching target struct property. TargetPath must name a property.
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
no invalid StateTreeNodeBase cast is triggered.

Also add a concrete native UObject task and a compiled Blueprint task through
their class paths. Verify the wrapper class, instance type, state Outer and
transactional flag; set multiple instance fields and a nested struct field,
bind a schema context object using an empty SourcePath, compile, save and reopen.
Confirm the instance identity and unedited fields survive property edits and
that Undo restores the edited values. Include a mixed valid/invalid property
request to verify that failed validation changes no field. Reject abstract,
deprecated, unrelated and schema-disallowed classes without adding a node;
reject mismatched context-object types. Repeat the existing struct-task and
condition scenarios to check compatibility. These checks have not been run.
