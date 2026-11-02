# UI Review and Execution Plan

**Status:** planning package only. Nothing in this document authorizes implementation.
The user must choose when to execute a plan; the recommended first step is the
visual-guidelines review in `01-VisualDesignGuidelines.md`.

## Purpose and evidence limits

This package turns the UI review into five finite, separately executable plans.
It is based on the `origin/main` source at `da54aaf` (PR #16 merged), the
repository documentation named below, previously captured test screenshots,
and vendor product-reference pages. It is not a live usability study, a
hands-on review of commercial applications, a physical audio-device test, or a
claim of parity with any vendor.

The reviewed screenshots are existing session artifacts, not new captures:

| Evidence | What it can support | Limit |
|---|---|---|
| `files\phase-one-before\desktop-integration-device-settings.png` (file timestamp 2026-10-31 20:54) | Earlier injected-device setup state. | Not a measurement of physical hardware or current live UI. |
| `files\phase-one-after\desktop-integration-device-settings.png` (2026-10-31 21:23) | Updated injected-device setup and labeled choices. | Test fixture, not a MiniFuse or other live-device qualification. |
| `files\phase-one-after\desktop-channel-counts-0-ready.png` (2026-10-31 21:23) | A capability-ready test state. | Does not establish driver latency, reliability, or real-device operation. |
| `files\phase-one-after\desktop-review-workspace.png` (2026-10-31 21:23) | The application shell and relative visual weight of canvas, browser, inspector and setup. | Dated screenshot, not a fresh inspection of a running app. |

Those timestamps are the session files' recorded modification times. The
screenshots were previously reviewed as injected-device/test evidence. Do not
describe them as physical hardware results.

Official reference pages consulted:

* Arturia, [Pigments](https://www.arturia.com/products/software-instruments/pigments/overview):
  the vendor describes its responsive, color-coded interface, visual feedback
  and drag-and-drop modulation. This supports examining visual hierarchy and
  immediate interaction feedback; it does not verify how the product feels in
  hands-on use.
* AAS, [Multiphonics CV-3 modules](https://www.applied-acoustics.com/multiphonics-cv-3/modules/):
  the vendor presents a modular catalog grouped into domains such as audio
  generators, filters, mixers, signal processors and visualizers. This is a
  useful reference for discoverable musician-oriented taxonomy, not a required
  category list or a claim that VAL has those modules.
* AAS, [Strum GS-2](https://www.applied-acoustics.com/strum-gs-2/):
  the vendor frames the product around guitar playing, voicing and sound
  shaping. It is a musician-oriented product reference, **not a tuner or a
  direct tuner-UI comparator**.
* Arturia, [MiniFreak](https://www.arturia.com/products/hardware-synths/minifreak/overview):
  a hardware-instrument reference for focused physical controls, not evidence
  about desktop software UI.

Use these only as broad quality and interaction references. Do not reproduce
proprietary artwork, exact layouts, typography, curves, icons or signature
visual treatments.

## Evidence-based current assessment

The existing design direction already calls VAL a visual audio laboratory and
prioritizes signal, route and processor meaning in
`docs\VisualDesignLanguage.md`. It also describes the current shell as a
developer-style arrangement of menu/toolbar/status chrome, browser, canvas,
inspector and setup area. The existing plan's proposal for adaptive docking
and some layout examples now conflict with the user's newer requirement:
**remove docking entirely**, keep the patch canvas present, and use small
collapse/reopen icons for eligible panels.

The actual shell implements four tabbed docks, draggable panel headings,
docking selectors, resizable dividers and project-layout restoration
(`src\VisualAudioLab.Presentation\src\DockWorkspace.cpp`,
`include\val\presentation\DockWorkspace.h` and `include\val\presentation\DockLayout.h`).
`MainComponent.cpp` exposes Audio Setup from Audio, View and the toolbar.
The Module Catalog is a searchable 14-entry list; Input and Output are marked
routing utilities. Double-click, Enter and the Insert button insert at the
canvas center; there is no catalog-to-canvas module drag path in the inspected
catalog implementation (`src\VisualAudioLab.Presentation\src\ModulePanels.cpp`,
`MainComponent.cpp`). These are concrete interaction gaps, not judgments about
users' abilities.

The current audio path already accepts zero device inputs. This does **not**
mean every graph can generate sound: an empty graph or disconnected Output
renders silence; a nonempty graph needs one Output and every node must feed it.
The graph has no mixer and rejects multiple upstream sources per input.
`GeneratorNode` is an extension point, not a shipped oscillator. A zero-input
VAL Script can generate a constant in the existing documented example, but it
is not the requested fixed-frequency oscillator. See
`docs\AudioEngine.md`, `docs\DspModules.md`, `docs\DesktopWorkflow.md`,
`src\VisualAudioLab.Audio\src\AudioGraph.cpp` and
`tests\AudioEngineTests.cpp`.

The shell already composes a bounded signal-capture and worker-analysis
pipeline for scope, spectrum and meters (`docs\SignalVisualization.md`).
That provides a concrete reuse point for the guitar tuner; the existing
Spectrum Analyzer's dominant bin is not a reliable fundamental-pitch detector.

**Quality assessment:** the repository has a coherent signal-first concept and
real DSP/visualization behavior, but the reviewed shell still exposes
administrative dock chrome and a uniform catalog rather than a concise,
musician-oriented instrument workflow. The evidence supports a focused
hierarchy, navigation and feedback pass—not a claim that the product is
commercially inferior or that a full rewrite is needed. The vendor pages
provide inspiration only; no verified side-by-side product comparison was
performed.

## Coverage matrix

| User request | Plan | Required outcome |
|---|---|---|
| Audio Setup is a dialog, not a main-view panel. | `02-ShellAndAudioSetupPlan.md` | One Audio-menu entry opens a dedicated setup dialog; no permanent setup panel. |
| No input device is required to process audio when a generator supplies signal. | `03-OutputOnlyProcessingPlan.md` | Output-only device configuration plus a runnable generated-signal graph; no hidden input requirement or autoplay. |
| A tuner must tune a guitar. | `05-GuitarTunerModulePlan.md` | A usable chromatic guitar tuner, not just a generic pitch plot. |
| Design useful module categories. | `04-ModuleCategoriesAndInsertionPlan.md` | Musician-oriented categories mapped truthfully to shipped modules, with proposals clearly marked. |
| Remove docking; replace dropdowns with collapse icons; canvas cannot collapse. | `02-ShellAndAudioSetupPlan.md` | No drag-to-dock, dock selectors, tabs-as-docks or floating/re-docking behavior; fixed shell with eligible panels collapsible. |
| Insert by double-click or drag to canvas. | `04-ModuleCategoriesAndInsertionPlan.md` | Preserve accessible double-click/keyboard insertion and add explicit drop placement without silent auto-wiring. |
| Audio Setup only in Audio menu, not toolbar or View menu. | `02-ShellAndAudioSetupPlan.md` | Remove redundant toolbar and View commands; keep the Audio-menu entry. |
| Re-evaluate against commercial offerings; create guidelines before UI changes. | `00` and `01-VisualDesignGuidelines.md` | Evidence-limited quality review and a guideline approval gate before implementation. |
| Make plans only; do not code. | All six | No application-code changes or execution of the plans in this assignment. |

## Recommended execution order and dependencies

```text
01 Visual guidelines
  ├─ 02 Shell and Audio Setup
  ├─ 03 Output-only processing
  ├─ 04 Module categories and insertion
  └─ 05 Guitar tuner (depends on 03 and 04; reuses existing visualization)
```

1. **`01-VisualDesignGuidelines.md` — review and reconcile the design rules.**
   It extends `docs\VisualDesignLanguage.md`, resolves conflicts with fixed,
   non-docking panels and a permanently present canvas, and establishes the
   measurable visual acceptance bar. Do not start UI implementation before
   this plan is reviewed and the guidelines are ready.
2. **`02-ShellAndAudioSetupPlan.md` — simplify shell and device access.**
   Can begin after the guideline gate; it changes shell behavior, not graph
   processing.
3. **`03-OutputOnlyProcessingPlan.md` — make generated-source processing
   explicit.** Can proceed independently after the guideline gate, but should
   precede the tuner so the tuner plan can rely on agreed output-only and
   source semantics.
4. **`04-ModuleCategoriesAndInsertionPlan.md` — improve discovery and
   placement.** Can proceed after guidelines. Its catalog taxonomy and source
   semantics should be coordinated with plan 03.
5. **`05-GuitarTunerModulePlan.md` — specify a musician-ready tuner.**
   Depends on the approved guideline, the generated/input signal-source rules
   in plan 03, and the module/analysis placement conventions in plan 04.

The ordering allows plans 02–04 to be chosen separately after the guideline
gate. It does not imply implementation of any planned feature. Each child
document contains its own objective, confirmed requirements, investigated
surfaces, dependencies, ordered steps, non-goals, acceptance checks and risks
or unresolved decisions.

## Open decisions to preserve for execution

The user has already decided the product direction stated in the matrix; do not
re-ask those items. Keep only implementation choices that genuinely remain
open: dialog modality and persistence location; which non-canvas panels are
collapsible by default; tuner algorithm/target qualification; reference-pitch
range and tuning presets; whether an empty patch remains valid silence; and
how generator and mixer proposals enter the catalog without presenting
unshipped features as available. Where a plan recommends a default, label it a
proposal and request product review at that plan's gate.

## Scope and handoff

This package is documentation only. Do not change application code, update
`docs\DesktopWorkflow.md` or other living product documentation as if the
design were implemented, run builds, alter the persistent review build,
open a pull request, or commence plan execution. The five execution plans are
the future work breakdown; an explicit later instruction is required to
execute one.
