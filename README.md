# Visual Audio Lab

C++20 / JUCE / CMake application framework for Windows and macOS. Product and design intent is in [docs](docs). This is not Release 0.1 or the full UI prototype: it contains a dark desktop shell, a transient dockable workspace, a structural node editor and tested framework boundaries. No audio devices, audio processing, DSP nodes, scripting, project files, plugin hosting or plugin export are implemented.

## Structure and dependencies

| Location / target | Responsibility and dependencies |
|---|---|
| `src/VisualAudioLab.Domain` | Standard C++20 workspace and structural graph values with stable node/port/connection identities; no framework dependency. |
| `src/VisualAudioLab.Application` | Service contracts, atomic structural graph/workspace commands, shared bounded undo/redo and validated settings lifecycle; Domain only. |
| `src/VisualAudioLab.Infrastructure` | Local rotating text diagnostics and versioned JSON settings adapters; Application plus private JUCE core. |
| `src/VisualAudioLab.Presentation` | Dark JUCE shell, command/menu/toolbar/status framework, transient docking model, spatially indexed canvas and lightweight node/cable views; Application and JUCE GUI. |
| `src/VisualAudioLab.Desktop` | Native app entry point and explicit ownership/composition. |
| `modules` | Reserved first-party module integration point; deliberately no DSP or empty processing targets. |
| `resources` | Embedded shell limitation notice. |
| `tests` | Application/history/graph, local-adapter, dock-layout and shell/canvas tests registered with CTest; opt-in large-graph benchmark. |
| `cmake` | Target-scoped C++20/warnings and dependency policy. |
| `docs` | Existing product/design specifications, unchanged by this skeleton. |

No Audio or plugin target is scaffolded before it has a real responsibility. Future processing must remain independent of the desktop/device owner, per the architecture. Production layers do not depend on tests; Presentation does not include concrete persistence/logging adapters.

All classes, including implementation/test helpers, have Doxygen descriptions in source. Public interfaces document ownership and failure semantics. The service layer is single-control-thread unless stated otherwise. The file logger serializes ordinary writes; it is not realtime-safe. Commands own no services: their target must outlive document history, and failed/throwing execute/undo operations must leave it unchanged. Histories default to 128 retained entries; this is a skeleton memory limit, not a product patch limit.

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

The component-scoped dark theme surrounds a menu bar, command-bound toolbar, capability banner, status bar and four fixed dock destinations (left, centre, right and bottom). Patch Canvas hosts the structural editor; Module Browser, Project Explorer, Property Inspector and Audio Settings remain placeholders. Panels in a shared dock are selectable tabs. Drag a panel's heading into a visible dock, or use its **Dock** selector to move it to any destination, including a collapsed dock. Peripheral docks collapse when empty; the empty centre remains a drop target. There are no floating windows, third-party docking packages or persisted layouts.

Drag the dividers to resize occupied docks within bounded proportions; focused dividers also accept arrow keys. Use the panel selector to hide a secondary panel and the **View** menu to reopen/focus any named area. Patch Canvas cannot be hidden. **Reset Layout** restores every panel and the default proportions. The toolbar and **Audio** menu also reach Audio Settings; its disabled controls explicitly disclose that devices and sample rates are unavailable. File and audio product operations are disabled, not simulated. This is the application-shell framework requested here, not the broader functional fixture described in the prototype study documents.

Renaming the existing temporary workspace and editing its graph use the same service/command/history boundary; Undo and Redo menu items/buttons operate on this shared history while focused text fields retain native editing history. Panel moves, selection and viewport navigation never enter document history. There is no project workflow or audio engine.

## Structural node editor

The editor starts empty. **Add Node**, **N** while the canvas is focused, or its context menu creates a generic structural node with an **In** port and **Out** port. These are editing primitives, not functional audio modules. No node creation, connection, selection or viewport action opens a device or runs DSP. All content is transient and discarded when the application closes.

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

The composition root stores **machine-local application preferences only**, not projects, in `VisualAudioLab/settings.json` below JUCE's user application-data directory (`%APPDATA%` on Windows and `~/Library` on macOS). Schema version 1 records window size and inspector visibility (now the Property Inspector); all other docking state resets on restart. Loads validate JSON types/ranges/version; failed loads preserve the current defaults and disable automatic writes, with a visible status message. Saves validate and use a sibling temporary replacement file. This is a single-process settings adapter, not concurrent editing or a power-loss durability guarantee. It differs from the earlier strictly in-memory prototype study by providing the settings framework explicitly requested for this skeleton; project saving remains unavailable.

Local diagnostics are in the adjacent `Logs/shell.log`. Epoch-millisecond timestamps, severity and category are recorded; no project content, paths or names are logged by default. Current and previous files are capped at approximately 1 MiB each, with per-record truncation and control-character escaping. There is no telemetry or remote export. Write failures are surfaced separately, not recursively logged. Shutdown saves only window preferences; failures are visible and do not claim success.

## Validation and distribution limits

CTest exercises history failure/branch/capacity semantics, structural graph edit/undo round trips and validation, canvas gestures/geometry/selection/cancellation, settings validation/error preservation, local-file round trips, schema/type rejection, blocked destinations, log rotation/serialization, docking-model move/visibility/reset invariants, JUCE panel ownership/navigation, disabled audio controls, menu/command registration, supported-size layout bounds and shell rendering. Windows/macOS builds and manual accessibility checks must be qualified on their actual platforms; successful Windows checks do not certify macOS.

JUCE's license, its bundled dependencies and any later SDKs must be reviewed before distribution. The project license remains a product decision; this skeleton does not choose one or assert that all dependencies are permissively licensed. No ASIO SDK/device or plugin modules are linked. The primary future standalone audio paths remain Windows/ASIO and macOS/CoreAudio, not enabled by this structure.
