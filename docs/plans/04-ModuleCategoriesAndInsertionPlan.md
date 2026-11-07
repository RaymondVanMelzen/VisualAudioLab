# Module Categories and Insertion Plan

**Status:** execution plan only. This document does not add modules or change
the current graph editor.

## Objective

Make the Module Browser understandable to musicians and support both
double-click insertion and direct drag-to-canvas placement. Preserve
keyboard/accessibility paths, explicit wiring, undo/redo and truthful module
availability.

## Confirmed requirements

- Provide useful module categories and map the existing catalog accurately.
- A user may insert a module by double-clicking it or dragging it onto the
  canvas.
- Keep the Patch Canvas visible; the shell's removal of panel docking is
  covered by `02-ShellAndAudioSetupPlan.md`.
- Do not silently auto-wire, auto-start, or represent planned modules as
  shipped.
- A **module** is a reusable audio function; a placed **node** is one
  instance. Input/Output are routing utilities, not effects.

## Historical catalog snapshot

This plan's catalog inventory is a historical pre-retirement snapshot, not a
description of the current application. At that time,
`src\VisualAudioLab.Presentation\src\ModulePanels.cpp` defined a flat
14-entry catalog including Script. VAL Script has since been retired and
removed from the current catalog; see the
[retirement report](../ValScriptRetirementPlan.md). The remaining interaction
details below are historical proposals and must be checked against current
code before implementation.

The graph's DSP mapping is in
`src\VisualAudioLab.Audio\src\AudioGraph.cpp`. The current `Analyzer` mapping
backs the four visualization entries; `docs\SignalVisualization.md` describes
the shared capture/analysis system. `docs\DspModules.md` explicitly says no
product oscillator or mixer is shipped and lists `GeneratorNode` only as an
extension point. `docs\AudioEngine.md` says there is no implicit mixer or
channel conversion.

## Proposed taxonomy

Use category metadata in one catalog source rather than separate, divergent
name lists. A practical initial map is:

| Category | Existing entries | Proposed/future entries |
|---|---|---|
| **Effects** | Gain, EQ, Delay, Compressor, Distortion, Chorus, Reverb | Keep any stereo-width variants marked only when implemented. |
| **Analysis** | Oscilloscope, Spectrum Analyzer, Peak Meter, RMS Meter | Guitar Tuner, after plan 05 is implemented and qualified. |
| **Routing** | Input, Output | Mixer, only after explicit summing/channel semantics are designed. |

These groupings are proposals, not a requirement to show empty future
categories. Do not add placeholders, disabled cards or “coming soon” modules
without separate product approval. Keep the existing module list discoverable
even if a category is collapsed. VAL Script is retired; it is not a category,
catalog entry, or future product capability.

The AAS Multiphonics CV-3 catalog is a reference for grouping modules into
musician-recognizable domains; it does not prescribe VAL's taxonomy.

## Ordered implementation steps

1. Define a typed catalog record with stable module type, display name,
   category, short purpose, shipped/proposed state, availability and
   accessibility description. Derive search, categories, insertion and help
   from the same source.
2. Group only modules that exist. Search spans all available categories and
   results state whether an item is a processor, source, analyzer or routing
   utility. Preserve the module/node distinction in the catalog and inspector.
3. Preserve double-click, Insert, Enter and keyboard navigation. Provide a
   visible selected row and an insertion-status message. Avoid requiring
   precise pointer use.
4. Add a JUCE drag source for an available catalog module and a canvas drop
   target. Distinguish module payloads from panel-docking payloads (which plan
   02 removes) and existing port-connection gestures.
5. Convert the drop point from screen/view coordinates through the existing
   canvas viewport/patch transform; account for zoom, pan, scroll offset and
   canvas origin. Place the node at the drop point, clamp only to a documented
   visible patch boundary, and avoid moving an existing node.
6. Treat each successful insertion as exactly one undoable graph edit. Select
   the new node and expose its properties without stealing focus from an
   active drag/keyboard path unexpectedly. Cancellation or an invalid drop
   makes no graph edit.
7. Never auto-wire on insertion or infer signal direction from screen
   proximity. Existing port gestures remain explicit; graph validation
   reports incompatible channel widths and disconnected routes.
8. Update `docs\DesktopWorkflow.md`, `docs\DspModules.md` and relevant design
   references after implementation. Clearly mark oscillator, tuner and mixer
   as proposed until their own accepted feature work ships.

## Non-goals

No oscillator, tuner, mixer, channel conversion, auto-wiring, automatic
transport, module-package/reuse system, broad graph redesign or catalog items
that are not implemented. Do not make drag the only way to insert.

## Acceptance checks

- Existing entries are present exactly once in the canonical catalog source
  and appear in truthful categories; proposed items are not selectable.
- Search works across categories; an empty search result is explicit and
  recoverable.
- Double-click, Insert button, Enter and keyboard focus still create one
  instance at the defined default position.
- Dragging each available category item to the canvas inserts at the pointer
  location under zoom, pan and scroll; dropping outside/canceling does not
  mutate the graph.
- Drag insertion creates one undo step and redo restores the same instance;
  no automatic connection or audio Start occurs.
- JUCE presentation tests exercise the real pointer/drop callbacks or an
  equivalent test seam. `tests\ModulePanelIntegrationTests.cpp`,
  `tests\PresentationTests.cpp`, `tests\NodeEditorTests.cpp`,
  `tests\GraphTests.cpp` and `tests\ProjectTests.cpp` are candidate coverage.
- Accessibility tree includes category names, result role/state, action
  instructions, keyboard insertion, and visible focus.

## Dependencies and risks

Follow `01-VisualDesignGuidelines.md`. Coordinate source classification with
`03-OutputOnlyProcessingPlan.md`; the tuner category depends on plan 05.
Potentially overlapping JUCE drag targets can confuse node placement and port
connection, so test payload types and gesture thresholds. Verify the canvas
coordinate transform rather than using raw screen coordinates.
