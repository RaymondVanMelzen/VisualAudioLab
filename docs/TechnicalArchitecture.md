# Visual Audio Lab: Technical Architecture

## Status, scope, and governing sources

This is a proposed technical architecture, not a description of implemented software. It refines [FunctionalArchitecture.md](FunctionalArchitecture.md) within the product intent of [ProductVision.md](ProductVision.md) and [PRD.md](PRD.md).

**Required stack:** C++20, JUCE, and CMake. Primary standalone audio targets are Windows using ASIO and macOS using CoreAudio. This direction supersedes the earlier managed/UI stack and portable device-library recommendation. The architecture must support future VST3 hosting and plugin export, without claiming either is implemented or assigning either to the MVP or a named release. WASAPI is only a possible future compatibility option, not a required primary path.

All target names, interfaces described in prose, and diagrams below are recommendations. No build files, binaries, runtime, or platform adapter are created by this document. Dependencies on a subscription, account service, cloud service, or AI service are excluded from the core workflow. Pin a compatible JUCE revision, CMake version, compiler/toolchain and SDK set before implementation; framework defaults do not select product features.

## Clean Architecture

Organize the system around inward-pointing dependencies:

- **Domain:** Patch meaning, node/port/parameter rules, identities, and validation independent of UI, storage, and device mechanisms.
- **Application:** Project lifecycle and use cases, editable state, undo/redo coordination, and boundaries for persistence and audio control.
- **Adapters and runtime:** Audio execution and implementations of persistence, device, diagnostics, and other local-platform capabilities.
- **Presentation:** JUCE components and separated presentation state, expressing user intent through application use cases.
- **Composition root:** The executable that selects and connects implementations for the current platform.

Runtime calls may travel outward through interfaces defined by an inner layer. That does not reverse compile-time dependency direction. Functional dependencies in the functional architecture describe collaboration, not necessarily CMake target dependencies.

Avoid one library per feature, a universal shared-kernel library, or a framework-heavy domain. Introduce another target only when dependency isolation, lifecycle, testing, or platform packaging justifies it.

## Solution structure

The proposed CMake project is named **VisualAudioLab**. It contains six baseline production targets and focused test targets. Optional plugin adapters are future extension points, not baseline deliverables. The tree is a UML-style containment sketch, not an instruction to scaffold files.

```text
<<CMake project>> VisualAudioLab
|
+-- src
|   +-- VisualAudioLab.Domain
|   +-- VisualAudioLab.Application
|   +-- VisualAudioLab.Audio
|   +-- VisualAudioLab.Infrastructure
|   +-- VisualAudioLab.Presentation
|   +-- VisualAudioLab.Desktop
|   +-- [future] VisualAudioLab.PluginHosting
|   +-- [future] VisualAudioLab.PluginAdapter
|
+-- tests
|   +-- Domain.Tests
|   +-- Application.Tests
|   +-- Audio.Tests
|   +-- Presentation.Tests
|   +-- Integration.Tests
|
+-- docs
```

CMake is the build-system source of truth; generated Visual Studio/Xcode projects are outputs, not separately maintained build definitions. Specify C++20 at target boundaries, pin dependencies, and keep platform flags and JUCE module options scoped to the targets that need them. JUCE provides CMake helpers for GUI applications and plugins [T1]; using a plugin build helper is not itself a patch-export workflow. Exact compiler versions, generator, test framework, packaging and dependency acquisition remain unresolved. Builds should be repeatable with dependencies provisioned locally; end users must not require a toolchain or online fetch for the core application.

## Project structure and native artifacts

Use CMake library targets for responsibility boundaries, initially recommending static linkage where suitable, and an executable/application-bundle target for Desktop. Native libraries are not CLR assemblies, and a library boundary does not require a DLL, separate process, or stable binary ABI. Shared-library/export visibility and plugin bundles require explicit packaging decisions.

| CMake target | Role and suggested internal areas | Allowed production target dependencies |
|---|---|---|
| VisualAudioLab.Domain | C++ library: Projects, Patches, Nodes, Connections, Validation, and optional future Modules. Holds meaning and invariant checks, not device-dependent execution. | None; standard C++20 only. |
| VisualAudioLab.Application | C++ library: ProjectSessions, Editing, AudioControl, PersistenceContracts, Notifications, DiagnosticsContracts. Contains use cases, project-state coordination, and outward-facing interfaces. | Domain. |
| VisualAudioLab.Audio | C++ library: GraphPreparation, Processing, RuntimeControl, RenderContracts, Observation. Owns host/device-independent processing state. | Application, Domain. JUCE DSP/audio-basics utilities may be private implementation details after qualification; no GUI/device modules. |
| VisualAudioLab.Infrastructure | C++ adapters: LocalPersistence, Devices, PlatformIntegration, Diagnostics. JUCE device adapter selects ASIO/CoreAudio. Maps external representations to internal contracts. | Application, Domain, Audio; required JUCE device/core modules privately. |
| VisualAudioLab.Presentation | JUCE component library: Views, PresentationModels, PatchCanvas, Inspectors, Dialogs, PresentationServices, Resources. Contains presentation state and UI-specific behavior. | Application, Domain; JUCE GUI/event modules. |
| VisualAudioLab.Desktop | JUCE application: Startup, Composition, PlatformSelection, Lifetime. Assembles the standalone application; does not contain patch/business rules. | All five libraries, for composition only; JUCE application support. |
| VisualAudioLab.PluginHosting, future | Adapter for VST3 discovery, instances, editors and lifecycle, with a separately decided scan/isolation design. | Audio, Application, Domain; JUCE processor/hosting modules and GUI adapter only where needed. |
| VisualAudioLab.PluginAdapter, future | JUCE processor wrapper around the reusable graph engine; CMake plugin-format target packages it for an external host. | Audio, Application, Domain; JUCE processor/plugin-client modules; optional Presentation editor. No standalone device/composition dependency. |

These are responsibility groupings, not a demand to create empty folders. Keep related use cases and presentation features together as they become necessary.

**Contract ownership:** Application owns audio-control, project-store, and application-notification boundaries. Audio owns host-neutral render and observation contracts; the standalone device adapter and future external-host adapter supply different render contexts. Infrastructure implements device/storage contracts. Domain owns only domain types and rules. Presentation owns UI-only boundaries such as view interaction; application workflows return decisions/results rather than opening native dialogs themselves. Split persistence/diagnostics from the device target if reused in a future plugin; never link the standalone device owner into the plugin merely to serialize state.

**Persistence distinction:** Persisted representations belong to Infrastructure, not Domain. Application receives a loaded-project description and diagnostics for validation; a readable file does not automatically become the active project or start audio.

### UML-style target dependency diagram

An arrow means "has a compile-time target dependency on"; the diagram is not runtime execution order. Repeated arrows to Domain are intentional.

```text
<<entry>> Desktop
  --> Presentation
  --> Infrastructure
  --> Audio
  --> Application
  --> Domain

<<UI>> Presentation       --> <<use cases>> Application
          |                                  |
          +--> Domain                        +--> Domain

<<adapters>> Infrastructure --> Application
          |                 --> Audio
          +--------------------> Domain

<<runtime>> Audio          --> Application
          +-------------------> Domain

<<core>> Domain            --> no production projects

[future] PluginHosting --> Audio, Application, Domain, JUCE hosting
[future] PluginAdapter --> Audio, Application, Domain, JUCE plugin client
                      --> optional Presentation editor
PluginAdapter -X-> Desktop or standalone device owner
```

There are no target-dependency cycles. Infrastructure references Audio to implement its device contracts; Audio never references Infrastructure to reach a concrete device backend. A future Desktop composition may additionally depend on PluginHosting; Audio only knows its processor-node contract, not the concrete hosting adapter.

## Dependency rules

1. Domain has no JUCE, filesystem, native-device, logging-provider, DI-container, or service-host dependency. It must remain usable in ordinary unit tests.
2. Application must not reference Presentation, Audio, Infrastructure, or Desktop. It depends on its own capability interfaces rather than concrete adapters.
3. Presentation must not reference concrete audio engines, storage implementations, or platform adapters. Use cases and application read models are its workflow boundary.
4. Audio may implement Application interfaces, but its time-sensitive processing must not call application use cases, UI objects, DI resolution, persistence, or ordinary notification/logging infrastructure.
5. Infrastructure must not reference Presentation or Desktop. Platform differences stay behind the same application/audio contracts.
6. Desktop may reference implementations solely to construct, initialize, and release them. The future plugin entry point has its own composition root and never starts the standalone device manager. Composition access does not justify moving workflow logic into startup.
7. Project and runtime objects do not obtain dependencies from a global service locator. Pass required collaborators explicitly at construction or through a narrow, lifecycle-aware factory.
8. Framework/platform objects and file-format models do not leak across inner-layer boundaries. Map them at adapters; do not make a UI control or native device handle part of a patch.
9. Keep production dependencies offline-capable and review license/distribution compatibility. JUCE is selected by the revised direction; that does not select every optional module or a license path. Its reviewed repository offers AGPLv3/commercial licensing [T4]. Settle a compatible project/distribution policy, including future exported artifacts and SDK obligations, without introducing subscription requirements for users.

Dependency checks and architecture-focused tests should verify these rules once projects exist. Test projects may reference the concrete components they exercise; that does not permit the same references in production layers.

## Mapping functional subsystems to technical boundaries

| Functional subsystem | Proposed primary placement | Collaboration and scope |
|---|---|---|
| Project system | Application; Domain for patch rules. | Owns accepted editable state, revisions, lifecycle, and edit-history coordination; uses persistence/audio interfaces. |
| Patch editor | Presentation. | Views and view models express intent and show accepted/active state; do not own a competing project model. |
| Node system | Domain for definitions/rules; Audio for executable behavior. | A supported component's description and processing behavior must agree. Local help/catalog content may be supplied by Infrastructure; exact distribution is unresolved. |
| Module system | Future Domain/Application rules, Presentation editing, Audio preparation. | Remains proposed reuse discovery, distinct from VST3 hosting and export; no library is created in advance. |
| Audio routing | Domain validation, Application orchestration, Audio runtime. | Separates accepted edit state from prepared/active audio state; Infrastructure connects device output. |
| Audio device manager | Application-facing capability; Infrastructure implementation using Audio device contracts. | Presents platform-neutral status and configuration choices; device settings do not define project identity. |
| Visualizers | Presentation views; Application status models; Audio observation bridge. | Basic status is supported conceptually; advanced signal displays remain discovery. Observing must not become required for processing. |
| Scripting system | No baseline implementation/target; see ValScriptArchitecture.md for the requested design. | Future local compiler prepares bounded DSP-node plans off-callback; only verified plans execute in Audio. No arbitrary host APIs or general-purpose script execution on the callback. |
| Persistence layer | Application store contracts; Infrastructure representation and local I/O. | Domain validation and project acceptance remain separate from storage success. |

## Presentation architecture: MVVM-style separation in JUCE

**Views** are JUCE components responsible for layout, drawing, accessibility information, pointer/keyboard gestures and focus. UI-specific behavior, including canvas hit testing or drawing, may live in the view layer; it must not become the project or DSP engine. MVVM-style separation is an architectural pattern here, not a claim that JUCE provides a declarative binding system.

**View models** expose observable presentation state, selection, commands, validation feedback, and busy/error indicators. Commands invoke Application use cases; commands are not synonymous with background events. Long-running save/load/device operations must not block UI interaction. Completion and failure are reflected explicitly, and stale operation results must not overwrite newer displayed state.

**Models** in the MVVM sense are application read models and domain concepts, not storage records. The project session remains authoritative for accepted patch state. Selection and temporary drag previews may be local presentation state; a preview must not masquerade as an accepted/running edit.

Use focused shell, project, patch-editor, node/inspector, and audio-status view models as needed. Avoid a single view model containing lifecycle, routing, persistence, and device logic. A view model's lifetime follows its window/document and subscriptions are released when it closes.

Presentation models are plain C++ state/commands, adapted to JUCE components on the message thread with explicit lifetime-scoped listeners or equivalent synchronization. Device and audio notifications must not directly mutate UI objects from their originating context. Choose only the minimal binding/notification mechanism needed; no MVVM toolkit, reactive library or docking framework is mandated.

### UML-style MVVM collaboration

```text
<<JUCE Component>> View
  --presentation adapter / user gestures--> ViewModel

ViewModel
  --command--> Application Use Case
  <--result / read model------ Application Use Case
  <--UI-dispatched status----- Presentation Status Adapter

Application Use Case
  --validates--> Domain Rules
  --updates----> Project Session
  --requests---> Capability Interface

Capability Interface
  <|.. Audio or Infrastructure Adapter

Legend: <|.. means "implemented by", not inheritance of UI state.
```

## Dependency injection strategy

Use constructor injection and explicit C++ ownership in Desktop's composition root, and a separate per-instance composition root for a future plugin. Manual composition is the initial recommendation; no DI container or service locator is required. Use RAII and unique ownership by default, non-owning references with documented lifetimes, and shared ownership only where necessary and proven safe. Reference-count decrements must not trigger destruction on the audio thread.

Construct concrete capability implementations and choose the platform adapter during startup. Missing required capabilities must produce a clear startup/device error, not a dummy implementation that reports success. Optional future services must not be required dependencies for the MVP.

| Lifetime | Proposed ownership |
|---|---|
| Application lifetime | Immutable supported-component catalog, diagnostics coordinator, and standalone device-discovery coordination. Shared application services must not retain closed projects; a plugin must not assume one process-wide instance. |
| Project/document lifetime | Project session, editable state, edit history, per-project use cases, view models, and notification subscriptions. This is an explicit desktop scope, not an HTTP request scope. |
| Audio-run lifetime | Prepared active graph, processing state, and observation resources; governed by start/stop and device lifecycle, not view creation. |
| Operation lifetime | Save/load requests and their cancellation/result handling, temporary snapshots, and short-lived validation work. |

Whether multiple projects or simultaneous audio sessions are supported is unresolved. A project scope is still useful for a single-document product; it does not commit to multiple active outputs.

The owner that creates a scope releases it. Application shutdown stops audio and establishes callback quiescence before destroying resources; it then releases document subscriptions and remaining services. Initialization failure follows the same ownership discipline. No construction/destruction of arbitrary dependency graphs occurs on the audio processing path.

## Runtime and audio boundaries

Clean Architecture alone does not make an audio engine real-time safe. Treat interactive UI/use-case work, local I/O, and time-sensitive audio execution as distinct execution contexts without prescribing a fixed thread count.

- Validate edits and prepare processing changes outside the audio callback. Hand over a ready processing state at a controlled boundary; the exact mechanism remains a technical design decision.
- Associate editable, prepared, and active states with a revision identity so delayed preparation or activation results cannot falsely mark a newer edit as running.
- Do not perform filesystem I/O, UI dispatch, synchronous event-handler invocation, unbounded waits, normal logging, DI lookup, or avoidable allocation on the audio path.
- Use an explicitly bounded handoff for control/status/observations. Define queue saturation, update coalescing, acknowledgement, graph retirement, and shutdown policies before implementation.
- High-rate observations may be coalesced or dropped according to a documented policy; operation results, stop/failure state, and accepted edit semantics must not be silently lost. Stop behavior must not rely solely on a potentially saturated ordinary-notification queue.
- Native C++ is not a hard real-time guarantee. Validate allocation/deallocation, destructor timing, page faults, synchronization, OS scheduling, device interaction, and representative workloads against agreed latency/resource budgets. No C++ exception may escape a callback; throwing and exception-driven recovery are not normal render-path control flow.

JUCE supplies the standalone device adapter over ASIO/CoreAudio, not patch meaning or an automatically qualified DSP scheduler. The graph engine accepts rate, bus layout, actual frame count and processing context without requiring a physical device or GUI.

## Future VST3 hosting and plugin export

These are required architectural capabilities, not implemented features, MVP scope, or a promise for Release 2.0. Hosting means running third-party VST3 processors inside Visual Audio Lab; export means packaging eligible Visual Audio Lab processing for an external host. They are separate adapters and release decisions.

- **Reusable processing boundary:** A JUCE AudioProcessor adapter can wrap the graph engine for a future plugin [T2]. Each instance owns independent graph, parameter, clock and state lifetimes and must process without an editor. The external host owns device access, scheduling, playback context and render buffers; the plugin never opens ASIO/CoreAudio itself.
- **Host integration contract:** Define supported buses/layouts, variable/zero-sized blocks, sample-rate changes, precision, stable exposed parameter IDs/ranges/gestures, versioned state restore, bypass, tail and latency reporting. Map host automation/timing into the engine with disclosed precision; JUCE abstraction alone is not proof of sample-offset event fidelity. Standalone start/stop and clock reset rules must not override external transport.
- **Hosting boundary:** JUCE exposes VST3 hosting facilities [T3]. Discovery/scanning, binary loading, instance creation, editor lifecycle and state capture run outside rendering, respecting platform affinity. In-process third-party code can crash, block or allocate; a catch block is not isolation or a realtime guarantee. Scan isolation, processing isolation, timeouts, disabled/missing-plugin recovery and compatibility policy require design before hosting ships.
- **Export boundary:** Separate offline build/packaging from rendering. A future exporter must validate eligible graphs, explicit input/output buses, exposed parameters and resource dependencies; fail visibly on unsupported content. CMake/JUCE plugin targets enable native packaging [T1], but do not decide whether export uses a fixed wrapper plus serialized plan or generated native DSP. Do not promise arbitrary patch export, on-device compiler availability, embedded third-party binaries, nested hosting, or other plugin formats. VST3 is the initial architectural format candidate; the full export format/workflow policy remains open.
- **State and distribution:** Project documents, machine device preferences and host plugin-state blobs have different owners. State restore validates compatibility without launching audio or opening a dialog from processing. Review JUCE/SDK and third-party redistribution terms, signing, identifiers and platform builds for exported artifacts; openness of the app does not license third-party plugins for redistribution.

```text
Standalone: JUCE device adapter (ASIO/CoreAudio) --> Render contract --> DSP
Future exported VST3: External host --> JUCE processor adapter --> Render contract
Future hosting: DSP node boundary --> PluginHosting adapter --> Third-party VST3
Presentation / compiler / exporter --off-callback preparation--> Ready plans
External-host path -X-> standalone device manager or required editor
```

## Event architecture

Separate four kinds of information rather than routing everything through a global event bus:

| Kind | Purpose and ownership | Delivery semantics |
|---|---|---|
| Command/request and result | User intent handled by Application, such as connecting nodes, saving, or starting audio. | Explicit outcome and cancellation rules. A notification alone is not proof of success. Mutations are coordinated per project. |
| Domain change facts | Accepted changes to patch meaning, if decoupled consumers actually require them. | Produced only after validation/acceptance; must not trigger device or storage work from Domain itself. Do not add domain events to every property by default. |
| Application/device notifications | Project changes, save outcomes, device availability, activation revision, and failures. | In-process, typed, scoped, and lifecycle-managed. Include relevant operation/project/revision identity to reject stale results. |
| Audio control and observations | Prepared-state handoff, acknowledgements, stop/fault status, and bounded observations. | Specialized bounded transfer across the audio boundary, not arbitrary event subscribers executing on the callback. |

Ordinary notifications should be published after authoritative state changes. Their ordering must be defined per project/operation where correctness depends on it; no global total order is implied. Consumers should obtain current authoritative state on initial subscription rather than assuming they observed startup events.

Subscriber failures must be surfaced to the owning operation or diagnostics boundary. They must not roll back an already accepted mutation implicitly or turn a failed save into success. Presentation dispatch and handler failures are distinct from domain validation failures.

Do not introduce a broker, cloud messaging, distributed event bus, event sourcing, or an append-only event database. Undo/redo is application edit history, not evidence that event sourcing is required. The exact notification library and transfer mechanism remain unresolved.

### UML-style runtime/event sequence

This interaction diagram illustrates a successful audio edit with an explicit activation result; failure branches produce visible diagnostics instead. Arrows are messages, not project references.

```text
User       ViewModel     Application     Audio Control     Audio Path
 |             |              |               |               |
 |--edit------>|              |               |               |
 |             |--request---->|               |               |
 |             |              |--+ validate (Domain)          |
 |             |              |<-+ accept project revision   |
 |             |<--accepted---|               |               |
 |             |              |--prepare r--->|               |
 |             |              |               |--handoff r--->|
 |             |              |               |<--active r----|
 |             |              |<--status r----|               |
 |             |<--notify r---|               |               |
 |<--display---|              |               |               |

Audio Path --bounded fault/status--> Audio Control
Audio Control --> Application --> UI-dispatched ViewModel
Audio Control --> Diagnostics Adapter --> Local Log Provider

The validation line is local to Application/Domain;
it does not invoke validation on the Audio Path.
```

An accepted edit may remain inactive while preparation occurs. Failed or stale activation is reported against its revision, and the UI must not claim it is audible. Follow DspArchitecture.md's proposed standalone recovery policy: keep the last valid graph on preparation failure if still safe; stop on device or runtime-integrity failure. Transition/state-transfer details and future external-host fault handling still require qualification.

## Logging architecture

Use structured diagnostics at application and adapter boundaries through an application-owned C++ diagnostics interface, with sink configuration in the composition root. No logging package, file format, remote collector, or retention policy is selected. Domain returns validation information rather than depending on a logger.

| Category | Examples | Proposed handling |
|---|---|---|
| Lifecycle and operations | Startup, project open/save result, device selection, audio start/stop. | Concise structured information with operation correlation. |
| Expected validation | Invalid connection, unsupported value, unavailable device. | Actionable user feedback; use diagnostic severity appropriate to the outcome rather than logging every user mistake as a crash. |
| Recoverable failure | Save denied, device removed, activation rejected. | Warning/error according to impact, with state and recovery context; do not clear unsaved state or suppress the failure. |
| Unexpected failure | Broken invariant, unhandled adapter failure. | Error/critical according to application impact; report at a clear ownership boundary and avoid duplicate logs at every layer. |
| High-frequency audio health | Processing overload/fault signals and observation limits. | Bounded counters/status captured without ordinary callback logging; aggregate and log off the audio path. |

Include event category, subsystem, operation identity, project/session correlation, and relevant revision where useful. Prefer non-content identifiers; project names, local paths, scripts, patch content, device details, and exception messages may contain sensitive information and must not be indiscriminately recorded or exported.

Default diagnostics remain local and must not be required for successful patch processing or saving. No automatic upload or telemetry service is assumed. Any future user-initiated support export must be reviewable and redacted. Log location, rotation limits, retention, access permissions, and export format require explicit decisions before distribution.

Logging failure must not block the audio path or overwrite project data. The diagnostics coordinator should expose a bounded non-recursive health indication if logging is unavailable; it must not claim logs were written successfully. User-facing errors remain visible even if a diagnostic sink fails.

## Unresolved choices and architecture validation

Before implementation, select or validate:

- Pinned JUCE/CMake/compiler/SDK versions compatible with C++20, platform/CPU matrix, packaging, license/distribution policy, and lifecycle requirements.
- Qualify JUCE ASIO/CoreAudio adapters and DSP design in DspArchitecture.md; settle live-edit recovery details, output-level behavior, bounded handoff mechanisms, and measurable workload budgets.
- Local project representation, dependency identification, migration/original-preservation policy, settings ownership, and crash-recovery scope.
- C++ ownership/handoff and notification implementation; logging sink, local retention/redaction policy, and JUCE accessibility/UI testing approach.
- Single-document versus multi-document behavior and ownership of concurrent audio use.
- Separate release approval and persistence/trust details for VAL Script; advanced visualizers and module reuse remain subject to the PRD's scope decisions.
- VST3 scan/runtime isolation, parameter/event fidelity, host transport mapping, export eligibility/formats/toolchain UX, latency compensation and state compatibility. No plugin feature has a release assignment.

Validate dependency direction, domain/use-case rules, view-model transitions, subscription lifetime, save/load round trips, activation revision handling, and device failure on both platforms. Exercise the render contract device-free to detect hidden GUI/device dependencies. Before plugin release, add host/processor compatibility, multiple-instance and editor-free tests. Audio workload and UI responsiveness require real integration measurements; unit tests cannot establish real-time suitability. No implementation or benchmark results are claimed by this document.

## Architecture sources

Official sources accessed **2026-10-30**; default branches are mutable. These establish framework capabilities, not a selected version or a shipping feature.

| ID | Source | Evidence |
|---|---|---|
| T1 | [JUCE CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md) | GUI/plugin target helpers, build/toolchain and format configuration. |
| T2 | [JUCE AudioProcessor](https://docs.juce.com/master/classjuce_1_1AudioProcessor.html) | Processing lifecycle, buses, plugin wrapper and hosted processor abstraction. |
| T3 | [JUCE processor module](https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_processors_headless/juce_audio_processors_headless.h) | VST3 host configuration and processor facilities; module layout must be checked at the pinned revision. |
| T4 | [JUCE license](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) | Open-source/commercial licensing and dependency inventory obligations. |
