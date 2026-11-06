# Visual Audio Lab Roadmap

*Audited: 2026-11-06 | Source: GitHub `main` at `27ca4d43f38af63ba3e2536c98cc3c69a22c9888` | Canonical path: `docs/Roadmap.md` (tracked capitalization preserved).*

# Vision

Visual Audio Lab is an open-source, offline-first desktop environment for making and understanding audio through visual programming. Its central workflow is to build a patch from visible components, follow the signal through it, hear or inspect the result, and learn by changing the system directly. It should welcome hobby musicians, sound designers, returning programmers, and DSP students without making conventional programming a prerequisite or removing technical depth.

The product is intended for Windows and macOS and must remain useful without a subscription, cloud service, or AI service. The aim is a focused creative and learning workbench, not an unrestricted DAW or a feature-count contest. See [ProductVision.md](ProductVision.md) and [PRD.md](PRD.md); the PRD's release names and scope are proposals, not delivery commitments.

# Current Status

## Status rubric

- **Complete** means the explicitly bounded capability is implemented and has repository test coverage. It does not imply qualification on every operating system, device, or workload.
- **Mostly Complete** means the principal end-to-end workflow is present, with meaningful limits or qualification still outstanding.
- **In Progress** means part of the capability is implemented, but a material user-facing gap, open correctness issue, or integration step remains.
- **Not Started** means no implementation of that capability is evident in the audited source; a mention in documentation or an issue is not implementation evidence.

Statuses describe today's bounded code, not a verdict on whether Visual Audio Lab as a whole is finished.

## Subsystem inventory

| Subsystem | Status | What is present and what remains |
|---|---|---|
| Audio Engine | **Mostly Complete** | A prepared, bounded audio graph; sample-time parameter changes; offline rendering; explicit stream lifecycle; and backend diagnostics are implemented. The engine is not a hard-realtime OS guarantee, and platform/device qualification remains incomplete. See [AudioEngine.md](AudioEngine.md), [AudioBackends.md](AudioBackends.md), and `AudioEngine` / `PreparedAudioGraph` in [`AudioEngine.cpp`](../src/VisualAudioLab.Audio/src/AudioEngine.cpp) and [`AudioGraph.cpp`](../src/VisualAudioLab.Audio/src/AudioGraph.cpp). |
| Module Framework | **In Progress** | Built-in processors and generators have runtime mappings, catalog entries, parameter panels, and insertion into a patch. Reusable user-authored module/group packaging, extension/plugin modules, and a broad library are not present. “Module” in the UI is a built-in audio function instance, not a packaged reusable patch. See [`ModuleCatalogItems.h`](../src/VisualAudioLab.Presentation/include/val/presentation/ModuleCatalogItems.h), [`ModulePanels.cpp`](../src/VisualAudioLab.Presentation/src/ModulePanels.cpp), and [DspModules.md](DspModules.md). |
| Node Graph | **Mostly Complete** | Patch nodes, connections, validation, undo/redo, multiple patches, and project-wide editing are implemented. Cycles/feedback are rejected; there is no general feedback-capable graph or saved automation-lane editor. See `WorkspaceService`, `GraphCommands`, [`Project.cpp`](../src/VisualAudioLab.Application/src/Project.cpp), and [FunctionalArchitecture.md](FunctionalArchitecture.md). |
| Routing System | **Mostly Complete** | Fixed hardware endpoint lanes, multiple source/destination rows, fan-out, explicit Sum, exact named channel assignment, and a visible mono-to-stereo adapter are implemented. Each destination accepts one feed; other incompatible widths are rejected. There is no implicit destination mixing, arbitrary conversion, or independent clock-domain routing. See [DesktopWorkflow.md](DesktopWorkflow.md), [ProjectFormat.md](ProjectFormat.md), and [RoutingModelRedesign.md](RoutingModelRedesign.md). |
| Visualization | **Mostly Complete** | Shared capture/analysis feeds the oscilloscope, spectrum, peak, RMS, correlation, and guitar-tuner views, including inspector preview and a persistent Signal Inspector. The larger workspace/docking, spectrogram, profile-editing, and accessibility proposals are not all implemented. See [SignalVisualization.md](SignalVisualization.md) and [VisualizationArchitecture.md](VisualizationArchitecture.md). |
| Signal Analysis | **Mostly Complete** | Bounded off-callback capture and worker analysis implement waveform, spectrum, sample peak, RMS, stereo correlation, and monophonic guitar pitch estimation. These are digital measurements with documented windows/limits, not calibrated physical measurements; tuner behavior is not qualified against all pickups, guitars, rooms, or devices. See [SignalVisualization.md](SignalVisualization.md), `AnalyzerEngine`, and `VisualizationTests` / `VisualizationPresentationTests`. |
| Project Persistence | **Mostly Complete** | `.valab` JSON schema 1.1 supports multiple patches, endpoints, connections, scalar processor/source parameters, layouts, and schema 1.0 migration. Runtime automation curves, plugin state, packaged modules, and arbitrary typed collections are not persisted. See [ProjectFormat.md](ProjectFormat.md), `LocalProjectStore`, `ProjectSession`, and `ProjectTests`. |
| VAL Script | **Mostly Complete** | Version 1 compiles to a verified bounded register program and runs stateless audio-rate source/effect kernels in the graph; source editing, examples, errors, and apply are exposed in the desktop. `CONTROL` is parsed but rejected; stateful scripts, arrays, native code generation, and external capabilities are not supported. See [ValScript.md](ValScript.md), [ValScriptArchitecture.md](ValScriptArchitecture.md), `Compiler`, `ScriptNode`, and `ScriptTests`. |
| MIDI | **Not Started** | No MIDI device I/O, event graph, routing, monitor, generator, piano roll, or instrument hosting is implemented. MIDI note numbers used by Signal Generator scale settings and frequency-entry parsing are not MIDI support. The architecture request remains [issue #32](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/32). |
| Generators | **Mostly Complete** | Seven executable catalog modules are present: Signal, Impulse, Click, Burst, Chirp, Noise, and DC generators. Signal Generator already supplies selectable test waveforms (including sine), noise/DC, and sweeps; that does not make the separately requested Oscillator or Sampler modules available. See [SignalGeneratorToolkit.md](modules/SignalGeneratorToolkit.md), `GeneratorNodes`, and [ModuleCatalogItems.h](../src/VisualAudioLab.Presentation/include/val/presentation/ModuleCatalogItems.h). |
| Effects | **In Progress** | Gain, EQ, Delay, Compressor, Distortion, Chorus, and Reverb are implemented and exposed. A current open report says Distortion's drive behavior does not meet the intended saturation/loudness result ([issue #29](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/29)); it must be investigated rather than assumed resolved. Flanger is not Chorus, and Compressor is not the separately requested Time Compressor. See [DspModules.md](DspModules.md), `DspNodes`, and `DspTests`. |
| Diagnostics | **In Progress** | Engine/backend telemetry, script fault records, compiler errors, generator readouts, and truthful visualization states exist. A sample-level Data Probe and Diagnostic Console are not catalog/runtime features; see [issue #33](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/33). See [AudioEngine.md](AudioEngine.md), [ValScript.md](ValScript.md), and [SignalVisualization.md](SignalVisualization.md). |
| User Interface | **Mostly Complete** | The desktop exposes a searchable/category-filtered module browser, patch canvas, properties/script editor, signal preview, project actions, explicit transport, and Audio Setup. The current shell has fixed regions rather than the proposed general-purpose dock/workspace system. This is an intentional UI improvement descision. Command shortcuts are fixed rather than user-configurable; see [issue #34](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/34) and [issue #30](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/30). See [DesktopWorkflow.md](DesktopWorkflow.md) and `MainComponent`. |
| Settings / Preferences | **In Progress** | Versioned machine-local settings retain window and audio-setup choices, with explicit load/save failure handling. A categorized Preferences dialog, shortcut editor, and preference import/export are not implemented; see [issue #36](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/36). See `SettingsService`, `LocalSettingsStore`, and `Main.cpp`. |
| Documentation | **In Progress** | The repository has substantial product, architecture, workflow, DSP, format, and test documentation. Some plans are still labeled as execution/feature plans although their described work has shipped, and some acceptance language remains future tense. This roadmap uses current code and implementation companions to distinguish delivery from proposal; it does not treat every old plan as abandoned. |
| Platform / Hardware Qualification | **In Progress** | Windows (ASIO, WASAPI Shared/Exclusive, DirectSound) and macOS (CoreAudio) are represented in the backend/UI implementation. Native discovery and an opt-in WASAPI shared silence check exist, but repository tests do not establish physical round-trip latency, universal driver reliability, all-device behavior, macOS hardware qualification, or hard-realtime guarantees. See [AudioPlatformStrategy.md](AudioPlatformStrategy.md), [AudioBackends.md](AudioBackends.md), [DesktopWorkflow.md](DesktopWorkflow.md), and `AudioBackendTests`. |

## Implemented inventory

- **Executable graph and desktop workflow:** audio nodes are explicitly mapped from persisted `audio.type` values; the browser's shipped entries are wired through insertion and editor panels rather than inferred from class names alone. The six analysis choices map to an Analyzer node plus a saved `visualization.kind`. See `AudioGraph::fromStructural` in [`AudioGraph.cpp`](../src/VisualAudioLab.Audio/src/AudioGraph.cpp), [`MainComponent.cpp`](../src/VisualAudioLab.Presentation/src/MainComponent.cpp), and [ModulePanelIntegrationTests.cpp](../tests/ModulePanelIntegrationTests.cpp).
- **DSP and sources:** seven effects and seven generators listed above have concrete runtime classes and deterministic DSP/generator coverage in [DspTests.cpp](../tests/DspTests.cpp). The explicit `Sum` utility is routing, not a Mixer.
- **Observation:** capture is bounded and analyzed off the audio callback; analysis products share a worker and feed UI consumers. Tests are in [VisualizationTests.cpp](../tests/VisualizationTests.cpp) and [VisualizationPresentationTests.cpp](../tests/VisualizationPresentationTests.cpp).
- **Projects and settings:** the local project adapter and settings adapter are exercised by [ProjectTests.cpp](../tests/ProjectTests.cpp), [InfrastructureTests.cpp](../tests/InfrastructureTests.cpp), and [RuntimeIntegrationTests.cpp](../tests/RuntimeIntegrationTests.cpp).
- **Scripts:** the compiler/runtime, four sample scripts, fault handling, and desktop editor are represented in [ScriptTests.cpp](../tests/ScriptTests.cpp), `examples/scripts`, and `ModulePanels.cpp`.

These are source/test references, not claims that this audit ran the test suite.

## Partially implemented inventory

- **Reusable modules:** built-in module definitions, runtime mappings, insertion, and per-instance parameters exist; reusable patch groups, user module packaging, and third-party module/plugin extension do not.
- **Graph and routing:** the supported directed audio graph and explicit endpoint model are implemented, but feedback, general control/MIDI signal types, arbitrary bus adapters, and automatic destination summing are not.
- **Generator versus oscillator:** Signal Generator can generate sine and other waveforms. There is no separate catalog entry/runtime module named Oscillator, nor a Sampler. Likewise, an envelope is available as a one-shot scalar parameter-control source, not as the requested audio module with external gate/retrigger ports.
- **Modulation and automation:** LFO, one-shot Envelope, and stateless VAL Script can drive supported continuous scalar parameters. This is not a general modulation graph, MIDI/control wire system, or editable/persisted automation arrangement.
- **Diagnostics:** engine metrics and script/compiler diagnostics are real; the Data Probe / Diagnostic Console request is not.
- **Preferences and commands:** settings persistence and a command target/key mapping are present; the configurable Preferences UI and user-remappable shortcut system are not.
- **Qualification:** synthetic deterministic tests and selected native discovery/silence paths exist. Those do not establish audible hardware behavior, physical latency, or full platform support.

## Planned inventory (open requests, not commitments)

GitHub's open backlog at the audited revision includes a reported Distortion defect ([#29](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/29)); UI/control simplification ([#30](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/30)); a broad module wishlist ([#35](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/35)) and its synthesizer umbrella ([#52](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/52)); Preferences ([#36](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/36)); configurable shortcuts ([#34](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/34)); Data Probe / Diagnostic Console ([#33](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/33)); and MIDI architecture ([#32](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/32)). Individual module requests include Flanger ([#27](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/27)), Pitch Shifter ([#28](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/28)), Vocoder ([#31](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/31)), and modules across [#38](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/38)–[#51](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/51), including Oscillator, Sampler, Mixer, Waveshaper, Modulator, Diffuser, Resonator, time-domain processing, and spectral processing.

An open issue is evidence of a request, not proof that its feature is missing or that the work is committed. In particular, the still-open Noise Generator request ([#40](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/40)) and Delay request ([#45](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/45)) overlap shipped catalog modules. Conversely, the separate Mixer ([#42](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/42)), Oscillator ([#38](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/38)), Sampler ([#39](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/39)), and Time Compressor ([#50](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/50)) remain distinct requests, not capabilities to infer from Sum, Signal Generator, or Compressor.

The GitHub API returned no closed issues at audit time, so no work item is marked complete on the basis of issue closure. Recheck both the implementation and issue state before taking any backlog item.

## Deprecated, superseded, or abandoned plans

No repository plan was found explicitly marked abandoned. Plans are evidence of intent and acceptance criteria, not implementation status. Several still-labeled plans are stale if read as a current status report:

- [plans/02-ShellAndAudioSetupPlan.md](plans/02-ShellAndAudioSetupPlan.md), [plans/03-OutputOnlyProcessingPlan.md](plans/03-OutputOnlyProcessingPlan.md), and [plans/04-ModuleCategoriesAndInsertionPlan.md](plans/04-ModuleCategoriesAndInsertionPlan.md) describe execution work whose main outcomes are now in `DesktopWorkflow.md`, the source, and presentation integration tests. Plan 03's explicit boundary still matters: the dedicated Oscillator module is not implied by the shipped Signal Generator.
- [plans/05-GuitarTunerModulePlan.md](plans/05-GuitarTunerModulePlan.md) remains labeled a feature plan with future-tense acceptance steps, while the tuner, catalog exposure, analysis, and presentation are implemented. The implementation and limits are recorded in [SignalVisualization.md](SignalVisualization.md); the plan itself does not establish an abandoned or incomplete implementation.
- [plans/06-PremiumPaletteAndGlowGuidelines.md](plans/06-PremiumPaletteAndGlowGuidelines.md) and [plans/08-HardwareEndpointLanesAndAudioSetupPlan.md](plans/08-HardwareEndpointLanesAndAudioSetupPlan.md) explicitly record implemented work. Plan 08 and [RoutingModelRedesign.md](RoutingModelRedesign.md) define the current fixed-endpoint boundary: one feed per destination, explicit Sum, and only specifically implemented adapters. Earlier proposal text for implicit destination summing is superseded by that boundary.
- [VisualizationArchitecture.md](VisualizationArchitecture.md) explicitly remains a proposal. Its generalized dockable workspaces, profile editing, spectrograms, and broader visualization products must not be presented as shipped; the actual subset and gaps are in [SignalVisualization.md](SignalVisualization.md).

# Current Release Focus

This is a proposed ordering based on the checked-out implementation, open GitHub issues, and architectural dependencies. It is not a release commitment or a statement that issue ordering reflects maintainer priority.

1. **Correctness of shipped behavior:** investigate and resolve the Distortion behavior reported in [#29](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/29), using DSP-level tests to verify the intended harmonic and level response. Do not treat the existence of `DistortionNode` as proof that the report is fixed.
2. **Make the existing workbench easier to operate:** assess the control feedback in [#30](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/30), and design the requested Preferences ([#36](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/36)) and configurable command/shortcut system ([#34](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/34)) around the existing command and settings services.
3. **Choose bounded module additions:** use [#52](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/52) and [#35](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/35) as request indexes, not a batch commitment. Verify every candidate against the actual 22-entry catalog and runtime mapping first; design and qualify one module end-to-end at a time.
4. **Defer architectural expansions until their contracts are explicit:** Data Probe ([#33](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/33)) must build on bounded off-callback observation; MIDI ([#32](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/32)) needs first-class event routing and persistence semantics. Neither should be approximated with existing audio or scalar-control facilities.

# Short-Term Roadmap

- Reproduce and correct the open Distortion report; retain the current DSP limitations (including no oversampling) as explicit behavior rather than masking them with a volume change.
- Close documentation drift as features change: update plan status/acceptance text when work ships, and keep source-backed distinctions between a shipped built-in, an open request, and a future architecture proposal.
- Define the accepted scope and smallest end-to-end implementation for the Preferences and keyboard-shortcut issues. Reuse existing `SettingsService`, `LocalSettingsStore`, and command dispatch; persist user choices with validation and preserve current defaults.
- Review the 22 shipped catalog entries against open module issues. Mark duplicate requests as already covered only after confirming equivalent user-visible behavior; refine issue wording rather than implementing a second copy of shipped Noise Generator or Delay.

# Mid-Term Roadmap

- Extend the built-in module library in discrete, testable slices. Candidate gaps explicitly requested include a dedicated oscillator, sampler, true Mixer, independently routed envelope/control modules, and selected effects; they depend on stable module contracts, parameter/persistence mappings, truthful UI exposure, and DSP tests.
- Deliver a Data Probe / Diagnostic Console only after specifying what sample snapshots, provenance, resource budgets, stale/gap states, and UI controls it needs. Keep all capture bounded and analysis/rendering off the audio callback.
- Improve platform and hardware qualification for the currently implemented Windows/macOS paths. Establish repeatable device/configuration coverage and report which systems, driver modes, and measurements were actually exercised; do not infer physical round-trip latency from callback or backend-reported latency.
- Evaluate usability and accessibility of the current fixed-shell module/property workflow before adopting the broader dockable workspaces described in `VisualizationArchitecture.md`.

# Long-Term Roadmap

- Design first-class MIDI sources, destinations, event ports, routing, timing, monitoring, persistence, and device lifecycle before adding MIDI modules, a piano roll, or instruments. Open [#32](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/32) is a broad architecture proposal, not a specification already satisfied by MIDI note-number parsing.
- Consider advanced spectral and time-domain processing (vocoder, pitch shifting, time stretching/compression, spectral inversion) only with explicit latency, state, parameter, channel, realtime, and test contracts.
- Explore reusable patch modules, import/export or sharing, plugins, and larger educational workflows only after user evidence and architecture decisions. These are not prerequisites for the current offline desktop workflow, nor are they committed by the product vision.

# Release Milestones

Milestones below group work by dependency and acceptance evidence. They intentionally have no version numbers or dates. The issue links identify requests; selecting or scheduling any milestone remains a maintainer decision.

## Core behavior and reliability

- **Goal:** Make the already shipped audio workbench trustworthy for everyday experimentation.
- **Included features:** Resolve the Distortion correctness report; update related DSP tests and the documented algorithm/limits.
- **Dependencies:** Reproducible DSP fixtures and a defined level/harmonic acceptance target.
- **Success criteria:** Tests distinguish increased nonlinear harmonic content from a simple level increase; the chosen output-level behavior is documented; no hardware claim exceeds the test evidence. Related request: [#29](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/29).

## Configurable workbench

- **Goal:** Let users adapt basic application preferences and commands without weakening explicit audio-control behavior.
- **Included features:** A scoped Preferences dialog and a user-configurable command/shortcut workflow; consider the module-control usability request as part of the same review.
- **Dependencies:** Existing validated settings storage and command IDs, a conflict/fallback policy for bindings, and an agreed first preference set.
- **Success criteria:** Settings survive restart, malformed settings remain recoverable, shortcut conflicts are visible, and the Start/Stop defaults remain discoverable and explicitly invoked. Related requests: [#30](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/30), [#34](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/34), [#36](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/36).

## Focused synthesis/module expansion

- **Goal:** Add a small number of genuinely new creative/synthesis capabilities through the complete runtime-to-UI path.
- **Included features:** Select from distinct gaps such as a dedicated Oscillator, Sampler, true Mixer, or audio-rate Envelope; do not count shipped Noise Generator, Delay, or the scalar one-shot envelope mapping as new work.
- **Dependencies:** A narrow module contract, graph/routing constraints, parameter and project-format mapping, desktop editor/catalog registration, and DSP/runtime tests.
- **Success criteria:** Each selected feature appears only when executable; can be inserted, wired, edited, saved/reloaded, and tested offline; its behavior and limits are documented. Request index: [#35](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/35) and [#52](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/52).

## Sample-level diagnostics

- **Goal:** Help users inspect and understand real values within a running patch.
- **Included features:** A bounded Data Probe and diagnostic view based on actual captured signal values and existing engine/script diagnostics.
- **Dependencies:** Explicit target/sample-window semantics, bounded memory and capture budgets, gap/freshness/provenance display, and accessible non-plot readings.
- **Success criteria:** Values identify their node/point, channel, window, graph revision, and freshness; overload and missing data are explicit; enabling observation does not add unbounded or UI work to the audio callback. Request: [#33](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/33).

## MIDI-capable graph

- **Goal:** Introduce MIDI as a separate, correctly timed signal domain rather than as an audio-node convention.
- **Included features:** To be scoped from MIDI device input/output, event inspection, routing, and transport/timing needs before modules or instruments are selected.
- **Dependencies:** Typed event ports, ordering/timestamp and overflow rules, device lifecycle, project persistence/versioning, and cross-platform tests.
- **Success criteria:** MIDI events can be monitored and routed with deterministic, documented timing and loss behavior; audio-only graphs keep their current behavior. Request: [#32](https://github.com/RaymondVanMelzen/VisualAudioLab/issues/32).

## Advanced DSP and interoperability

- **Goal:** Explore high-cost or high-complexity processing without compromising the bounded, understandable core.
- **Included features:** Selected spectral/time-domain modules, reusable patch packaging, and (if separately justified) plugin hosting/export.
- **Dependencies:** Algorithm and latency contracts, state migration/reset behavior, CPU/memory admission, platform qualification, and explicit licensing/format decisions where relevant.
- **Success criteria:** Every feature has reproducible quality/performance tests, documented tradeoffs, and a clear no-feature fallback; no aspirational architecture or wishlist is described as shipped.

# Evidence and Audit Limits

- The isolated worktree fetched GitHub `origin/main`; remote HEAD resolves to `main`. `HEAD`, `FETCH_HEAD`, and the fetched `origin/main` all identify `27ca4d43f38af63ba3e2536c98cc3c69a22c9888`. The tracked canonical file is `docs/Roadmap.md`; no lowercase case-only duplicate is tracked.
- Open GitHub issues were queried on 2026-11-06, including the module requests and architecture/usability issues cited above. The closed-issue query returned no items. Issues are treated as requests, not implementation evidence or approved commitments.
- The roadmap cites current implementation companions, source mappings, and test targets. This documentation-only task did not build the application or execute tests. Test names/paths establish repository coverage intent only; they do not establish a passing run in this audit.
- Native backend code, simulated/native test seams, and documented opt-in silence checks are not equivalent to qualification on physical Windows/macOS devices. Round-trip latency, all hardware combinations, driver stability, hard-realtime behavior, and platform UI details remain unverified here.

# Guiding Principles

- Prefer inspectable signal flow, learnability, reliability, and user independence over module count.
- Expose a feature only when its runtime, desktop affordance, saved representation, and failure behavior agree.
- Keep audio start/stop explicit; keep observation bounded and separate from sound generation.
- Cite implementation and tests for shipped claims; label proposals, issue requests, and qualification gaps honestly.
