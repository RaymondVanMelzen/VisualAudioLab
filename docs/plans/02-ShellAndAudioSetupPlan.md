# Shell and Audio Setup Plan

**Status:** execution plan only. No shell or application code is changed by
this document.

## Objective

Remove device configuration from the permanent main workspace, make Audio
Setup an ordinary dedicated dialog, and remove docking as a product behavior.
Keep a stable patch canvas and replace dock-location selectors with small,
accessible collapse/reopen controls for eligible fixed panels.

## Confirmed requirements

- Audio Setup has exactly one entry point: **Audio > Audio Setup**.
- Remove the toolbar Audio Setup button and the View-menu Audio Setup item.
- The setup UI is a dialog, not a docked/main-view panel.
- Remove docking functionality throughout: no re-docking, floating docks,
  dock tabs, draggable panel headings or dock selectors.
- Use a small icon to collapse/reopen eligible panels. The patch canvas is
  always visible and cannot be collapsed.
- Keep explicit Start/Stop, automatic background enumeration/capability
  discovery, actionable errors, no silent fallback and no automatic playback.
- Follow `01-VisualDesignGuidelines.md` before implementing visual changes.

## Investigated surfaces and current behavior

- `src\VisualAudioLab.Presentation\src\MainComponent.cpp` installs Audio Setup
  in the toolbar, View menu and Audio menu; it sets setup panel content and
  responds to a dismissal callback.
- `src\VisualAudioLab.Presentation\src\ShellChrome.cpp` and
  `include\val\presentation\ShellChrome.h` define the toolbar button.
- `src\VisualAudioLab.Presentation\src\DockWorkspace.cpp` implements dock
  tabs, drag-to-dock headings, dock selectors, panel movement and visibility.
- `include\val\presentation\DockWorkspace.h`,
  `include\val\presentation\DockLayout.h` and
  `src\VisualAudioLab.Presentation\src\DockLayout.cpp` define dock identities
  and layout state. `MainComponent.cpp` bridges project view state to
  project-layout persistence; inspect the codec and preference lifecycle
  before choosing the replacement storage.
- `src\VisualAudioLab.Presentation\src\RuntimePanel.cpp` hosts setup controls.
  `docs\AudioBackends.md` documents background discovery in an isolated helper,
  a 15-second discovery/probe deadline, actionable failures, saved hints and
  explicit-only stream operations. Preserve these safeguards.
- `docs\DesktopWorkflow.md`, `docs\VisualDesignLanguage.md`,
  `tests\DockLayoutTests.cpp`, `tests\PresentationTests.cpp`,
  `tests\RuntimeIntegrationTests.cpp` and `tests\ProjectPresentationTests.cpp`
  are directly relevant documentation and test surfaces.

## Proposed design choices for review

Use a resizable JUCE settings dialog with its own heading, close behavior and
stable focus. Keep discovery asynchronous and show pending, ready, empty,
failed and retry states inside the dialog. A compact status in the main shell
may report stopped/running/setup-required/error, but it must not become a
second setup button or imply that audio is active. Dialog modality is an open
choice: prefer a non-blocking settings window if it preserves familiar
Windows/JUCE behavior and safe configuration lifecycle; otherwise explain why
modal behavior is necessary. Closing the dialog must not start/stop transport
implicitly. Configuration changes continue to use existing runtime
validation and explicit Start.

Use fixed side regions with an accessible icon button whose accessible name
describes the action, e.g. “Collapse Module Browser” / “Show Module Browser”.
Preserve panel identity and keyboard focus when collapsing/reopening. Keep
necessary bounded resizing if it does not recreate docking. The canvas region
must always have a nonzero usable size.

## Ordered implementation steps

1. Inventory all shell commands, toolbar controls, accessibility names,
   shortcuts, layout fields and serialization paths. Separate dock placement
   from project data that must remain.
2. Define the fixed shell geometry and panel visibility state using the
   approved visual guidelines. Keep the canvas present, non-collapsible and
   keyboard reachable at every supported window size.
3. Replace the Audio Setup dock content with a dedicated dialog. Wire only
   Audio > Audio Setup; remove the toolbar command/button and View-menu
   command/item. Ensure dialog close/dismiss cannot change transport.
4. Remove dock destination selectors, heading drag behavior, tabbed dock
   areas, docking drag/drop targets, float/re-dock behavior, and obsolete
   dock-position commands. Do not remove module-to-canvas or port-connection
   drag/drop as collateral.
5. Add collapse/reopen icon controls to eligible fixed panels. Add stable
   accessible names, keyboard activation, focus restoration, tooltips and a
   visible non-color-only collapsed state. Do not offer a collapse action for
   the canvas.
6. Migrate saved layout safely. Preserve unrelated project/window preferences;
   map legacy panel visibility sensibly, ignore obsolete dock coordinates,
   and retain a recovery/reset path without silently rewriting malformed
   user files.
7. Update `docs\DesktopWorkflow.md` and design references only after behavior
   is implemented. Remove instructions that still direct users to toolbar,
   View-menu or dock setup. Update the canonical visual guideline under plan
   01 rather than leaving contradictory adaptive-docking proposals.

## Non-goals

No DSP/device semantics change, no new audio backends, no broad visual
redesign, no hidden canvas mode, and no change to explicit Start/Stop. Do not
remove bounded panel resizing merely because docking is removed.

## Acceptance checks

- Audio Setup opens from Audio menu only; it is absent from toolbar and View.
- Device discovery continues asynchronously, with pending/error/retry feedback
  and no UI-thread wait; close/reopen and cancellation do not strand workers.
- Main workspace has no dock-location selectors, dock tabs, panel drag-to-dock
  or float/re-dock path.
- Canvas remains present, visible and usable through collapse/reopen actions
  and legacy-layout migration.
- Eligible panels have accessible collapse/reopen controls, visible keyboard
  focus and restored focus.
- Collapsing panels or opening/closing setup does not start audio or
  inadvertently change transport.
- Tests cover menu and toolbar inventory, panel visibility state, legacy
  layout migration, minimum window geometry, setup lifecycle, discovery
  pending/failure/close and explicit transport.
- Update `tests\DockLayoutTests.cpp` or replace obsolete assertions with fixed
  shell state tests; retain meaningful project-persistence regressions.

## Risks and open decisions

Dialog modality and persistence destination (project layout versus
machine-local shell preferences) need review against current project semantics.
Ensure the setup dialog does not block native control-panel interaction or
background discovery. Removing docking must not break project opening or
layout migration. A compact status is allowed only if it is passive, truthful
and consistent with the approved guidelines.
