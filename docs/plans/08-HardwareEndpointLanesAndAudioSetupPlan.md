# Hardware Endpoint Lanes and Audio Setup Plan

**Status:** proposed implementation plan only. This document changes no
application behavior. Implementation, builds, review-build installation and a
PR require explicit user authorization.

## Objective

Replace hardware Input/Output module insertion and channel configuration in
Audio Setup with fixed, persistent endpoint rows on the left and right of the
always-visible Patch Canvas. The rows must bind to actual named channels from
the selected audio-device session, preserve graph identity and project data,
and execute through real channel-aware audio buses. This is an architectural
change, not a rails-only visual treatment.

The plan is grounded in the current repository as checked out on
`signal-generator-level-policy-editor` at `de7152d`; PR #24 is open on PR #23,
which is also open. PR #22 is reported merged, but it targets the
`docs/routing-model-redesign` branch; the checked-out `origin/main` is
`237eab5`. Recheck those references before implementation, and do not assume
open stacked PR content has landed on `main`.

## Confirmed product decisions and boundaries

- **Remove hardware `Input` and `Output` from Module Browser.** They must not
  appear in categories, search results, double-click/Enter insertion, drag
  payloads or any other user-facing insertion shortcut. Retain their internal
  `audio.type` recognition for legacy project decoding and migration. Keep
  implemented generators and the explicit `Sum` utility discoverable. There
  is no implemented `Mixer` module to advertise.
- **Keep fixed left Inputs/Sources and right Outputs/Destinations lanes.** The
  patch remains visible and occupies the center. Lane rows are not movable
  canvas nodes. A new patch starts with one unassigned hardware-input row
  (which may be None/Disabled) and one unassigned output row. `+ Add input`
  and `+ Add output` create unassigned rows; neither action selects hardware,
  connects a cable, starts playback or changes transport. Existing internal
  generators remain available from the Module Browser and appear as
  non-movable source cards when inserted.
- **Configure channels in the lanes, not Audio Setup.** Each hardware row
  selects actual driver-reported channel names: a mono channel or a stereo
  pair in the normal view, with wider groups/channel selections under an
  Advanced row editor. All available channels appear in selectors; rows alone
  determine active selections. Exact channel identity must survive mask
  construction and callback-buffer compaction.
- **Use one explicitly negotiated backend/device session.** A separate input
  and output device pair is allowed only when the selected backend supports
  and successfully negotiates that pair as one session. Do not run unrelated,
  unsynchronized streams or promise cross-device clock synchronization.
  Multiple outputs select disjoint channel groups from that configured
  session. Reject overlapping output assignments with an actionable
  channel-owner message. Input channel reuse/fan-out is allowed.
- **No hidden summing or channel conversion.** A destination accepts one
  incoming bus. Combine sources through the visible `Sum` processor. Update
  `RoutingModelRedesign.md` to reconcile its earlier proposal for summing
  destination ports and diagrams: that proposal is superseded for this
  implementation by single-feed destinations plus explicit Sum. A mono
  hardware source may reach stereo only through an explicit, persisted,
  visible dual-mono adapter (or another specifically implemented adapter).
  Never invent processor layout support or silently truncate, reorder,
  duplicate, normalize or sum channels.
- **Endpoint identity is stable; hardware identity is not portable.** Project
  rows and cables use stable document IDs and retain Missing/Disabled rows and
  wires. A project stores endpoint role, user label, enablement, bus layout
  and requested driver-channel labels. Machine-local settings store backend
  and selected device name/binding plus the resolution of channel labels to
  current driver indices. Do not persist inventory-generation device tokens,
  handles or machine paths in project files. Resolve labels only when
  unambiguous; missing/ambiguous labels remain visible and require user
  action, never first-N or nearest-name substitution.
- **Keep existing real-time and transport safety.** Start remains explicit;
  there is no fallback device/API and no auto-playback. Unassigned/disabled
  input cannot block output-only internal-generator workflows. Stream or
  channel-binding edits while running are staged and marked **Restart
  required**; apply them only through an explicit control-thread Stop/Start
  (or explicit Restart) transition. Failures leave transport stopped and show
  the exact failure.
- Defer Application Capture, File Player, Virtual Audio Device, Recorder/File
  Writer, Streaming, plugin hosting, generalized resampling, unsynchronized
  multiple devices, and any unimplemented hardware kind. Current supported
  source scope remains hardware input plus shipped internal generators;
  destination scope remains configured main hardware output. Do not suggest
  physical voltage/SPL calibration.

## Current evidence and architectural gap

| Surface | Current behavior | Planning consequence |
|---|---|---|
| `src/VisualAudioLab.Presentation/include/val/presentation/ModuleCatalogItems.h` | `moduleCatalog` has 24 entries; `Input`, `Output` and `Sum` are in Routing utilities. `ModulePanels.cpp` derives filtering, keyboard/double-click insertion and drag payloads from this catalog. | Remove only `Input` and `Output` catalog entries, preserve `Sum` and generators, and add an insertion allowlist at the callback boundary so a stale/forged `Input` or `Output` payload cannot create a node. |
| `MainComponent::insertModule` | Converts catalog display names to `audio.type` nodes and creates graph nodes. | Keep generator/processor insertion; route hardware-row creation through endpoint commands, not this generic node function. |
| `domain::Node`, `Connection`, `Routing.h`, `WorkspaceService` | Nodes have one structural input/output; connections identify only node and direction. `Sum` alone permits fan-in. Stable `GraphId`s and undoable graph commands already exist. | Introduce typed bus/port identity and endpoint rows in the domain/application contract; do not model several independent endpoint buses as cosmetic duplicate ordinary nodes. Extend commands and undo/redo for endpoint row lifecycle and cable edits. |
| `AudioGraph::fromStructural` and `PreparedAudioGraph::prepare` | One global graph width (1–32); input bus is zero-width or equal to output width; one `Output`; no cycles; explicit Sum; multiple roots/fan-out and inert unconnected nodes already exist. DSP processors are currently prepared at one shared width. | Retain proven DAG, explicit Sum, bounded callback and inert-node invariants while replacing the single global bus with per-port layouts, multiple actual sink buses and explicit adaptation. Declare and test supported layouts per processor. |
| `InputNode`, `OutputNode`, `AudioEngine` | Input copies corresponding host channels; Output copies one upstream bus. Plans and scratch are prepared off-thread and published/retired with the existing ownership protocol. | Preserve no-lock/no-allocation callback rules. Allocate endpoint and adapter scratch before publication and retire plans only after callback quiescence. |
| `RuntimePanel.cpp` | Audio Setup currently contains backend/device selection, sample rate, buffer, input/output channel-group selectors and Advanced wider contiguous groups. Port choices are generated from contiguous first-to-last ranges. It already has discovery/loading/error states, explicit Start and driver-control-panel support. | Keep backend/device/rate/buffer/control-panel duties in the dialog; move every hardware-channel assignment to endpoint rows. Replace contiguous-pair enumeration with row-specific named channel selection. |
| `IAudioBackend`, `AudioStreamRequest`, `JuceAudioBackend::open` | Request currently carries one input/output token and explicit index vectors. The backend builds exact BigInteger masks, rejects disappeared/out-of-range channels and rejects driver-changed masks. Returned planar buffers expose channel counts, not driver-index labels. | Build each stream request from unions of active row assignments, preserving sparse masks. Add an explicit verified mapping between selected physical indices and compact planar callback slots; never infer that buffer slot `n` means physical channel `n`. |
| `AudioSetupPreference` / `Settings` | Machine-local settings persist backend/device names, rate, buffer, channel counts and selected index lists; live tokens are inventory-scoped. | Evolve settings to persist one session's device resolution and channel mapping without turning tokens into persistent IDs. Retain compatibility with existing names/index fields. |
| `LocalProjectStore` / `ProjectFormat.md` | Schema 1.0 serializes stable node/connection IDs and unknown fields. Readers reject unknown required features; current writer uses `audio.explicit-sum.v1`. | Add a versioned required feature for endpoint rows, port-indexed cables and layouts. Migrate legacy Input/Output nodes losslessly and retain original IDs/cable references where possible. Do not silently downgrade files for older readers. |
| `CanvasView.cpp` / `NodeView.cpp` | PR #23-style rails pin ordinary graph source nodes and Output nodes, preventing their movement/deletion, but endpoint instances remain manually inserted. The empty-canvas hint still says to add Main Output from Module Browser. | Replace node-discovery rails with endpoint-row views; initialize default rows and replace obsolete hints. Preserve fixed canvas zoom/pan, processor editing and keyboard navigation. |
| `RoutingModelRedesign.md`, `plans/03-OutputOnlyProcessingPlan.md`, `plans/07-TypographyAndRoutingImplementation.md` | The redesign is explicitly aspirational for typed buses and system endpoint lifetime. Earlier plan layers implemented shared-width Sum and presentation rails only. | Update status and dependencies accurately. The earlier destination-summing proposal must be reconciled with the confirmed single-feed destination rule. |

## Target interaction and wireframes

```text
┌ Inputs / Sources ─────────┬──────────── Patch Canvas ────────────┬ Outputs / Destinations ─────┐
│ Hardware input                                      EQ            │ Main output                  │
│ [Enabled] Input 1 — rename                            ┌──────┐      │ [Enabled] Output 1 — rename  │
│ Channel: Mic/Line 1 (Mono) ── meter ──●──────────────▶│ Gain │──●──▶ Channel: Out 1 + Out 2    │
│ [None / Disabled]                                    └──────┘      │ Stereo · meter              │
│ + Add input                                                        │ + Add output                 │
│                                                                    │                              │
│ Built-in sources (added from Module Browser)                       │                              │
│ Signal Generator · 440 Hz · -6 dBFS peak                          │                              │
└──────────────────────────┴─────────────────────────────────────────┴──────────────────────────────┘
```

Rows show a stable user label, endpoint kind, enable/disable, exact selected
channel names/layout, a compact live meter, assignment readiness and a
contextual remove/rename action. Channel selection remains in-row; an
accessible expanded row editor exposes per-channel checkboxes/order for
Advanced widths. Distinguish machine device choice (Audio Setup) from channel
choice (lane).

| State/action | Required behavior |
|---|---|
| First project / new patch | Show one unassigned Hardware Input row with `None / Disabled` and one unassigned Main Output row. Neither row has an implied cable or physical binding. |
| Add input/output | Add an unassigned stable-ID row. Keep existing row identities and cables; do not pick the next channel automatically. |
| Assign channels | List every currently reported named channel. Normal choices are mono or a named stereo pair; Advanced offers wider groups and arbitrary explicit channel sets within the supported limit. Show selected names persistently. |
| Rename / enable / disable / remove | Rename never changes assignment. Disabling preserves assignment and cables while showing Disabled. Remove only the selected row and its incident cables after a clear confirmation/undoable action; keep the Main Output role available and preserve at least one output row. All operations participate in undo/redo. |
| Ready / active | Show a text readiness label in addition to the semantic status accent. Meters show live signal only when fresh captured samples are available; show Stopped, Waiting, Stale or error honestly. |
| Missing device/channel | Retain row, requested label and cables, show Missing with the reason and a reassign action. Do not substitute another device or channel. If the configured session cannot open, Start fails with actionable status. |
| Disabled / unassigned | Preserve cables and selection, but treat the row as inactive/unresolved. A disabled or unassigned input never blocks a valid output-only generator path. An enabled active output must be assigned before hardware Start. |
| Channel overlap | Permit input reuse. Reject overlapping physical output-channel assignments before Start and identify both rows and the conflicting named channels. |
| Width mismatch | Keep cable/assignment visible and flag it invalid. Offer an explicit compatible adapter operation; do not auto-rewire when a binding changes. |
| Running edit | Keep the current active stream mapping immutable. Mark edits pending/restart-required; do not mutate callback state, implicitly stop, or auto-start. Apply only after explicit user Stop/Restart and report success or failure. |
| Offline preview | Keep graph validation separate from hardware readiness. Offline output may use an explicitly chosen virtual bus width; it does not claim a hardware endpoint is ready. |

Lane headers stay fixed while lane rows scroll independently. Keep the
canvas centered and always visible. Constrain lane widths, preserve readable
typography from the established Inter-based roles, and prioritize the patch
area at constrained window sizes; test the minimum supported 800×600 window
and default/high-DPI Windows rendering before approving any responsive
fallback. Follow the approved neutral charcoal, signal-cyan, crisp-boundary
and restrained-glow rules in `plans/06-PremiumPaletteAndGlowGuidelines.md`;
readiness must never be conveyed by color alone.

## Proposed data and execution contract

1. **Endpoint rows:** Add typed hardware-input and hardware-output endpoint
   records with a stable document-local ID, role/kind, user label, enabled
   state, logical bus layout and requested named channel set. Keep row identity
   independent from UI position and device-inventory generation. Internal
   generator nodes remain real source processors and are represented in the
   left lane without becoming hardware input rows.
2. **Machine resolution:** Add a machine-local endpoint resolver keyed by the
   chosen backend/device binding. Inventory tokens remain ephemeral. Resolve
   requested channel names only to one exact driver entry (including a
   stable disambiguator if names repeat); absent or ambiguous matches remain
   unresolved. Never map a saved request to “the first available channels.”
3. **Ports and cables:** Introduce typed port IDs and per-port layouts
   (mono, stereo, multichannel/custom with an explicit ordered channel map).
   Cable records identify both endpoint/processor ID and port ID. Build
   topological plans from all roots to all enabled sinks, preserving
   fan-out, legal inert nodes, explicit Sum-only fan-in and cycle rejection.
   Enforce bounded node/port/cable/channel counts before allocation.
4. **Processor contracts:** Audit every shipped processor and generator.
   Initially declare only layouts each implementation actually processes.
   Same-width mono and stereo layouts are supported only after tests verify
   node behavior and scripts' declared dimensions. A processor with no
   implementation for a requested layout is invalid, not silently adapted.
5. **Explicit adaptation:** Materialize a deterministic adapter as a visible,
   editable, persisted cable-stage/processor. The first required adapter is
   mono-to-stereo dual-mono: one mono sample is copied to L and R, with no gain
   change. Create it only after the user explicitly accepts the adapter on a
   mismatch action. No pan, fold-down, N-to-M matrix or resampling claim until
   separately implemented and tested.
6. **Summing policy:** Destination inputs accept exactly one cable. The
   existing `Sum` processor remains the visible source-combination point;
   preserve its unity, corresponding-channel addition and no-limiter
   behavior. Multi-output fan-out is allowed from one source/processor bus;
   two sources that need to meet require an explicit Sum before the
   destination.
7. **Channel mask and callback mapping:** For each direction, form the exact
   union of channel indices assigned to **enabled and assigned endpoint
   rows**, whether or not a row is currently wired. Connectivity controls
   graph execution, not hardware identity. Disabled/unassigned input rows add
   no mask bits, so zero-input/output-only operation is valid. Disjoint output
   rows union into the output mask. Reject duplicate physical output bits.
   Construct a control-thread mapping from each row's ordered physical
   channel indices to the selected-channel compact planar callback slots;
   test sparse sets such as channels 1, 3 and 8 against fake-driver buffers.
8. **Readiness versus graph validity:** Validate the graph topology/layout
   independent of device inventory. Separately report each endpoint's
   Disabled, Unassigned, Ready, Missing, Ambiguous or Error state. Hardware
   Start requires an openable configured session and at least one assigned
   enabled output; it does not require an input or a connected path at every
   sink. No path is reported as silence, not as a fabricated signal.
9. **Real-time ownership:** Resolve inventory, validate layouts, allocate
   adapters/scratch, and prepare the immutable graph/stream plan on the
   control thread before publication. The callback uses only bounded prepared
   arrays, exact precomputed maps and lock-free handoff; no allocation,
   mutex, logging, UI call, device discovery or ownership destruction in the
   audio callback. Stream transitions stop/quiesce before releasing device or
   graph resources and publish/retire plans under existing engine lifetime
   guarantees.

## Simplified Audio Setup responsibilities

Keep the single **Audio > Audio Setup** entry point and the current isolated
discovery helper, timeout/cancellation, driver-control-panel and explicit
Start/Stop safeguards.

| Remains in Audio Setup | Moves to endpoint rows |
|---|---|
| Driver model/backend | Hardware input/output channel names and grouping |
| Device/session choice; separate input/output device selectors only if the selected backend requires them | Per-row assignment, label, enablement and output-overlap feedback |
| Sample rate (never label it merely “Frequency”) | Mono/stereo/wider bus layout and explicit cable adapters |
| Buffer size / callback frames with backend-specific truthful help | Endpoint meters and route/readiness state |
| Supported backend control panel, discovery/loading/error states, and a secondary estimated latency readout only when backend diagnostics provide it | Row add/remove/undo and route connection interaction |

Remove channel count, first-N, mono/stereo and group assignment controls from
this dialog. Its device choice populates the endpoint-row channel inventory;
it never silently assigns rows. Preserve no-fallback behavior and actionable
loading, empty, failed, timeout and disconnected states. Do not claim measured
physical round-trip latency when only an estimate or backend-reported frame
count is available.

## Dependency-ordered implementation stages

### Stage 1 — Freeze contracts and migration fixtures

- Audit exact supported channel layouts per shipped processor/generator,
  current backend pair negotiation, channel-name ambiguity, machine settings,
  project examples and UI call sites.
- Define stable row/port/cable identifiers, lifecycle states, output overlap
  rules, machine resolver semantics, row removal/undo, disabled-input behavior
  and the explicit stop/restart policy in typed domain contracts and tests.
- Specify a new schema version plus required-feature identifier. Define a
  migration fixture for every legacy project shape before changing the reader.
- Update the architecture proposal to state single-feed destinations and
  explicit Sum. Do not begin visual-only lanes as a substitute for these
  contracts.

### Stage 2 — Versioned project model and lossless legacy migration

- Extend domain/project entities with endpoint rows, typed ports, channel
  layouts and cables; keep per-node processor positions unchanged.
- Migrate legacy `Input` nodes to Hardware Input rows and legacy `Output`
  nodes to Main Output rows. Preserve their GraphIds and connection IDs where
  representable; otherwise persist an explicit old-ID-to-new-ID cable map.
  Preserve every processor's ID, position, parameters, user data and unknown
  compatible JSON fields.
- Retain requested endpoint channel labels in the project; keep device name,
  backend and local exact index resolution in machine-local settings. Missing
  or unmatched references stay visible after load.
- Require the new feature for semantics old readers cannot edit. Keep legacy
  read support; reject unsafe downgrade rather than omit endpoints/cables.
- Make add, assignment, rename, enable/disable, remove and cable changes
  undoable; reserve IDs monotonically and never recycle removed IDs.

### Stage 3 — Per-port DAG, adapters and multiple sink execution

- Replace `AudioGraph.channels` as the sole layout authority with validated
  per-port contracts and a bounded multi-root/multi-destination plan.
- Implement exact physical input/output bus bindings and output reachability
  without implicit fan-in; preserve the explicit Sum processor.
- Implement the tested mono-to-stereo adapter and visible cable representation.
  Reject unsupported layout/processor combinations with endpoint/port-specific
  messages.
- Prepare all per-bus scratch, adapter state, channel maps and capture demands
  off-thread; keep current immutable publish/retire and callback constraints.
- Keep disconnected processors legal/inert and separate graph-valid state from
  endpoint readiness.

### Stage 4 — Device inventory, settings and simplified Audio Setup

- Populate all available named channels from the selected, successfully
  inspected device/session. Do not synthesize channels from count or contiguous
  first-N assumptions.
- Change stream request construction to use the union of enabled+assigned
  endpoint rows and exact sparse channel masks. Build/test deterministic
  physical-index-to-compact-buffer-index maps in each direction.
- Keep optional input (zero channels) valid. Support input/output pair
  inspection only when the selected backend explicitly supports it; fail
  clearly if the pair cannot open. Do not open multiple unrelated streams.
- Move channel choice out of Audio Setup. Retain driver/device, sample rate,
  buffer, control panel, explicit actions and pending/error/retry feedback.
  Expose estimated latency only if the backend provides defensible data.
- Preserve existing local setting names/indexes through a validated migration;
  re-discover/re-probe before use and surface unavailable or ambiguous values.

### Stage 5 — Endpoint lanes, browser removal and interactions

- Render always-visible fixed left/right lanes around the centered patch area.
  Hardware rows are endpoint descriptors, not draggable graph nodes; existing
  generator nodes appear as non-movable source cards. Long lists scroll within
  their own lanes; headers remain fixed.
- Add one default unassigned input row and one output row for new projects.
  Provide explicit Add, assignment, rename, enable/disable, meter, remove and
  undo/redo actions without automatic wiring or transport changes.
- Remove Input and Output from the canonical module catalog; verify absence
  from category counts, search, keyboard/Enter, double-click, Insert, drag
  payloads and canvas node shortcuts. Preserve internal type parsing and
  legacy migration. Keep generators and Sum available.
- Replace obsolete empty-canvas instructions about inserting Input/Output.
  Keep fixed lanes, cables and processor editing keyboard-accessible with
  explicit focus/selection labels and visible readiness text.
- Apply approved palette and typography; exercise minimum/constrained window
  sizes before fixing lane widths or any responsive fallback.

### Stage 6 — Integrate, document and qualify

- Update `RoutingModelRedesign.md`, `ProjectFormat.md`, `AudioEngine.md`,
  `AudioBackends.md`, `DesktopWorkflow.md`, `DspModules.md` and relevant
  examples/tests to distinguish shipped endpoint kinds from deferred kinds.
- Run all focused domain, graph, backend, persistence, runtime and presentation
  tests, then the relevant full Windows Release suite. Use injected/fake
  drivers and deterministic buffers; do not probe physical audio hardware
  unattended.
- Inspect default and constrained/HiDPI screenshots, keyboard/accessibility
  names, missing/error states and fixed lane behavior. Record unavailable OS
  or device qualifications explicitly.
- Only after authorization, create a focused implementation PR on the
  then-current authorized base and perform the standing persistent Windows
  review-build workflow: preserve the prior complete bundle, install the exact
  PR head and verify README identity and every manifest checksum. Do not
  auto-merge.

## Validation and acceptance matrix

| Area | Acceptance evidence |
|---|---|
| Module Browser | Input and Output are absent from category/search/count, Insert/Enter, double-click and drag; a direct forbidden insertion request is rejected. Generators and Sum remain available and working. Internal parser still loads/migrates old Input/Output types. |
| Default project/rows | New patch has one unassigned/disabled input row and one unassigned output row; Add creates unique unassigned IDs only. No implicit channel selection, cable, Start or transport change. |
| Named channel identity | Dropdown lists every driver-reported channel. Multi-row masks use exact selected identities including sparse sets; fake backend verifies every callback channel reads/writes the corresponding named hardware channel after compact buffer packing. No first-N fallback. |
| Multiple endpoints | Two or more independent input rows and output rows map exact disjoint physical channels to distinct endpoint buses. Output overlap is rejected with row/channel names; input fan-out/reuse is allowed. No unrelated simultaneous device clocks. |
| Layouts/adapters | Same-layout direct routes work. Mono input to stereo output uses a visible persisted dual-mono adapter. Invalid dimensions and unsupported processor layouts fail with exact endpoint/port guidance. No hidden sum, fold-down, truncation or reorder. |
| Graph invariants | Direct source-to-destination, multiple roots/sinks, processor fan-out, explicit Sum, disconnected/inert nodes, duplicate cable and cycle rejection all have graph/runtime tests. Destination fan-in is rejected unless a Sum precedes it. |
| Output-only | Disabled/None input results in a zero-input stream mask; an internal generator reaches hardware output after explicit Start. Unassigned input never blocks this path. No playback begins on row/device/project edits. |
| Device lifecycle | Device removal, channel disappearance, duplicate names, unsupported split-device pair, probe failure, mask rejection and stream-open failure retain rows/wires, report Missing/Error and never substitute. |
| Running changes | Endpoint assignment edits while running are staged; current callback mapping remains unchanged. Explicit Stop/Restart applies exact pending settings; failed transition remains stopped and reports the failure. |
| Persistence/migration | Round-trip new projects, multiple endpoint rows, stable IDs, sparse channel labels, adapters, cables and unknown fields. Migrate representative schema-1 projects and examples with legacy Input/Output while retaining graph IDs, cable mapping, processor positions and generator routes. Unsupported older-reader features are refused, not dropped. |
| Undo/redo | Add, rename, enable/disable, assignment, remove and cable edits undo/redo atomically, retaining stable IDs and compatible unknown data. |
| UI/accessibility | Fixed lanes remain left/right, canvas remains visible and usable at default/minimum size and high-DPI scale; independent lane scroll does not move headers or canvas. Keyboard order/actions, focus, accessible names, route states and errors are discoverable without color alone. |
| Real-time | Static checks plus callback allocation/lock guards cover multi-bus processing and meters. No discovery, allocation, locks, UI, logging or resource retirement occurs in callback. |

## Risks and completion gates

- This depends on typed per-port layouts and actual multi-destination execution;
  implementing only descriptors, row widgets or cables is not a valid partial
  delivery.
- Current processors share one graph width. Every processor needs a verified
  port-layout declaration before it can join mixed-width routes. VAL Script
  input/output declarations remain dimensionally exact unless an explicit
  adapter is inserted.
- Driver names may be duplicated, unstable or absent. The local resolver must
  expose ambiguity instead of guessing. JUCE may compact sparse masks in a
  backend-specific way; verify the mapping via injected backend tests and
  native API contracts before enabling a device.
- A whole-device disconnect may prevent the one configured session from
  opening even if another row is still Ready. The UI must explain that session
  limitation and must not silently route around it.
- Versioned migration and required-feature gating are prerequisites to moving
  hardware assignment out of ordinary graph nodes. Existing example projects,
  unknown-field preservation and generator-only workflows are regression
  gates.
- Keep the explicitly approved instrument palette and readable typography;
  do not let narrower lanes regress the user's readability request. Do not
  claim macOS, physical-device, multichannel hardware or measured-latency
  qualification without corresponding evidence.

## Future delivery steps (not authorized by this plan)

After explicit implementation authorization: complete all stages and
acceptance gates; create a focused PR on the then-current agreed base; preserve
and update the persistent Windows review package only after a complete
compatible Release build; verify source identity, README and every file
checksum; report exact PR, commit, test/screenshot evidence and unqualified
platform/hardware coverage. No code, tests, build, PR, Latest package or
hardware probe is part of this planning assignment.
