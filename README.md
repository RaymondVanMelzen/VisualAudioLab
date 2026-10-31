# Visual Audio Lab

C++20 / JUCE / CMake application framework for Windows and macOS. Product and design intent is in [docs](docs). This is not Release 0.1 or the full UI prototype: it contains a dark desktop shell, a dockable workspace, a structural node editor, versioned `.valab` project persistence, native audio abstractions, a realtime engine with seven built-in DSP processors, calibrated signal visualizers and bounded VAL Script audio kernels. Plugin hosting/export and a script editor UI are not implemented; audio is API-driven, with no automatic playback from shell controls.

## Structure and dependencies

| Location / target | Responsibility and dependencies |
|---|---|
| `src/VisualAudioLab.Domain` | Standard C++20 workspace and structural graph values with stable node/port/connection identities; no framework dependency. |
| `src/VisualAudioLab.Application` | Project lifecycle/store contracts, multi-patch graph/workspace commands, shared bounded undo/redo and validated settings lifecycle; Domain only. |
| `src/VisualAudioLab.Script` | Standard C++20 lexer, parser, AST, semantic analyzer, compiler and immutable typed IR executor; no JUCE/host/device dependency. |
| `src/VisualAudioLab.Audio` | Device-independent prepared graphs, DSP/script nodes, bounded automation, lock-free capture/render handoff, shared analysis and telemetry; Application/Domain, Script and standard C++20 only. |
| `src/VisualAudioLab.Infrastructure` | Versioned JSON project/settings adapters, local diagnostics and native audio discovery; Application plus private JUCE core/device modules. |
| `src/VisualAudioLab.Presentation` | Dark JUCE shell, command/menu/toolbar/status framework, project-backed docking, spatially indexed canvas, lightweight node/cable views and shared visualization widgets; Application, device-independent Audio and JUCE GUI. |
| `src/VisualAudioLab.Desktop` | Native app entry point and explicit ownership/composition. |
| `modules` | Reserved first-party module integration point; deliberately no DSP or empty processing targets. |
| `resources` | Embedded shell limitation notice. |
| `tests` | Application/history/graph, persistence, audio discovery, local-adapter, dock-layout and shell/canvas tests registered with CTest; opt-in large-graph benchmark. |
| `cmake` | Target-scoped C++20/warnings and dependency policy. |
| `docs` | Product/design specifications and the implemented [project schema/workflows](docs/ProjectFormat.md). |

The Audio target owns actual processing without device/UI dependencies. No plugin target is scaffolded. Production layers do not depend on tests; Presentation does not include concrete persistence/logging/device adapters.

All classes, including implementation/test helpers, have Doxygen descriptions in source. Public interfaces document ownership and failure semantics. The service layer is single-control-thread unless stated otherwise. The file logger serializes ordinary writes; it is not realtime-safe. Commands own no services: their target must outlive document history, and failed/throwing execute/undo operations must leave it unchanged. Histories default to 128 retained entries; this is a skeleton memory limit, not a product patch limit.

## Audio backend abstraction

Application owns `IAudioBackend`; Infrastructure implements `AsioBackend`, `WasapiExclusiveBackend`, `WasapiSharedBackend` and `CoreAudioBackend` using real JUCE discovery and explicit stream lifecycle. The message-thread API provides asynchronous inventory/capability notifications, generation-scoped device tokens, channel/rate/buffer candidates, explicit availability errors and hot-plug invalidation. ASIO remains preferred on Windows and CoreAudio on macOS; neither fallback nor device selection is automatic. This library API is not yet wired to the shell's placeholder Audio Settings controls.

`VAL_ENABLE_ASIO=ON` uses the ASIO headers bundled in pinned JUCE 8.0.12; review their license and JUCE's terms before distribution. `OFF` reports ASIO disabled explicitly. Native capability probes can initialize hardware; ASIO probes may briefly start/stop its driver inside JUCE, so they are explicit and excluded from unattended tests. See [AudioBackends.md](docs/AudioBackends.md) for identity, threading, cancellation, capability limits, build configuration and qualification boundaries.

## Basic realtime engine

`AudioEngine` prepares `AudioGraph` instances off-callback, processes actual buffers, publishes newer plans through a bounded lock-free handoff and reclaims obsolete nodes on the control thread. `AudioNode` (also named `AudioProcessorNode` for compatibility) unifies Effect/Generator/Analyzer bases. Gain, EQ, Delay, Compressor, Distortion, Chorus and Reverb share deterministic smoothing, automation and latency-aware bypass. Timestamped targets take effect at exact session-frame offsets, followed by declared smoothing; bounded queues report overflow/cancellation/missed delivery. Variable blocks stay within declared capacity; invalid layouts/oversized blocks latch silence rather than allocating. Compile-time checks require callback atomics to be lock-free.

The engine exposes callback duration/deadline load and misses separately from backend-reported latency/xruns; unknown backend data stays absent and callback load is never called whole-process CPU. Generic canvas nodes map only through explicit `audio.type`, `audio.param.<name>` and `audio.bypass` fields; Gain's existing `audio.gain` remains supported. There is no automatic playback on edit/load. See [AudioEngine.md](docs/AudioEngine.md) for stream/ownership boundaries and [DspModules.md](docs/DspModules.md) for processor algorithms, parameter domains, automation ordering, bypass and offline allocation-instrumented tests.

Oscilloscope, Spectrum Analyzer, Peak Meter and raw RMS Meter share bounded lock-free capture and one off-audio-thread analysis worker. Registered runtime sources appear in the Property Inspector, pinned non-modal Signal Inspector and semantic-zoom canvas sparks; without a processing source the shell explicitly shows no data. No analysis or rendering runs on the callback. Capture copies have a measured nonzero cost, not literally "zero impact." See [SignalVisualization.md](docs/SignalVisualization.md) for calibration, lifecycle, budgets, configuration, integration and reproducible metrics.

VAL Script compiles Pascal-inspired `AUDIO` kernels into typed, forward-only IR consumed by `ScriptNode` in the same graph/parameter/lifetime machinery. It supports stateless scalar variables/constants, pure value functions, effect-checked procedures, `IF THEN ELSE` and compile-time bounded `FOR ... NEXT`. Source errors never publish a partial artifact; runtime arithmetic faults latch source-aware silence. No host capabilities, allocation operations or unbounded loops exist in the language. See [ValScript.md](docs/ValScript.md) for exact semantics, resource limits, API integration and four runnable examples. `CONTROL` kernels and native machine-code generation are not implemented.

## Build

Prerequisites: CMake 3.25+, C++20 toolchain (MSVC with Windows SDK, or Apple Clang with Xcode/macOS SDK), and **JUCE 8.0.12**. The optional fetch route pins commit `29396c22c93392d6738e021b83196283d6e4d850`; this is an engineering dependency choice, not a revised product requirement. Existing local trees are version-checked; use the pinned commit for reproducible results.

Offline dependency provisioning is the default. Point `VAL_JUCE_SOURCE_DIR` at an already provisioned JUCE source tree. No fetch, account or network is required to run the application. An initial dependency download is possible only when the developer explicitly sets `VAL_FETCH_JUCE=ON`.

Windows (Visual Studio 2022):

```powershell
cmake --preset windows -DVAL_JUCE_SOURCE_DIR="C:\dependencies\JUCE"
cmake --build --preset windows-debug
ctest --preset windows-debug
```

macOS (native architecture):

```sh
cmake --preset macos -DVAL_JUCE_SOURCE_DIR="$HOME/dependencies/JUCE"
cmake --build --preset macos-debug
ctest --preset macos-debug
```

For a different installed generator, use a separate build directory, for example `cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVAL_FETCH_JUCE=ON`, then `cmake --build build/native` and `ctest --test-dir build/native --output-on-failure`. Windows Ninja requires a Visual Studio developer environment. macOS universal binaries can be evaluated explicitly with `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`; no universal/OS support matrix is claimed yet.

The executable/bundle is generated under `src/VisualAudioLab.Desktop/VisualAudioLab.Desktop_artefacts/<configuration>` in the build tree. `BUILD_TESTING=OFF` excludes tests. The GUI construction/rendering test requires a desktop session; the application, adapter and layout-model tests do not create windows. The layout-model test has no JUCE dependency.

## Shell, settings and diagnostics

The component-scoped dark theme surrounds a menu bar, command-bound toolbar, capability banner, status bar and four fixed dock destinations (left, centre, right and bottom). Patch Canvas hosts the structural editor; Module Browser, Project Explorer, Property Inspector and Audio Settings remain placeholders. Panels in a shared dock are selectable tabs. Drag a panel's heading into a visible dock, or use its **Dock** selector to move it to any destination, including a collapsed dock. Peripheral docks collapse when empty; the empty centre remains a drop target. Projects retain window bounds, docking, visibility, splitter proportions and selected tabs. There are no floating windows or third-party docking packages.

Drag the dividers to resize occupied docks within bounded proportions; focused dividers also accept arrow keys. Use the panel selector to hide a secondary panel and the **View** menu to reopen/focus any named area. Patch Canvas cannot be hidden. **Reset Layout** restores every panel and the default proportions. The toolbar and **Audio** menu also reach Audio Settings; its disabled controls explicitly disclose that devices and sample rates are unavailable. Audio product operations remain disabled, not simulated. This is not the broader functional fixture described in the prototype study documents.

Renaming the workspace and editing its patches use the same service/command/history boundary; Undo and Redo menu items/buttons operate on this shared history while focused text fields retain native editing history. Panel moves, selection and viewport navigation never enter document history. The shell does not start the library audio engine.

## Project persistence

Use **File > Save Project**, **Save Project As...**, **Load Project...** and **Recent Files** for human-readable, versioned `.valab` JSON. Save/Load use Ctrl+S/Ctrl+O on Windows and Command+S/Command+O on macOS; Shift+Save chooses a new path. Dirty content is marked in the window title, and replacement/close offers Save/Discard/Cancel. **New Patch** adds another graph; the patch selector changes the active graph without losing undo history.

Projects retain all patches, node/connection identities and geometry, scalar parameters, window/dock layout and per-patch zoom/pan. Selection and undo history are not serialized. Compatible unknown JSON fields survive read-edit-save; unsupported schemas and invalid references never replace active content or get overwritten. Saves prepare, verify, flush and replace through a sibling temporary file. Recent Files is bounded to ten persistent local paths, labels missing entries and supports their removal. See [ProjectFormat.md](docs/ProjectFormat.md) for the exact schema, compatibility/preservation limits, failure policy and tests.

## Structural node editor

A new project starts with one empty patch. **Add Node**, **N** while the canvas is focused, or its context menu creates a generic structural node with an **In** port and **Out** port. These are editing primitives, not functional audio modules. No node creation, connection, selection or viewport action opens a device or runs DSP. Save explicitly to retain content across application runs.

| Interaction | Behavior |
|---|---|
| Select / multi-select | Click a node; Shift-click adds; Ctrl-click (Windows) / Command-click (macOS) toggles. Empty-space drag rubber-band selects intersecting node bodies; modifiers add/toggle. |
| Move | Drag a selected body to preview one shared patch-space delta; release commits all selected nodes as one command. Escape or focus loss cancels the preview. |
| Delete | Delete/Backspace or canvas Delete removes selected nodes and all incident wires; clicking a cable selects it for deletion. |
| Wire | Drag either port to an opposite-direction port. Outputs allow fan-out; inputs accept one wire. Missing endpoints, self-links, duplicates, occupied inputs and cycles are rejected with a visible status explanation. No implicit replacement or conversion. |
| Disconnect | Right-click a cable, or choose a wire from a port's context menu. Delete a selected cable; **D** disconnects the focused node's incoming wire. Each is undoable. |
| Zoom / pan | Wheel zooms around the pointer; +/- buttons or keys zoom around the viewport centre (2.5%-300%). Middle-drag or Space-drag pans; Shift-wheel pans. Fit frames the graph within these zoom limits; overview rendering suppresses unreadable node detail below 35%. |
| Keyboard navigation | Arrow keys focus/select nodes in stable creation order and reveal offscreen targets; Tab leaves the canvas. Ctrl/Command+A selects all. **M**, arrows, then Enter moves the selection as one gesture; Escape cancels. **C**, arrows, then Enter connects the starting node's output to the focused node's input; Escape cancels. |
| Undo / redo | Shell buttons/menu or Ctrl/Command+Z and Ctrl/Command+Shift+Z in the canvas. Creation, deletion, grouped movement, connection and disconnection are reversible, including exact incident wire identities on restore. |

Coordinates are finite patch units, independent of zoom/pan, bounded to +/-1,000,000 per node origin. Selection is navigation state: undo restores graph content but does not restore an earlier selection/viewport or select resurrected nodes automatically. A new accepted edit truncates redo; invalid edits, cancelled gestures and no-op moves do not. History retains the existing most recent 128 commands, not an unlimited session journal.

`NodeView` and `ConnectionView` are lightweight render/geometry classes, deliberately not one heavyweight JUCE component per node/wire. `CanvasView` owns selection/gesture/viewport state and borrows the workspace. Its scene caches cable paths, adjacency and uniform-grid indexes. Paint/hit-test visit spatial candidates; accepted edits rebuild indexes, while dragging updates only moved-node previews and incident wire geometry, invalidating affected regions. A very dense or fully zoomed-out graph still requires work proportional to visible content; no unlimited capacity or fixed frame-rate guarantee is claimed.

The focused graph tests and GUI tests cover edit/history invariants, invalid connections, selection, geometry, gestures and cancellation. An explicit, non-startup scale fixture creates **5,000 nodes and 4,950 connections** through the real service, checks culling against a brute-force oracle, renders the real canvas and reports timings:

```powershell
.\build\windows-native\tests\Release\VisualAudioLab.Presentation.Tests.exe --benchmark
```

Use the corresponding executable in your build directory/configuration; the optional benchmark is not part of ordinary application startup or CTest. Its software snapshot timings are not native GPU frame-rate measurements. Keyboard actions exist, but semantic screen-reader traversal of the graph and full platform accessibility qualification remain future work; an accessible component description alone does not provide complete graph semantics.

The composition root stores **machine-local application preferences**, separate from `.valab` projects, in `VisualAudioLab/settings.json` below JUCE's user application-data directory (`%APPDATA%` on Windows and `~/Library` on macOS). Settings schema version 1 records window size and inspector visibility (now the Property Inspector); loading a project overrides the view with its own saved layout. Loads validate JSON types/ranges/version; failed settings loads preserve defaults and disable automatic preference writes, with a visible status message. Saves validate and use a sibling temporary replacement file. This is a single-process settings adapter, not concurrent editing or a power-loss durability guarantee.

Local diagnostics are in the adjacent `Logs/shell.log`. Epoch-millisecond timestamps, severity and category are recorded; no project content, paths or names are logged by default. Current and previous files are capped at approximately 1 MiB each, with per-record truncation and control-character escaping. There is no telemetry or remote export. Write failures are surfaced separately, not recursively logged. Shutdown saves only window preferences; failures are visible and do not claim success.

## Validation and distribution limits

CTest exercises history failure/branch/capacity semantics, structural graph edit/undo round trips and validation, canvas gestures/geometry/selection/cancellation, settings validation/error preservation, local-file round trips, schema/type rejection, blocked destinations, log rotation/serialization, audio discovery/cancellation/hot-plug contracts and native enumeration, docking-model move/visibility/reset invariants, JUCE panel ownership/navigation, disabled audio controls, menu/command registration, supported-size layout bounds and shell rendering. Windows/macOS builds and manual accessibility checks must be qualified on their actual platforms; successful Windows checks do not certify macOS.

JUCE's license, its bundled dependencies (including the ASIO headers when enabled) and any later SDKs must be reviewed before distribution. The project license remains a product decision; this skeleton does not choose one or assert that all dependencies are permissively licensed. JUCE device modules support discovery and explicitly requested streams for the basic engine; no plugin modules are linked. Primary standalone preferences remain Windows/ASIO and macOS/CoreAudio, with distinct WASAPI Shared/Exclusive implementations.
