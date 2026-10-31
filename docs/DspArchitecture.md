# Visual Audio Lab: DSP Architecture

## Status and governing context

This is a proposed DSP design, not an implementation or a statement of measured performance. It refines processing decisions left open in [FunctionalArchitecture.md](FunctionalArchitecture.md), within the product intent of [ProductVision.md](ProductVision.md) and [PRD.md](PRD.md).

**Implemented subset:** The separately requested [basic realtime engine](AudioEngine.md) now implements only Input/Gain/Output, prepared graph handoff, bounded live gain changes and telemetry. The wider DSP framework below remains proposed; effects, control-rate scheduling, automation and plugins are not introduced by that implementation.

Use the C++20/JUCE/CMake boundaries in [TechnicalArchitecture.md](TechnicalArchitecture.md). Keep backend I/O separate from DSP as defined in [AudioPlatformStrategy.md](AudioPlatformStrategy.md): JUCE adapts the primary standalone paths Windows/ASIO and macOS/CoreAudio, but its device or processor abstractions do not by themselves define the graph scheduler or node semantics. The subsequently requested platform abstraction also implements WASAPI Shared and Exclusive without changing the primary preferences or allowing silent fallback.

The design supports inspectable visual patching, local projects, Windows and macOS, and no subscription, cloud, or AI dependency. It preserves required future VST3 hosting and plugin export boundaries without delivering those features or assigning a release. Recording, MIDI, a DAW timeline and generated DSP code are not added; the separately requested VAL Script design lowers bounded node computations into prepared plans rather than changing the visual-first core.

## Proposed decisions and assumptions

| Area | Proposed baseline | Qualification or unresolved choice |
|---|---|---|
| Graph topology | A directed acyclic graph of typed connections; reject cycles explicitly. | Feedback requires a separately designed delay-aware scheduler and is not implicitly enabled by adding a delay node. |
| Scheduling | Prepare deterministic execution plans off the audio path; execute each active plan serially on the render context. | Parallel DSP is a later measured optimization, not a baseline thread pool. |
| Audio representation | Floating-point PCM with explicit channel counts; 32-bit samples are the starting recommendation. | Precision, buffer layout, SIMD approach, and double-precision nodes require validation before being fixed. |
| Control processing | A sample-clock-based control grid, with an explicit interval per graph. | The supported interval choices and user-facing control-rate setting are unresolved; never derive the grid from UI refresh or callback size. |
| Parameters | Typed, validated values; live value changes use bounded handoff. | Smoothing and modulation are declared per parameter rather than globally guessed. |
| Automation | Define timestamped parameter scheduling semantics separately from the UI. | Basic live edits support the core workflow; editable lanes, recording, musical transport, and persistent automation require scope approval. |
| Resource limits | Qualify node count, memory, events, and callback work before activation. | Exact limits follow representative workloads, not arbitrary claims of unlimited complexity. |

These are recommendations that resolve enough semantics to guide implementation design. Any change must retain explicit patch meaning, deterministic ordering, visible failures, and realtime safety.

## DSP graph

### Editable graph, prepared plan, and active state

Keep three distinct representations:

- **Editable graph:** The accepted project revision, containing stable node/port identities, connections, parameter base values, and relevant layout information. The project system owns it.
- **Prepared execution plan:** A validated description of execution order, buffer ownership, node preparation, rate transitions, channel mapping, and resource requirements for a particular graph revision and effective render configuration (rate, buses/layout and capacity), whether supplied by a device or a future external host.
- **Active processing state:** The selected plan plus mutable per-node DSP state, the session sample clock, effective parameter state, and bounded scheduled events. The audio runtime owns it.

Canvas positions and selection do not influence sound or execution order. A successful edit/save does not prove a new revision is audible. Expose accepted, prepared, active, or failed status against the relevant revision.

### Nodes, ports, and connections

Each node definition declares identity/version, ports and direction, rate category, channel requirements, parameter metadata, processing behavior, state/reset behavior, latency/tail characteristics where applicable, and local descriptions.

Each node instance has its own identity and DSP state. Sharing a node definition does not share oscillator phase, filter history, or delay memory between instances.

Ports are explicitly **audio-rate** or **control-rate** in the proposed graph. Parameter events are a separate scheduled-control channel, not audio samples disguised as generic objects. Other value/event categories require defined semantics before support.

Connections must validate direction, type/rate, channel compatibility, ownership, and cycle rules. Proposed baseline rules:

- Fan-out is allowed: one immutable output can feed several consumers.
- A normal input has one upstream source. Combine multiple sources through an explicit mixer/summing node so gain and ordering are visible.
- An unconnected audio input supplies silence; an unconnected control input uses its documented default. Nodes that require a connection reject activation instead of guessing.
- No implicit mono/stereo expansion, channel truncation, summing, or resampling. Use an explicit supported adapter or reject the connection.
- Standalone device-output mapping is explicit and validated against effective output configuration. The render boundary also permits explicit graph input/output buses for a future external host, without implying live device-input support in the MVP.

Modules, if approved under the PRD's reuse direction, resolve to the same graph rules. Module expansion preserves stable instance identity and traceability back to the visual patch; it does not invent hidden timing or permit otherwise invalid cycles.

### Feedback policy

Reject all directed cycles in the baseline and identify the involved nodes/connections in diagnostics. This is a conservative proposed scope choice, not a claim that feedback is musically unimportant.

Future feedback support would require proving a causal delay on every loop, defining delay granularity, read/write phases, initialization, and behavior across callback sizes. A generic block delay would make sound depend on callback size unless its semantics were defined independently. Zero-delay algebraic loops require a different explicit design and are not solved through arbitrary node ordering.

## Node execution model

### Lifecycle

| Phase | Context | Responsibilities |
|---|---|---|
| Validate | Application/domain and preparation context. | Check definition availability, parameter values, ports, rate/channel rules, topology, and supported capabilities. |
| Prepare | Worker/control context, outside callback. | Resolve execution order, allocate buffers and state, prepare coefficients/tables, declare latency, and establish processing limits. |
| Activate | Controlled render boundary. | Select a fully prepared revision without waiting or allocation, acknowledge its identity, and retain valid ownership of all referenced state. |
| Process | Device- or future host-driven render context. | Produce the requested bounded span of output using declared inputs, effective parameters, and instance state, without requiring UI or device access. |
| Retire | Control/worker context after render use ends. | Release obsolete graph/resources only after safe acknowledgement; never free state still reachable by processing. |

Stateful nodes advance only according to the processed sample timeline and their declared rate. Nodes must fully initialize their output spans, never depend on stale buffer content, and keep output ownership explicit.

Preparation may use immutable tables shared across instances. Mutable DSP state is owned by the active instance. Any transfer of phase/history across graph revisions requires a defined race-free handoff; workers must not read or copy mutable live state concurrently.

### Changes during playback

- **Ordinary value changes** target prepared parameters without graph reconstruction, using validated timestamped updates.
- **Preparation-dependent changes** such as channel layout or a resource size require a new prepared plan; they are not treated as cheap sample-time events.
- **Structural edits** activate as a complete revision at a controlled boundary. A late older preparation result cannot replace a newer accepted target.
- Validate node-state reuse by stable identity, definition version, and compatible configuration. If preserving state cannot be proved safe, disclose/reset it under a defined transition policy.

Proposed initial transition policy: on preparation failure, keep the last valid graph if it remains safe and show the newer edit as inactive. Device failure or runtime integrity failure stops audio. For transitions requiring reset, prefer an explicit stop/restart over an unqualified promise of seamless editing. Crossfades and more sophisticated continuity are optional designs with additional CPU/memory costs.

An unconnected or unused node must not advance unpredictably. The proposed baseline renders only nodes reachable from explicit processing roots, normally graph outputs mapped to a device (or future host buses). If inspection of otherwise disconnected nodes is later supported, its state-advancement behavior requires a documented policy; merely opening a view must not change audible behavior.

## Parameter system

### Metadata and identities

Define stable parameter identities within a versioned node definition. Metadata includes value type, default, valid domain, units, display mapping, whether live changes/automation/modulation are supported, smoothing policy, and whether a change requires preparation.

Keep physical values separate from normalized UI controls. A logarithmic knob display does not automatically make automation logarithmic. Persist values in a defined domain with enough version information to detect incompatible meanings.

Reject non-finite numeric values, invalid enum values, and out-of-range base/automation values with actionable feedback. A deliberate clamp/limiter node has its own visible semantics; it is not permission to hide invalid user input.

### Layers and precedence

Do not confuse the saved **base value**, an **automation target**, a **live override**, and the **effective value** used by DSP.

Proposed precedence for an automatable parameter:

1. Use the base value when automation is disabled or absent.
2. When automation is enabled, evaluate its target on the session timeline.
3. An explicit live override temporarily replaces that target until the user releases the override or disables/replaces it.
4. Apply the parameter's declared smoothing to the selected target, if applicable.

A manual edit while automation is enabled becomes a visible override, not an undisclosed rewrite of automation. Recording that gesture into automation is outside the baseline. The UI shows both automation/override state and effective value where relevant.

Audio-rate modulation is a separate future capability. The baseline does not implicitly add a connected signal to a scalar parameter. Before enabling modulation, define the affected parameters, depth/domain, combination order, range policy, smoothing placement, and port/rate semantics.

### Smoothing and discrete changes

Continuous parameters may specify a time-based ramp or smoother; discrete switches/enums must not be interpolated numerically. Smoothing duration is defined in time/sample semantics, not "one callback," so changing backend period does not change it.

For initial consistency, scheduled automation produces targets and parameter smoothing applies afterward. A sample-accurate target therefore need not produce an instantaneous audible jump; expose the difference. Parameters requiring exact step behavior explicitly declare no smoothing.

Expensive coefficient/resource preparation stays outside the callback or uses a qualified bounded per-sample/block update path. If an event would require unavailable prepared data, reject it or retain the prior effective state with a reported failure, rather than allocate during rendering.

## Automation system

### Scope and clock

Define automation as a sequence of typed parameter targets at positions on an audio-session timeline. This establishes runtime semantics, not a commitment to a lane editor, MIDI, scripting, musical beats/tempo, or recorded automation in any release.

Use a sufficiently wide integer sample-frame clock for the active session. It advances by frames actually rendered, not wall-clock elapsed time or UI ticks. At the standalone baseline, a new start creates a new session epoch at frame zero; stopping ends it. The epoch distinguishes delayed events from an earlier run.

No background time elapses while standalone audio is stopped. Pausing, seeking, looping, resuming transport, musical time, and sample-rate-independent persisted timelines are unresolved product features. A rate change ends/reprepares the session rather than silently reinterpreting event sample positions. A future plugin needs an explicit adapter between the monotonically processed-frame clock and host transport/automation positions, including stopped-transport rendering, discontinuities and offline rendering. Do not apply standalone Start/Stop rules to an external host.

### Event identity and order

An event identifies session epoch, target node/parameter, intended frame, target value, and an application-assigned acceptance sequence. Events depending on a structural revision also identify that revision; compatible live-value targets can remain valid across later revisions with the same node/parameter meaning.

Order events by frame and then acceptance sequence. For multiple target events on the same parameter at the same frame, the last accepted target wins. Override/release events use the same ordering, followed by the declared precedence rules. Do not rely on dictionary order, UI arrival timing in the callback, or thread scheduling.

Only events for the active epoch and valid target definitions can be applied. Deleted/incompatible targets produce an explicit cancellation outcome. Old-revision events must not mutate a reused node identity with a different meaning.

### Delivery and interpolation

Schedule ahead through a bounded, prevalidated handoff. Keep distant future content in control-owned storage and feed a bounded horizon; do not copy an unbounded timeline into the callback.

Live unscheduled edits become eligible at the next render-boundary opportunity after acceptance. They are responsive but are not promised to have sample-accurate wall-clock timing. Scheduled in-buffer events can be sample-position accurate within the rendered stream if delivered before that span begins.

Proposed interpolation starts with step targets; linear interpolation is allowed only for parameters whose definitions explicitly support continuous interpolation in a specified value domain. Other curves and musical timing require separate decisions. Parameter smoothing still follows target evaluation.

Handle late live events at the next eligible boundary and report their applied position. Reject late pre-scheduled events as missed rather than replaying a burst or pretending they were punctual. Coalesce only replaceable unscheduled continuous targets; never silently discard scheduled points, discrete actions, release/override transitions, stop intent, or failures.

Preparation limits events and distinct event boundaries per processing span. If capacity would be exceeded, reject the schedule with feedback before activation; keep callback work bounded. Exact limits are qualification decisions. Queue saturation must be reported off-callback and cannot starve stop/fault handling.

## Audio-rate processing

Audio-rate nodes compute sample streams at the effective render sample rate supplied by the standalone device or future host adapter. They receive an actual frame count and processing span, not a hard-coded assumption of 128 or 256 frames. A zero-frame host call must not advance sample state; adapter-specific control/lifecycle handling requires validation.

The runtime may divide a backend callback into subspans at parameter-event positions, control ticks, or qualified internal size limits. Every frame is rendered once and in order; subdivision adds no implicit queue of audio or extra callback of latency.

Use preallocated working storage sized to an approved capacity. Larger host requests can be processed in bounded-capacity chunks without losing sample/control alignment; total work is still proportional to host frame count. A host request outside a qualified workload limit is a reported backend/runtime fault, not a reason to allocate unexpectedly.

Persist oscillator/filter/delay history across subspans and normal callbacks. Node behavior must not depend on how equivalent samples were partitioned, except where a node explicitly declares a fixed-hop/window algorithm.

Windowed or fixed-size algorithms need an internal adapter with declared accumulation, hop alignment, and algorithmic latency. This is not permission to add a silent block delay to every node. The exact buffer layout and sample precision remain preparation/runtime choices, provided they preserve channel and numerical semantics.

## Control-rate processing

Control-rate processing updates scalar/control values less frequently than audio, with a declared hold or conversion behavior. It is not the JUCE message/UI refresh rate and is not automatically one execution per callback.

For the proposed baseline, select a graph-wide control interval in sample frames during preparation. Control ticks occur at absolute session-frame multiples of that interval, including frame zero. Callbacks split at those tick positions so the same patch has the same control timing at different backend buffer sizes.

At a tick, evaluate control-rate nodes in their prepared dependency order. A produced value remains held until the next tick. Control-to-audio conversion uses an explicit hold or interpolation/smoothing adapter with declared behavior; linear interpolation cannot assume knowledge of a future value that has not been produced.

An event can update a control node's parameter target between ticks, but that node's output changes only at its next tick. Report this quantization rather than claiming sample-accurate control output. Its parameter smoother evaluates elapsed sample time on the same timeline regardless of callback subdivision. Newly activated control nodes use their declared initial held value until the next tick; compatible reused state preserves its held value.

Audio-to-control conversion requires an explicit detector/decimator with a defined observation window and availability delay. It is outside the initial baseline; rejecting such a connection is preferable to an implicit callback-average whose timing varies with hardware settings.

Audio-rate consumers may use held control values without introducing an implicit rate change to the audio path. Dependencies for adapters still participate in graph validation. If later mixed-rate feedback is added, its causality must be designed explicitly rather than bypassing the cycle check.

## Graph scheduler

Separate a **preparation scheduler** from the **render scheduler**.

### Preparation scheduler

For each target graph revision and effective render configuration:

1. Resolve node definitions and validate parameter, port, channel, and rate semantics, including explicit adapters.
2. Identify output roots and their required upstream nodes. Report disconnected branches as inactive rather than silently deleting them from the project.
3. Detect cycles, including any introduced through module expansion or adapters; reject unsupported topology with localized diagnostics.
4. Produce a topological ordering of the required graph. Resolve ties by a documented canonical ordering of persisted stable node identities, not visual layout.
5. Derive audio and control schedules consistent with those dependencies; plan buffer ownership, lifetimes, mixing order, node latency, and bounded event capacity.
6. Prepare resources off-callback and publish a ready plan with revision/configuration identity. Invalid or obsolete plans never become active.

Do not perform sorting, graph traversal, module expansion, allocation, or dependency discovery during audio rendering.

### Render scheduler and execution order

At each callback and each relevant subspan boundary:

1. Observe dedicated stop/fault state and eligible prepared-plan activation at the defined callback boundary. Acknowledge the active epoch/revision; do not activate structural changes halfway through a callback.
2. Apply due parameter/override events in timestamp/sequence order before computing the affected frame.
3. If this frame is a control tick, execute the prepared control schedule using the current effective parameters and hold its resulting outputs.
4. Evaluate audio-rate parameter trajectories and explicit control-to-audio adapters, then execute audio nodes in the prepared topological order for the next span.
5. Route initialized output to the explicit graph output buses (mapped to device channels in standalone use), advance the sample clock, and expose only bounded health/activation observations.

At coincident timestamps, parameter events precede control evaluation, and control evaluation precedes audio evaluation. Within an audio node, smoothing advances in sample order; it must not be applied twice merely because a span was split.

At the end of one span, events exactly at the following frame apply to the following span, not the already rendered samples. A callback boundary is not a second opportunity to apply the same event.

### Determinism

The same initial state, graph/configuration, sample-clock event sequence, and explicit random seed should yield the same semantic result regardless of canvas layout or equivalent callback subdivision. Sources of randomness declare their seed/reset policy.

Use stable summation order for explicit mixers. Avoid unordered reductions and implicit parallelization that change floating-point evaluation order. Bit-identical output across CPU architectures, math libraries, and runtime optimizations is not promised; numerical tolerances must be declared per algorithm and test.

Determinism covers ordering and declared timing, not identical wall-clock arrival of unscheduled human gestures.

## Latency, state, and failure semantics

Separate graph-intrinsic latency from control delay and backend/output latency defined in the audio platform strategy. Nodes declare fixed or configuration-dependent algorithmic latency where known; an unknown value is not reported as zero.

Parallel branches may have different delays. The standalone baseline does not insert automatic latency compensation; make delay differences inspectable and use explicit delay alignment if supported. Future plugin hosting/export requires explicit graph latency/tail/bypass contracts and a decision on compensation or rejection of unsupported topologies before shipping; unknown latency must not be reported as zero. Compensation, look-ahead, tail preservation, and bypass behavior require node-specific policies before introduction.

Define supported input defaults and validated finite values before rendering. If runtime output becomes invalid or processing fails unexpectedly, initialize the affected device output safely, signal a bounded fault, and stop through the control owner. Do not silently clamp all numerical failures or keep presenting a failed graph as healthy.

Keep editable work and the last successful save intact after a processing failure. Diagnostics and visualization are consumed off the audio path. Stop is not dependent on the ordinary event queue or visualization keeping up.

## Future plugin processing constraints

The revised architecture must accommodate these capabilities; none is implemented or assigned to a named release:

- **VST3 hosting:** A future hosted-processor node adapts JUCE plugin instances to the existing node contract. Declare buses, parameters, preparation dependencies, latency, tails and bypass/reset semantics. Scan/load, state serialization and editors stay off rendering. Do not assume third-party processors are stateless, bounded, safe to split at every control tick, or allocation-free. Qualify supported block/subspan behavior and any buffering/latency explicitly; reject incompatible nodes rather than silently changing the scheduler.
- **Plugin export:** A future JUCE processor wrapper supplies host buffers/context to the same engine, without standalone device or editor dependencies. Preserve independent state per instance and stable exposed parameter identities; map host changes/gestures to parameter precedence without double smoothing or feedback loops. Host API timing resolution must be disclosed rather than assumed sample-accurate.
- **Export eligibility:** Explicit graph input/output roots, validated content, versioned state and finite resource requirements define a future validation boundary, not a promise that every patch can be exported. Whether export embeds a verified plan or compiles native DSP, which formats are delivered, and how resources are packaged remain open. No source generation is authorized here. Hosting a plugin does not establish a right or capability to bundle it in an export.
- **Safety and determinism:** Native plugin code is not constrained by VAL Script. Scan/runtime isolation, overload/crash recovery, dynamic latency changes and non-deterministic external behavior require separate policy. Built-in determinism claims do not extend automatically to arbitrary third-party nodes.
- **Host lifecycle:** Define rate/layout changes, variable or oversized blocks, repeated prepare/release, no-editor operation, state restore, offline rendering and transport discontinuities. Integrity faults initialize returned output safely and surface bounded status; the plugin cannot stop the external host's hardware device.

## Optimization opportunities

Every optimization requires measured benefit and equivalence tests; none authorizes code generation.

| Opportunity | Potential benefit | Required safeguards |
|---|---|---|
| Output-reachability pruning | Avoid work on branches that cannot affect processing roots. | Preserve explicit side-effect/inspection policies and state-advancement semantics; do not mutate saved topology. |
| Buffer reuse and read-only fan-out | Reduce memory and copies. | Plan last-use ownership off-callback; never overwrite data still needed by another consumer. |
| Constant preparation/folding | Avoid recalculating truly invariant values. | Exclude stateful, random, automated, or time-dependent behavior; invalidate on relevant changes. |
| Incremental preparation | Reduce edit-to-ready time. | Key caches by definition/configuration/revision dependencies; never expose partly rebuilt plans or reuse incompatible state. |
| SIMD and contiguous processing | Reduce per-sample CPU cost. | Verify alignment, tails, supported CPU features, and numerical tolerances on all targets. |
| Span batching | Reduce dispatch overhead for compatible event-free spans. | Preserve event/control boundaries and state evolution; do not trade hidden buffering for throughput. |
| Coefficient/table caching | Move repeated preparation work off-callback. | Bound memory and invalidation; immutable sharing only, with safe lifetime. |
| Silence/constant propagation | Skip genuinely redundant operations. | Account for oscillator evolution, filter history, delay tails, pending events, and nonzero behavior after silence. |
| Parallel branch processing | Potentially exploit larger CPUs. | Future-only: prove dependency/order semantics and deadline behavior without callback waits, thread-pool scheduling, or unbounded synchronization. |

Do not optimize based solely on average CPU. Include tail latency, memory pressure, allocation/deallocation and destructor timing, callback deadlines, and the declared node/automation workload. C++ ownership must ensure plan/resource retirement occurs outside rendering; native compilation is not a hard realtime guarantee.

## Validation and unresolved decisions

Architecture validation should cover:

- Stable topological ordering, explicit fan-in, cycles, missing definitions, incompatible channels/rates, and disconnected branches.
- Equivalent results for different callback sizes and subdivisions, including control ticks and coincident/same-frame automation events.
- Parameter range/type checks, smoothing duration, overrides/releases, late events, queue saturation, session restart, and stale graph/event rejection.
- Independent node state, explicit reset behavior, prepared-plan activation/retirement, and safe stop during faults or overload.
- Offline save/reopen of base values and graph meaning. Persistent automation remains conditional on a separate format/timeline decision.
- Cross-platform numerical tolerances and workload qualification against the proposed latency/stability goals in AudioPlatformStrategy.md.
- Device-free and editor-free render tests that preserve future plugin reuse; before plugin delivery, qualify host lifecycle/automation/state, latency/tails, and hosted-node subspan behavior separately from built-in-node determinism.

Still unresolved: the exact node inventory, supported control intervals, detailed smoothing/interpolation options, limits, fixed-window algorithms, numerical precision/layout, persistent automation UI/format, advanced modulation/feedback, state-transfer mechanics, and latency-compensation policy. Plugin isolation, host timing fidelity, export eligibility/formats and delivery releases also require explicit decisions.

These decisions must be settled and validated before the corresponding capability is claimed. No source code, generated code, benchmark results, or implementation scaffolding is included.
