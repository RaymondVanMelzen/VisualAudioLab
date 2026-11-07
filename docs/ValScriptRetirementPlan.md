# VAL Script retirement and migration report

**Status:** VAL Script execution, authoring UI, catalog integration, examples
and build/test targets have been removed. The `.valab` schema was not changed;
legacy VAL Script fields are retained as opaque data and rejected explicitly
by the runtime.

## Rationale and decision

VAL Script was intended to let non-C++ users, particularly people familiar
with Turbo Pascal, author DSP modules. Feedback and evaluation showed that
custom DSP development is better served by a dedicated external FreePascal
environment that already handles audio devices, PortAudio, streams and sample
handling. Maintaining an in-product language added compiler/runtime,
architecture, documentation, UI, serialization and debugging costs without a
compelling advantage.

Visual Audio Lab therefore focuses on routing, built-in audio processing,
visualization, analysis, module hosting and the desktop experience. Custom DSP
authoring is external; the FreePascal environment is not integrated or
replaced. No replacement scripting language or source format is introduced.
The product and roadmap record this boundary in
[README.md](../README.md) and [Roadmap.md](Roadmap.md).

## Affected files and behavior

| Area | Affected paths and outcome |
|---|---|
| Compiler and runtime | Removed `src/VisualAudioLab.Script/CMakeLists.txt`, `include/val/script/Compiler.h`, `include/val/script/Program.h`, `src/Compiler.cpp` and `src/Program.cpp`; removed `src/VisualAudioLab.Audio/include/val/audio/ScriptNode.h` and `src/VisualAudioLab.Audio/src/ScriptNode.cpp`. Script execution and fault monitoring were removed from `AudioNode`, `AudioGraph`, `AudioEngine` and `RuntimePanel`. |
| Build and tests | Removed Script target wiring from `src/CMakeLists.txt`, `src/VisualAudioLab.Audio/CMakeLists.txt` and `tests/CMakeLists.txt`; `modules/CMakeLists.txt` no longer describes scripting targets. Removed `tests/ScriptTests.cpp`. Legacy rejection and persistence coverage is in `tests/DspTests.cpp` and `tests/ProjectTests.cpp`. |
| UI and module catalog | Removed the Script editor/source insertion path, the `Properties / Script` tab, Script-specific command descriptions and runtime fault UI from `src/VisualAudioLab.Presentation/src/MainComponent.cpp`, `ModulePanels.cpp` and `RuntimePanel.cpp`. `ModuleCatalogItems.h` now contains 21 non-Script modules in five categories (`ModuleCatalogItems.h:24-49`); the integration test checks the catalog count and inserts every entry (`tests/ModulePanelIntegrationTests.cpp:230-248`). The only Script-specific UI is an unsupported-feature notice for an existing legacy node (`ModulePanels.cpp:908`). |
| Scalar control mappings | `audio.control.<parameter>.source: "script"` is no longer an executable mapping source. It is retained in project data and rejected explicitly during runtime graph construction (`src/VisualAudioLab.Audio/src/AudioGraph.cpp:248-250`). |
| Examples | Removed `examples/scripts/Constant.vals`, `Gain.vals`, `ShapedAverage.vals` and `StereoGate.vals`. The runtime integration test now uses built-in modules. |
| Documentation | Updated `README.md`, `docs/AudioEngine.md`, `docs/DesktopWorkflow.md`, `docs/DspArchitecture.md`, `docs/DspModules.md`, `docs/FunctionalArchitecture.md`, `docs/PRD.md`, `docs/ProductVision.md`, `docs/ProjectFormat.md`, `docs/Roadmap.md`, `docs/RoutingModelRedesign.md`, `docs/TechnicalArchitecture.md`, `docs/TypographyAndFontSystem.md`, `docs/UiPrototypePlan.md`, `docs/UxBlueprint.md`, `docs/UxReview.md`, `docs/VisualDesignLanguage.md`, `docs/VisualizationArchitecture.md`, `docs/SignalGeneratorUxRedesign.md`, `docs/modules/SignalGeneratorToolkit.md`, and `docs/plans/00-UiReviewAndExecutionPlan.md`, `03-OutputOnlyProcessingPlan.md`, `04-ModuleCategoriesAndInsertionPlan.md`, `05-GuitarTunerModulePlan.md`, `07-TypographyAndRoutingImplementation.md` and `08-HardwareEndpointLanesAndAudioSetupPlan.md`. These now describe retirement, legacy-data behavior or remove proposed Script surfaces. |
| Shell notice and settings | Updated `resources/ShellNotice.txt` and clarified the settings-store boundary in `src/VisualAudioLab.Infrastructure/include/val/infrastructure/LocalSettingsStore.h`; application preferences do not store project content. |

## Legacy project compatibility and migration impact

The `.valab` schema remains **1.1**. The project codec decodes every scalar
entry in a parameter object and encodes each stored scalar generically; it does
not filter on module type (`src/VisualAudioLab.Infrastructure/src/LocalProjectStore.cpp:323-335, 487-500`).
It does not interpret, compile, execute, strip or rewrite VAL Script fields. A
legacy node's `audio.type: "Script"`,
`audio.script`, scalar mapping fields and values, node identity, name,
geometry, other parameters and graph connections remain in the project on
load/save. The format contract documents these fields as opaque compatibility
data (`docs/ProjectFormat.md:118-126`).

The regression test saves and reloads a project containing legacy Script
source, a parameter, a `source: "script"` mapping, mapping values, multiple
patches and a connection, then verifies that the values and wire survive
(`tests/ProjectTests.cpp:64-75, 105-116`). No schema migration or automatic
translation is performed.

The presentation integration test also verifies that selecting a retained
legacy node shows an unsupported notice while preserving its source and
unrelated parameters (`tests/ModulePanelIntegrationTests.cpp:402-412`).

**Execution is intentionally unsupported.** `AudioGraph::fromStructural`
rejects an `audio.type: "Script"` node with a retirement diagnostic
(`src/VisualAudioLab.Audio/src/AudioGraph.cpp:92-93`) and rejects a Script
scalar mapping with a separate diagnostic (`AudioGraph.cpp:248-250`). The
module inspector also identifies a retained Script node as unsupported
(`src/VisualAudioLab.Presentation/src/ModulePanels.cpp:908`). A project with
either feature can be opened and saved without losing those fields, but its
graph cannot run until the user replaces/removes the unsupported module or
mapping. Projects without those legacy features continue to use supported
modules. There is no automatic conversion to the external FreePascal
environment; users must reimplement custom DSP there and make any project
changes explicitly.

## Archived assets

The former design documents are retained, clearly labeled as historical
material, under `docs/archive/retired-val-script/`:

- `ValScript.md` — the former language/runtime and editor contract.
- `ValScriptArchitecture.md` — the former language and compiler architecture.
- `README.md` — archive scope and links to this report.

These are not current requirements, tutorials or supported product manuals.
The four `.vals` source examples and Script test suite were removed rather
than presented as runnable assets.

## Cleanup checklist

- [x] Remove the VAL Script compiler, intermediate representation/runtime,
  audio node and execution/fault-monitoring paths.
- [x] Remove the editor, active catalog category/item, insertion defaults,
  menu/command copy and Script-only test target.
- [x] Remove `.vals` examples and replace the Script-dependent integration
  route with built-in modules.
- [x] Preserve legacy serialized fields losslessly; add round-trip and runtime
  rejection tests instead of deleting or silently disabling user data.
- [x] Archive the language and architecture documents with explicit historical
  notices.
- [x] Update active product, UX, architecture, project-format and roadmap
  documentation; identify any remaining historical references as such.
- [x] Do not add a replacement scripting language or integrate an external
  custom-DSP environment.
- [ ] Record final Windows Release build, test-suite and release-bundle
  verification after validation is complete.

## Verification and residual limits

The Windows Release configuration built successfully with the Visual Studio
2026 toolchain and pinned JUCE 8.0.12. All 18 non-device CTest tests passed,
including project persistence, DSP retirement diagnostics and presentation
integration. `Audio.NativeDiscovery` was deliberately excluded to avoid
probing physical devices. Exact PR-source build and distributable verification
are recorded in the delivery handoff. Legacy Script projects remain readable
and losslessly saveable, but are not executable; that compatibility boundary
is the principal migration limitation. This retirement does not qualify audio
hardware, acoustic performance or macOS behavior.
