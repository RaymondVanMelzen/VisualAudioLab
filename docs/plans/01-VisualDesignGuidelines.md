# Visual Design Guidelines and Review Gate

**Status:** actionable design rules for review; proposed product direction,
not shipped UI behavior. This document extends
`docs\VisualDesignLanguage.md`; it is not a competing visual system.

## Objective

Prepare a coherent, signal-first visual standard for later UI implementation.
Resolve contradictions between the existing adaptive-docking proposals and
the user's confirmed fixed-shell direction before changing the interface.
Use Arturia and AAS products as quality references, not as templates or
claims of verified parity.

## Confirmed requirements

- The patch canvas remains in the main view and cannot be collapsed.
- Remove docking functionality entirely. Eligible side panels may be
  collapsed/reopened using small, accessible icon controls.
- Audio Setup is a dialog reachable only from the Audio menu.
- Module categories, drag insertion and the guitar tuner are covered in their
  own plans; this guideline defines the visual and interaction standards they
  must meet.
- Explicit Start/Stop and truthful setup/analysis status remain visible and
  understandable. Never autoplay or invent signal/measurement state.

## Investigated design and evidence

Read `docs\VisualDesignLanguage.md`, especially its product premise, attention
hierarchy, color tokens, typography/rhythm, node identity, visualization
states, workspace concepts and open tradeoffs. Read
`docs\DesktopWorkflow.md` for current setup/catalog behavior and
`docs\SignalVisualization.md` for current live/stale/source states. The current
design language already proposes a signal-first hierarchy, semantic color,
named units, redundant status cues, readable nodes, keyboard access and
reduced motion. Preserve these strengths.

The dated screenshots listed in
`00-UiReviewAndExecutionPlan.md` show an injected-device test shell, not a
fresh production or hardware audit. Arturia's Pigments page describes a
responsive color-coded interface and visual feedback; AAS's Multiphonics
catalog shows domain grouping; AAS Strum GS-2 is a guitar-oriented product
reference, not a tuner. These sources motivate evaluation questions, not
validated usability claims.

## Guidelines

### Instrument identity and hierarchy

1. Make the patch and its audible/observed signal the visual center of gravity.
   Device, file and project administration should be compact and appear when
   requested or when recovery needs attention.
2. Use a restrained studio/instrument identity: layered dark-neutral surfaces
   from the existing palette, crisp grid and signal-route accents, typography
   with strong hierarchy, and purposeful spacing. Do not add decorative
   hardware skeuomorphism or reproduce vendor artwork.
3. Keep the primary shell stable and predictable: persistent menu/transport
   chrome, a permanently present patch canvas, and fixed side regions that can
   collapse/reopen without moving to other docking areas. Do not show tabs
   representing dock locations or drag handles implying re-docking.
4. Make Stop and a truthful stopped/running/fault state easy to locate.
   Configuration availability or a visible meter must not imply that audio is
   running.

### Signal, patch, modules and controls

1. Prefer a readable patch flow and selected signal path over rows of generic
   equal-weight panes. Keep the canvas usable when other panels collapse.
2. Give each shipped category a stable, modest visual mark plus a text label;
   never use color alone to communicate module identity, direction or state.
   Unavailable/proposed modules must not appear as enabled catalog choices.
3. Distinguish audio sources/processors/analyzers from routing utilities in
   names, descriptions and node treatment. Ports show direction, channel
   identity/width and labels supported by actual metadata. Never imply that a
   channel count is a chosen port or that a visualizer changes the signal.
4. Label every adjustable value with a meaningful name, unit, range/context
   and current value where applicable. Keep interaction targets large enough
   for pointer and touch-like precision, with visible focus and a clear
   selected/hover/disabled distinction.
5. Direct manipulation must have an accessible alternative. Dragging a module
   cannot be the only insertion path; keyboard selection and activation remain
   available. Dropping or selecting a module must not silently connect it.

### Color, type, layout and accessibility

1. Reuse the token system in `docs\VisualDesignLanguage.md`; reconcile it
   rather than introducing a second palette. Separate surface, category,
   signal and semantic-state colors. A category accent must not be mistaken
   for warning/error.
2. Maintain contrast for text, focus, selection, grid and thin routes at
   normal and high-DPI scaling. Pair color with text, shape, line style or
   iconography. Respect Windows high contrast and system text scaling.
3. Preserve the existing four-pixel spacing rhythm as a baseline, but validate
   actual density instead of mechanically applying it. Titles, units and
   controls must not clip at supported minimum window dimensions.
4. Provide a stable keyboard traversal order, keyboard equivalents for
   collapsing panels and inserting modules, screen-reader names/descriptions,
   and a visible focus ring. Avoid keyboard traps in the canvas and Audio
   Setup dialog.
5. Use animation only for verified data/interaction changes. Stopped, absent,
   stale, invalid and unavailable measurements need distinct visible text or
   symbols. Respect reduced-motion preferences.
6. Preserve plot provenance: source, channel, units, rate and freshness. A
   visual effect must not suggest live data when the capture is stopped,
   missing or stale.

### Proposed measurable review criteria

These are design acceptance targets to validate during implementation, not
claims about current support:

- At 100% and 200% Windows text scaling, and at representative macOS scaling,
  the canvas remains visible, panel controls remain reachable, and no
  essential text/control clips at the agreed minimum window size.
- The main view exposes the selected patch, current transport state and Stop
  without opening a panel; opening Audio Setup takes one Audio-menu action.
- Every category and state is understandable without color; keyboard users can
  discover and use the same insertion/collapse actions as pointer users.
- A stopped/no-source/stale visualizer never renders with live styling.
- Review screenshots include the smallest supported window and a standard
  high-DPI layout; screenshots are labeled by platform, scale and build.

## Execution sequence, non-goals and gate

1. Re-read the current guideline and this plan; inventory statements that
   conflict with the confirmed no-docking/non-collapsible-canvas direction.
2. Update the canonical `docs\VisualDesignLanguage.md` to remove those
   contradictions while preserving useful color, typography, accessibility,
   signal hierarchy and truthful-state rules. Record proposals as proposals.
3. Produce a small set of shell and module-browser wireframes/states using
   repository-native design documentation; include normal, collapsed-panel,
   Audio Setup, error, stopped/live and high-DPI cases.
4. Review the guideline and wireframes against the measurable criteria and
   request explicit approval before implementing shell or feature UI.
5. Once approved, implementation proceeds through the separate execution
   plans; any new visual rule must be reconciled in the canonical guideline.

**Non-goals:** a complete product redesign, new typography/assets copied from
commercial products, unimplemented DSP features, or declaring commercial
parity based on vendor marketing pages.

**Dependencies:** this is the prerequisite review gate for plans 02–05.

**Risks and open choices:** establish minimum supported window size and
platform/high-DPI review matrix; decide which fixed side panels are
collapsible by default; choose dialog modality; and validate palette contrast
against actual JUCE rendering. Do not treat proposals here as settled product
defaults until reviewed.
