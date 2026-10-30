# Visual Audio Lab: Functional Architecture

## Purpose and governing sources

This document defines the conceptual responsibilities and information exchanged by nine subsystems. It is governed by [ProductVision.md](ProductVision.md) and [PRD.md](PRD.md). It describes functional boundaries, not an implementation. The revised technical direction is C++20, JUCE and CMake, with primary standalone audio paths Windows/ASIO and macOS/CoreAudio; details belong to [TechnicalArchitecture.md](TechnicalArchitecture.md) and [AudioPlatformStrategy.md](AudioPlatformStrategy.md). Storage format, detailed threading/process layout and plugin delivery scope remain unresolved.

The core experience remains visual audio programming for former programmers, hobby musicians, sound designers, and DSP students. Every supported core workflow is local and offline-first on Windows and macOS, with open-source availability and no subscription, cloud dependency, or AI dependency.

The PRD's capabilities and release sequencing remain proposals. Defining a requested subsystem here does not add it to the MVP or establish a delivery commitment.

## Terminology and assumptions

| Term | Functional meaning |
|---|---|
| Project | The user's locally saved work: patch structure, parameter values, relevant editing state, and any declared project dependencies. Exact project composition is unresolved. |
| Patch | A visual arrangement of node instances and their connections that expresses audio behavior. |
| Node | One instance of an available component, with identified ports, parameter values, and defined behavior. |
| Node definition | A component's meaning and capabilities: ports, parameters, supported values, descriptions, and processing behavior. |
| Module | A proposed reusable grouping of patch structure exposed through a defined boundary. It is not synonymous with a node, a third-party plugin, or a compiled extension. |
| Connection | A declared link between supported ports. Audio connections carry sound; any additional connection categories require definition before use. |
| Active audio state | The validated behavior currently running, distinct from unsaved edits or a proposed connection that has not been accepted. |

Assumptions and unresolved decisions:

- **A1 - Interactive audio:** Retain PRD assumption A1: users hear a patch while adjusting it. DSP execution is included functionally under audio routing so the audio-producing responsibility is explicit; its eventual technical decomposition is undecided.
- **A2 - State ownership:** The project system coordinates authoritative editable project state. The editor presents that state; persistence stores it; audio routing maintains active audio state. This is a conceptual ownership rule, not a prescribed data structure.
- **A3 - Modules:** Reusable patch groupings are a working interpretation of the requested module system. Richer organization/reuse remains proposed 2.0 discovery under FR-13; nested structures, interfaces, sharing, and versioning require design.
- **A4 - Visualizers:** Basic values and audio/routing status support the existing workflow. Dedicated signal inspection, such as waveform or frequency displays, remains future discovery rather than an MVP promise.
- **A5 - Scripting:** [ValScriptArchitecture.md](ValScriptArchitecture.md) refines the requested future scripting boundary into bounded DSP-node compilation, not unrestricted project automation. The PRD excludes a custom-code environment from the MVP and does not commit scripting to any later release.
- **A6 - Processing semantics:** [DspArchitecture.md](DspArchitecture.md) proposes typed rates, explicit channels/conversions, deterministic scheduling and cycle rejection. Detailed limits and budgets still need qualification. Unsupported behavior must be identified rather than guessed.
- **A7 - Persistence and compatibility:** Local save/reopen is foundational. File representation, external-resource handling, recovery mechanisms, and migration policy are undecided; compatibility and original-preservation requirements still apply.
- **A8 - Future plugins:** Support future VST3 hosting and plugin export architecturally without making them required for core use or assigning a release. A reusable patch module is not a native plugin. Export packaging and hosting are separate workflows with unresolved eligibility, compatibility and trust policies.

## Ownership and scope overview

| Subsystem | Owns functionally | Does not own |
|---|---|---|
| Project system | Project lifecycle, accepted editable state, unsaved-change status, edit history coordination. | Audio execution, physical storage, or device operation. |
| Patch editor | Visual interaction, selection, layout presentation, and user feedback. | Independent copies of project truth or DSP/device execution. |
| Node system | Available node definitions, instance rules, parameter validation, and component descriptions. | Project lifecycle or hardware configuration. |
| Module system | Proposed reusable-group boundaries and their validity. | A mandatory plugin ecosystem or new processing semantics. |
| Audio routing | Connection validity, active signal flow, audio processing, and run/stop state. | Saving projects or choosing physical devices on its own. |
| Audio device manager | Available output capabilities, selection, readiness, and device status. | Patch structure or sound-generation behavior. |
| Visualizers | Presentation of supported values, status, and optional observations. | Altering sound merely by observing it. |
| Scripting system | If approved for delivery, local compilation into bounded validated DSP-node artifacts. | Arbitrary host APIs, a bypass around validation, or a prerequisite for visual patching. |
| Persistence layer | Local read/write outcomes, stored representation, and compatibility reporting. | Deciding user edits or starting audio. |

## 1. Project system

### Responsibilities

- Coordinate creating, opening, saving, and closing projects, including bundled examples.
- Maintain accepted editable patch structure, parameter values, and relevant layout state; distinguish unsaved changes from the last successful save.
- Coordinate ordinary edit history for undo/redo and confirmation before discarding unsaved changes, consistent with FR-10.
- Request validation from the relevant subsystem before accepting changes; distinguish acceptance as an edit from activation in running audio.
- Coordinate loading and dependency checks without silently replacing unavailable components or discarding unsupported information.
- Present project-level save, load, compatibility, and dependency outcomes to the editor.

### Inputs

- User lifecycle requests and proposed patch/parameter/layout changes from the patch editor.
- Node and, when supported, module definitions and validation outcomes.
- Loaded project content, compatibility findings, and save results from persistence.
- Routing acceptance, activation outcomes, and runtime status; verified script-node definitions if VAL Script is approved for delivery.

### Outputs

- Authoritative accepted project state, selection-independent change information, unsaved-change status, and edit-history availability.
- Requested patch and parameter changes for audio routing to validate and, when appropriate, activate.
- A consistent project snapshot and explicit local save requests to persistence.
- Load/save results, unresolved-dependency reports, and actionable project errors for user presentation.

### Dependencies

- **Node system:** Validate node identities, parameter values, and available definitions.
- **Audio routing:** Validate connectivity and report whether accepted edits can become active.
- **Persistence layer:** Read and write local work and report compatibility outcomes.
- **Module system, conditional:** Resolve reusable structures if that capability is approved.
- **Patch editor, interaction boundary:** Receive user intent and expose project outcomes; the project must not require a particular visual presentation to preserve its rules.

**Source alignment:** FR-01, FR-03, FR-05, FR-10 through FR-12; NFR-01 and NFR-04.

## 2. Patch editor

### Responsibilities

- Let users add, select, move, connect, disconnect, and remove available components visually.
- Display node names, ports, connections, editable values, and units where relevant.
- Translate user actions into proposed project changes instead of independently changing audio behavior.
- Explain invalid actions, unavailable components, unsaved work, and audio/device problems in context.
- Expose undo/redo, local examples/help, and clear audio start/stop controls; support the PRD's accessible-interaction intent.
- Keep intended routing and actual active-audio status distinguishable when an edit cannot be activated.

### Inputs

- User pointer/keyboard actions and project lifecycle requests.
- Accepted project state, edit-history status, and project outcomes.
- Available node/module descriptions, valid values, and contextual help.
- Routing/device status, connection diagnostics, and supported visualizer presentations.

### Outputs

- Proposed structural, parameter, and layout edits and undo/redo requests to the project system.
- Project open/save/close intent, audio start/stop requests, and output-selection requests.
- User-visible patch views, contextual explanations, validation feedback, and state indicators.
- Observation requests to visualizers where inspection is supported.

### Dependencies

- **Project system:** The source of accepted editable project state and lifecycle outcomes.
- **Node system:** Discoverable components, port/parameter information, and local descriptions.
- **Audio routing and audio device manager:** Start/stop and device-selection outcomes and current status.
- **Visualizers:** Supported observations and status presentation.
- **Module system, conditional:** Reusable structures and their exposed interfaces.

**Source alignment:** FR-01, FR-03, FR-04, FR-06 through FR-10; NFR-05 and NFR-06.

## 3. Node system

### Responsibilities

- Describe the supported component collection, including proposed generation, shaping, level-control, and output roles.
- Define each component's ports, parameter meanings, valid ranges, defaults, units, and local help.
- Validate node instances and parameter changes consistently for editing, loading, and audio use.
- Supply the declared processing behavior needed by audio routing without choosing its technical implementation.
- Report missing or unsupported definitions rather than silently substituting a different sound-producing component.

### Inputs

- The supported release's component definitions and descriptions.
- Node-creation and parameter-validation requests from the project system.
- Port/behavior queries from audio routing and component-discovery queries from the editor.
- Loaded node identities and values that require validation.

### Outputs

- Discoverable component metadata and descriptions.
- Validated node-instance information and parameter values, or explicit validation errors.
- Port capabilities and component behavior for routing validation and processing.
- Missing-component and unsupported-value diagnostics.

### Dependencies

- **Supported component content:** Locally available definitions and help; exact inventory is unresolved.
- **Project system, interaction boundary:** Provides instance/edit requests and retains accepted state.
- **Audio routing, interaction boundary:** Consumes declared node behavior and port capabilities; channel and processing semantics must be agreed before support is claimed.
- **Module system, conditional:** A future module may be exposed as a component only through an explicitly designed node/module boundary.

**Source alignment:** PRD A3; FR-02, FR-03, FR-07, FR-09, and FR-11.

## 4. Module system

**Scope:** Proposed future organization/reuse under FR-13, not an MVP prerequisite. Basic patching must work without user-created modules.

### Responsibilities

- If approved, describe reusable patch groupings and their externally visible ports and parameters.
- Validate a grouping's contained nodes, connections, and exposed interface.
- Preserve the meaning of contained behavior when reused; do not introduce implicit routing or parameter conversions.
- Identify unavailable dependencies and interface changes explicitly.
- Keep reuse understandable to users rather than exposing internal complexity by default.

### Inputs

- Proposed patch groupings and interface definitions from the editor/project workflow.
- Contained node definitions, values, and connections.
- Reuse requests and saved module references or content, according to a future persistence policy.

### Outputs

- Validated reusable definitions and exposed-interface descriptions.
- Resolved contained structure and boundary information for project validation and audio routing.
- Interface, dependency, and unsupported-structure diagnostics.

### Dependencies

- **Node system:** Meaning and validity of contained components and exposed parameters.
- **Audio routing:** Validity and behavior of contained and boundary connections.
- **Project system:** Grouping/reuse intent and ownership of project references or instances.
- **Persistence layer:** Storage of approved module content or references, coordinated through project save/load.

**Open decisions:** Nesting, reuse identity, sharing, edits to existing instances, and compatibility policy require discovery. This subsystem does not itself implement VST3 hosting, plugin export, a marketplace, or code generation; future plugin capabilities have separate boundaries below.

**Source alignment:** FR-13 and the PRD's proposed 2.0/future scope.

## 5. Audio routing

### Responsibilities

- Validate proposed connections against declared port capabilities and supported graph rules.
- Translate a valid patch into active signal flow and execute its declared audio behavior.
- Apply supported parameter changes and report whether requested changes actually became active.
- Own requested/running/stopped/failed audio-state distinctions and honor an explicit stop request.
- Coordinate output delivery with device readiness, surfacing device or processing failures without presenting silence as successful playback.
- Provide supported observations and diagnostics without requiring an active visualizer for normal audio use.

### Inputs

- Proposed patch structure, accepted parameter values, and activation requests from the project system.
- Node processing behavior and port capabilities; resolved module structure if supported.
- Start/stop requests and current device capabilities/readiness.
- Device interruption/failure notifications and optional observation requests.

### Outputs

- Connection-validity results and actionable routing diagnostics.
- Active-audio state and change-activation outcomes for the project system/editor.
- Generated or processed audio directed to the selected available output.
- Processing/device-related failure status and supported observation data for visualizers.

### Dependencies

- **Node system:** Definitions of component behavior, ports, and parameter meaning.
- **Audio device manager:** Output availability, capabilities, and delivery status.
- **Project system:** Desired patch state and changes; routing does not write project files.
- **Module system, conditional:** Validated reusable structures and interface mapping.

**Processing refinement:** DspArchitecture.md proposes cycle rejection, typed audio/control rates, explicit conversion, deterministic scheduling and a last-valid-graph/stop-on-integrity-fault policy. Qualify those choices and transition details before implementation. Unsupported changes must be rejected or clearly reported as inactive; the UI must never imply an unsuccessful edit is running. Keep processing independent of device selection so future host-driven rendering can reuse it.

**Source alignment:** FR-01 through FR-04, FR-06, FR-07; NFR-03 and NFR-07.

## 6. Audio device manager

### Responsibilities

- Discover and describe output devices on the declared Windows/ASIO and macOS/CoreAudio primary support matrix. Do not promise other paths merely because the framework offers them.
- Manage user output selection and report readiness, unavailability, and device changes.
- Coordinate opening, stopping, and releasing output use with audio routing at the functional level.
- Explain configuration failures and device-specific settings that need reassignment after moving a project.
- Keep preferred device settings distinct from the device that is actually available and in use.

### Inputs

- Locally available device capabilities and device-change notifications.
- User selection/configuration requests and any saved device preference.
- Routing start/stop intent and output requirements.

### Outputs

- Available output choices, supported capabilities, and effective selection.
- Ready/stopped/unavailable/failed status with actionable diagnostics.
- Output access and delivery outcomes for audio routing.
- Device-preference values eligible for persistence under an explicit settings policy.

### Dependencies

- **Local platform audio facilities:** JUCE adapts ASIO on Windows and CoreAudio on macOS. Installed compatible ASIO drivers are prerequisites, not supplied drivers. Missing hardware leaves audio stopped with guidance, not a silent fallback; WASAPI is only a possible future compatibility choice.
- **Audio routing:** Coordinates the lifecycle of active sound and handles device-status changes.
- **Patch editor:** Presents choices and errors and receives user intent.
- **Project system/persistence layer, conditional:** Supplies and saves preferences if approved; exact preference ownership and storage location remain unresolved.

**Scope limit:** Audio input, recording, MIDI, and device interoperability beyond the supported output path are not established MVP commitments.

**Future exported-plugin boundary:** The external host, not this device manager, owns device access and render scheduling. Device settings must not be required by a graph or restored from a host plugin-state blob.

**Source alignment:** PRD A5; FR-04, FR-06, FR-11; NFR-02 and NFR-03.

## 7. Visualizers

### Responsibilities

- Present supported values, routing status, and audio/device state in understandable form.
- Distinguish an unavailable observation or stopped audio state from a valid zero-valued signal.
- If approved later, provide signal-inspection views that help users relate audio behavior to DSP concepts.
- Remain observational: enabling or disabling a view must not change the intended patch behavior.
- Respect declared responsiveness/resource limits and report when an observation cannot be provided.

### Inputs

- Supported node values, meanings, and units.
- Audio-routing observations, processing state, and connection diagnostics.
- Device status and user selection of what to inspect.

### Outputs

- Labeled value/status presentations and, if approved, signal-inspection views.
- Unavailable-observation and inspection-limit feedback.
- Observation requests identifying the selected supported signal or value.

### Dependencies

- **Audio routing:** Authoritative active-audio status and supported signal observations.
- **Node system:** Parameter/port meanings and units.
- **Audio device manager:** Effective output state.
- **Patch editor:** Selection context and presentation location.

**Scope limit:** Basic status supports existing requirements; waveform, spectrum, or other advanced displays need an explicit scope decision and are not promised for a named release.

**Source alignment:** FR-03, FR-04, FR-06; NFR-03, NFR-05, NFR-06; PRD future signal-inspection discovery.

## 8. Scripting system

**Scope:** An optional future conceptual boundary, not a currently approved feature or a requirement for Releases 0.1, 0.5, 1.0, or 2.0. All core workflows must remain usable without scripts.

### Responsibilities

- If approved for delivery, compile VAL Script locally into bounded DSP-node definitions using the language design in ValScriptArchitecture.md.
- Validate types, ports, resource limits and realtime-safe operation plans outside rendering; report source/compilation/runtime faults explicitly.
- Preserve project ownership, compiled-versus-active visibility and audio stop behavior; never bypass graph validation.
- Prohibit script file access, networking, threads and dynamic allocation. Define artifact trust/version policy before loaded content can execute.

### Inputs

- Source text, explicit compile/activation intent and the closed language vocabulary.
- Node interfaces, parameter/rate metadata and graph validation context.
- Compilation, preparation and activation outcomes.

### Outputs

- Verified node artifacts for the node system and graph preparation, plus source-aware diagnostics and visible compilation/activation status.
- Optional persisted script content only if a later product decision defines its ownership and portability.
- No script hardware, file, network, thread or allocation access; host persistence of source is not script storage permission.

### Dependencies

- **Project system:** Accepted node changes, edit history, and consistent validation.
- **Node system/audio routing:** Validated node interfaces, verified plans and off-callback preparation; no parser/compiler services in rendering.
- **Patch editor:** Source editing if approved, compile intent and visible outcomes; source is never a prerequisite for visual use.
- **Persistence layer, conditional:** Retention of content only after a format/trust decision.

**Open decisions:** Language version, exact resource/precision limits, artifact persistence/trust and release placement remain subject to the language design and validation. VAL Script proposes custom DSP nodes, not AI-generated logic, cloud execution, unrestricted native compilation or automatic execution merely on opening a project. Native plugin loading/export does not expand script capabilities.

**Source alignment:** The PRD explicitly excludes a custom-code environment from the MVP; FR-13 does not commit to a scripting language.

## 9. Persistence layer

### Responsibilities

- Read and write local project content and any explicitly supported related settings/content.
- Preserve supported patch structure, parameter values, and agreed editing state across save/reopen.
- Return clear success/failure outcomes, including unavailable destinations, unreadable content, and unsupported versions.
- Provide compatibility findings and any approved migration result without silently destroying an original.
- Separate storage/format validity from node availability and routing validity; a readable file is not proof of a runnable patch.

### Inputs

- Explicit load/save requests and local destinations.
- Consistent project snapshots supplied by the project system.
- Stored content and declared format/version information.
- Approved compatibility rules and, only if later supported, module/script content or settings.

### Outputs

- Loaded project content and format/compatibility diagnostics for project validation.
- Save results that identify success or actionable failure.
- Supported-version or migration outcomes and an original-preservation path where required.
- Retrieved settings/content only within their approved ownership boundaries.

### Dependencies

- **Local storage facilities:** Read/write access and storage error information; no database or file format is chosen.
- **Project system:** Defines the consistent project snapshot and coordinates user-facing lifecycle decisions.
- **Agreed project/version contract:** Specifies what may be retained and how unsupported content is handled; details remain unresolved.

**Boundary:** Persistence never starts audio, resolves missing components by substitution, marks a failed save successful, or decides to discard unsaved edits.

**Source alignment:** FR-05, FR-10 through FR-12; NFR-01 and NFR-04.

## End-to-end functional flows

| Workflow | Functional handoff | Required outcome |
|---|---|---|
| Create and edit | Editor submits intent; project system requests node/routing validation and updates accepted state; editor displays the result. | Invalid or inactive changes are distinguishable from accepted and active behavior. |
| Start and adjust audio | Routing validates the desired patch, checks device readiness, activates supported behavior, and reports state; parameter edits follow the same validation boundaries. | The user hears the supported result or sees an actionable failure, with stop control retained. |
| Stop or lose a device | Stop intent or device failure reaches routing; routing/device manager coordinate output state; editor/visualizers show the actual result. | Stopped, failed, and running states are not conflated, and the editable project remains available. |
| Save | Project system supplies a consistent snapshot; persistence writes locally and reports the outcome; project system updates saved/unsaved status. | Only a successful save clears the corresponding unsaved state; if newer edits exist, they remain unsaved. |
| Open on either platform | Persistence reads content; project system coordinates version, node, routing, and optional module checks; device preferences are reconciled with local availability. | Missing requirements and reassignment needs are visible; loading is not presented as successful audio activation. |
| Inspect behavior | Editor requests a supported observation; routing/node/device status informs the visualizer. | Inspection explains the state without silently changing sound or requiring a network service. |

## Future plugin capability boundaries

These are architectural requirements from the revision, not a tenth MVP subsystem or implemented features.

| Capability | Responsibilities, inputs and outputs | Dependencies and unresolved decisions |
|---|---|---|
| VST3 hosting | Accept explicit discovery/instance requests and plugin descriptors/state; expose validated processor-node metadata and prepared instances, visible scan/load failures, and editor lifecycle/status. Keep discovery/loading and state capture off rendering. | Node system, routing, project/persistence and a JUCE hosting adapter. Decide scan/runtime isolation, binary trust, missing-plugin preservation, automation/latency/tail semantics and support matrix. Arbitrary native plugins are not sandboxed by VAL Script rules. |
| Plugin export | Accept an eligible validated patch, selected buses/exposed parameters, resources and build/package intent; produce a native plugin artifact or explicit eligibility/build/distribution diagnostics through a future export adapter. | Device-independent routing engine, node validation, versioned state and JUCE/CMake packaging. Export workflow/formats, resource redistribution, identifiers/toolchain UX and supported graphs remain open. No automatic third-party bundling or code-generation commitment. |
| External-host rendering | Accept host-provided buffers, rate/layout, parameter/timing context and state; return initialized audio, bounded status and declared latency/tails without requiring a window or physical device. | Reusable DSP engine plus future JUCE processor wrapper, separate from the standalone device manager. Reconcile host lifecycle/transport and parameter precision before support claims. |

Future hosted-node dependencies must be identified and preserved when unavailable, not silently replaced or erased. Project portability covers available compatible content, not universal availability of third-party native binaries. Hosting and export have no named release assignment; reusable modules remain a separate FR-13 proposal.

## Cross-subsystem rules and unresolved policies

- **Single meaning for edits:** Node and connection rules apply equally to user edits, loaded projects, undo/redo, future modules, and any approved scripts.
- **Visible failure:** Validation, load/save, activation, and device failures must reach user-facing feedback. No subsystem may conceal failure behind a success-shaped result.
- **Independent core:** Saving, help, definitions, examples, patching, and sound use cannot depend on account authentication, cloud services, or AI. Optional future boundaries cannot become mandatory.
- **Portable intent:** Preserve supported patch meaning across Windows and macOS; treat unavailable hardware settings as reassignment needs, not grounds for losing the patch.
- **Observability without authority drift:** Editors and visualizers present accepted/active state; they do not become competing sources of patch truth.
- **Recovery and safety policy:** Live-edit failure handling, automatic restart behavior, default output levels, and recovery after interruption require explicit design. This architecture promises neither uninterrupted audio nor guaranteed hearing protection.
- **Scope discipline:** The core project/editor/node/routing/device/persistence path supports the PRD's proposed 0.1/0.5 loop. The proposed 1.0 strengthens compatibility and reliability; richer module reuse remains proposed 2.0 discovery. Advanced visualizers and scripting have no approved release placement.
- **Future compatibility without scope expansion:** Keep device/UI ownership outside the render contract for future VST3 hosting and plugin export. Those user-facing features remain outside the MVP and unassigned to Releases 0.1, 0.5, 1.0 and 2.0.

This document establishes functional responsibilities and handoffs only. Implementation design and measurable quality budgets remain separate work.
