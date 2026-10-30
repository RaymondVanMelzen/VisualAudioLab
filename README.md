# Visual Audio Lab

C++20 / JUCE / CMake application skeleton for Windows and macOS. Product and design intent is in [docs](docs). This is not Release 0.1 or the full UI prototype: it contains a native shell, a transient workspace identity, and tested framework boundaries. No audio devices, audio processing, DSP nodes, scripting, project files, plugin hosting or plugin export are implemented.

## Structure and dependencies

| Location / target | Responsibility and dependencies |
|---|---|
| `src/VisualAudioLab.Domain` | Standard C++20 workspace value; no framework dependency. Header-only until implementation is needed. |
| `src/VisualAudioLab.Application` | Service contracts, workspace commands, bounded undo/redo and validated settings lifecycle; Domain only. |
| `src/VisualAudioLab.Infrastructure` | Local rotating text diagnostics and versioned JSON settings adapters; Application plus private JUCE core. |
| `src/VisualAudioLab.Presentation` | JUCE component shell, menu/command routing, placeholder regions; Application and JUCE GUI. |
| `src/VisualAudioLab.Desktop` | Native app entry point and explicit ownership/composition. |
| `modules` | Reserved first-party module integration point; deliberately no DSP or empty processing targets. |
| `resources` | Embedded shell limitation notice. |
| `tests` | Application, local-adapter and shell tests registered with CTest. |
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

The executable/bundle is generated under `src/VisualAudioLab.Desktop/VisualAudioLab.Desktop_artefacts/<configuration>` in the build tree. `BUILD_TESTING=OFF` excludes tests. The GUI construction test requires a desktop session; the two framework/adapter tests do not create windows.

## Shell, settings and diagnostics

The shell displays named, nonfunctional regions. Renaming its temporary workspace exercises the service/command/history boundary; Undo and Redo menu items/buttons operate on workspace history while focused text fields retain native editing history. No patch editor, docking framework or complete usability-study fixture is claimed.

The composition root stores **machine-local application preferences only**, not projects, in `VisualAudioLab/settings.json` below JUCE's user application-data directory (`%APPDATA%` on Windows and `~/Library` on macOS). Schema version 1 records window size and inspector visibility. Loads validate JSON types/ranges/version; failed loads preserve the current defaults and disable automatic writes, with a visible status message. Saves validate and use a sibling temporary replacement file. This is a single-process settings adapter, not concurrent editing or a power-loss durability guarantee. It differs from the earlier strictly in-memory prototype study by providing the settings framework explicitly requested for this skeleton; project saving remains unavailable.

Local diagnostics are in the adjacent `Logs/shell.log`. Epoch-millisecond timestamps, severity and category are recorded; no project content, paths or names are logged by default. Current and previous files are capped at approximately 1 MiB each, with per-record truncation and control-character escaping. There is no telemetry or remote export. Write failures are surfaced separately, not recursively logged. Shutdown saves only window preferences; failures are visible and do not claim success.

## Validation and distribution limits

CTest exercises history failure/branch/capacity semantics, settings validation/error preservation, local-file round trips, schema/type rejection, blocked destinations, log rotation/serialization and basic JUCE shell construction. Windows/macOS builds and manual accessibility checks must be qualified on their actual platforms; successful Windows checks do not certify macOS.

JUCE's license, its bundled dependencies and any later SDKs must be reviewed before distribution. The project license remains a product decision; this skeleton does not choose one or assert that all dependencies are permissively licensed. No ASIO SDK/device or plugin modules are linked. The primary future standalone audio paths remain Windows/ASIO and macOS/CoreAudio, not enabled by this structure.
