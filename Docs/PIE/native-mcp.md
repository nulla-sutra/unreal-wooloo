# Native PIE network diagnostics

`WoolooPIE` is an Editor-only diagnostic module registered as
`WoolooPIE.WoolooPIEToolset` with Unreal's official Toolset Registry.
It is included only in Debug, DebugGame and Development configurations.
The module uses native world contexts, Enhanced Input and GAS inspection APIs;
it has no dependency on a consuming project or its gameplay plugins.

## Integration and connection

1. Enable Wooloo in the host project. The plugin declares the Editor-only
   `EnhancedInput`, `GameplayAbilities`, `ToolsetRegistry` and
   `ModelContextProtocol` dependencies. The module declares its C++ dependencies.
2. Have the user build the host project's Editor target and fully restart the
   editor. Tool discovery cannot load newly added reflected declarations into
   an old binary; this workflow does not invoke C++ compilation or Live Coding.
3. Connect the MCP client directly to Unreal's official MCP server, using the
   endpoint reported by that editor. If needed, start it with
   `ModelContextProtocol.StartServer`, then run `ModelContextProtocol.RefreshTools`.
4. Use the official server's `list_toolsets` and `describe_toolset` to discover
   `WoolooPIE.WoolooPIEToolset`. Inspect the actual returned parameter schemas
   before using `call_tool`. The JSON examples below illustrate the reviewed
   reflected parameter names; discovery is authoritative for transport naming.

A Monolith-only connection does not automatically expose official toolsets.
If this toolset is absent or the client has no direct official MCP connection,
stop before input submission and report that prerequisite. Do not bridge calls
through Python `ToolsetRegistry.execute_tool`, `ProcessEvent`, timers or callbacks.
Monolith's current input action selects the first PIE world; its `player_index`
does not select a different client world.

## Why the input boundary matters

The editor Python bridge invokes Unreal functions under
`FEditorScriptExecutionGuard`. While its global flag is active,
`AActor::GetFunctionCallspace` returns Local, including for Server RPCs.
A local-predicted GAS activation can consequently re-enter its activation RPC on
the client and keep extending the PredictionKey chain.

`InjectInputAction` refuses calls while this flag is active. It also refuses an
action bound on an Enhanced Input component with `ShouldFireDelegatesInEditor`
enabled, since that component would introduce another guard when processing input.
It never changes either setting. Input is submitted using native
`InjectInputForAction`; normal input processing executes the gameplay path.

## DescribeSession

```cpp
static FWoolooPIEResult DescribeSession();
```

Invoke `call_tool` with toolset `WoolooPIE.WoolooPIEToolset`, tool
`DescribeSession`, and arguments `{}`. The envelope contains `bSuccess`, `Message`
and a JSON string `Json`. A stopped session is a successful snapshot with
`Worlds: []`. No worlds or objects are retained between requests.

Each `Worlds` entry includes `WorldContextHandle`, `PIEInstance`, `WorldPath`,
`NetMode`, `TimeSeconds`, `Paused`, `TearingDown` and `Players`. Only in-process
PIE worlds are enumerated. External standalone or packaged processes need their
own connection and are outside this tool's scope.

Each player includes a `Target`, Controller/Pawn roles, `IsLocalController`,
`PlayerId`, input-stack size and Enhanced Player Input availability. Where
available, it also includes position, the GAS ActorInfo mesh's current montage,
ASC identity/ActorInfo paths, the scoped prediction key and granted ability
handles/classes with their active state. The fallback mesh is the Pawn's first
skeletal mesh component and its path is reported. Remote server-side controllers
remain visible, with `LocalPlayerIndex: -1`; they are not injectable local players.

The `Target` field contains:

```json
{
  "WorldContextHandle": "<actual context handle from this snapshot>",
  "LocalPlayerIndex": 0,
  "ExpectedControllerPath": "<exact controller object path>",
  "ExpectedPawnPath": "<exact pawn object path>"
}
```

`LocalPlayerIndex` indexes that world's GameInstance local-player array. A new
world context receives a new engine handle. Re-discover after stopping/restarting
PIE, changing maps, possession changes, or respawning; never replace an expired
target with whichever world or Pawn happens to be first.

Snapshots are synchronous and read-only. They do not tick worlds, pump frames,
repair assets, activate abilities, start PIE, install observers, or write reports.
An instantaneous ability may start and finish between snapshots. An active ability
or montage is not proof of server acceptance or a particular RPC's delivery;
scoped prediction keys may already have been restored when inspected.

## InjectInputAction

```cpp
static FWoolooPIEResult InjectInputAction(
    const FWoolooPIEPlayerTarget& Target,
    const FString& InputActionPath,
    const FVector& Value);
```

Example arguments, with `Target` copied from the chosen client snapshot and the
action path supplied by the consuming project:

```json
{
  "Target": {
    "WorldContextHandle": "<actual context handle>",
    "LocalPlayerIndex": 0,
    "ExpectedControllerPath": "<exact controller object path>",
    "ExpectedPawnPath": "<exact pawn object path>"
  },
  "InputActionPath": "/Game/Example/Input/IA_Action",
  "Value": {"X": 1, "Y": 0, "Z": 0}
}
```

The full package path or full asset object path must identify an already-bound
action in the selected player's most recently processed input stack. There is no
asset-name search, asset load or binding repair. Wait for normal input processing
after startup or a binding change; an absent/empty stack is a failure, not a reason
to call the skill directly. The reported snapshot/input result identifies the
resolved target. Rejected requests submit no input.

Boolean uses X=0/1; Axis1D uses X; Axis2D uses XY; Axis3D uses XYZ. Unused
components must be zero and all values finite. The tool rejects invalid/stale
contexts, changed Controller/Pawn identities, cross-world targets, remote players,
paused/tearing-down worlds, absent local Enhanced Input, invalid paths, unbound
actions and editor-script guards. Calls must run on the game thread.

Success reports `Submitted: true` and `Frames: 1`. It means one frame's raw input
has been submitted, not that the action triggered, an ability activated or a server
accepted it. InputAction modifiers/triggers, consumption, tags, costs and gameplay
checks still apply. The tool supplies no extra modifiers/triggers and does not
emulate hardware key mappings or viewport focus. No continuous-injection API,
ticker, deferred Python callback or persistent hold is registered. A zero-value
request can explicitly submit a release on a later normal frame. Sustained-input
scenarios require a separately reviewed interface; do not implement a Python loop.

## Explicit verification

Run these steps only when builds/runs are authorized by the consuming project's
policy. Importing the module does not run any of them.

1. After the user builds and restarts, run Automation tests matching `Wooloo.PIE.`
   using the native Automation tooling. `WorldIsolation` creates two owned,
   transient PIE-type worlds without launching an editor PIE session or ticking
   gameplay, checks exact selection/cross-world and changed-Pawn rejection,
   snapshot enumeration and context expiration, then destroys its own contexts.
   `InputValidation` checks value types, exact binding matching, script-enabled
   bindings and a removed stack entry. `ScriptGuard` verifies rejection without
   changing the caller's guard. No test saves assets or registers a repeating
   callback. These tests do not prove network transport or real gameplay success.
2. In a real in-process Listen Server + client session, snapshot all worlds, choose
   the intended local Client player, and submit the consumer's action once. Over
   a bounded observation window (for example 5 seconds), compare the selected
   client's state with the matching server player using `PlayerId` and exact
   ActorInfo identities. Confirm the other player's state was not triggered.
3. Repeat with two clients on a Dedicated Server, including the second client.
   Use native logs/Trace when snapshot sampling cannot establish server receipt
   or catch an instantaneous action. Record the actual observation duration,
   selected identities and observed activation/replication separately from
   inferences. Check for `Deep PredictionKey Chain Detected` and repeated RPCs.
4. Stop PIE, repeat a request with the old target and expect refusal. Reopen PIE
   and verify the old target is still refused. Repeat after a Pawn replacement.
   End only sessions started by the diagnostician. Input submissions create no
   background observers to cancel. Do not claim a test passed before running it.

Consumer-specific asset paths, scenarios and report locations belong in the
consumer's workflow documentation. Generated reports, when explicitly requested,
belong in its normal ignored automation output directory.
