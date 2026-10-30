# Visual Audio Lab: UI Prototype Plan

## Purpose, sources, and status

Plan a first, **UI-only prototype** to validate the workbench proposed in [UxBlueprint.md](UxBlueprint.md): can former programmers, hobby musicians, sound designers, and DSP students discover, edit, navigate, and explain a visual patch?

This document defines proposed screens, controls, conceptual view models, navigation, a constrained docking model, and usability evaluation. It is not an implemented interface, implementation instructions, or evidence of validated usability.

Respect [ProductVision.md](ProductVision.md), [PRD.md](PRD.md), and the revised [TechnicalArchitecture.md](TechnicalArchitecture.md): C++20, JUCE, and CMake. View models mean separated C++ presentation state and commands, not a new UI framework or a claim that JUCE supplies declarative MVVM bindings. This plan creates no source files, build configuration, libraries, or application.

The prototype is a research increment, not PRD Release 0.1, the 0.5 MVP, or an audio-capable release. It cannot satisfy their audible-result requirements. Future VST3 hosting, plugin export, reusable modules, and advanced visualizers retain their existing scope decisions and are not prototype features.

## Hard boundary: UI only

| Included as planned prototype behavior | Explicitly excluded |
|---|---|
| Menus, panels, dialogs, focus, shortcuts, accessible labels, and resizing. | Device enumeration, driver access, ASIO/CoreAudio initialization, device selection against real hardware, audio callbacks, streams, or permissions. |
| In-memory editing of illustrative node cards, connection drawings, positions, and parameter fields. | DSP nodes, graph compilation/scheduling, signal generation/processing, sample buffers, sound playback, meters derived from signals, and latency measurement. |
| A small authored fixture catalog and local explanatory text. | Plugin scanning/loading, VST3 hosting, plugin export, marketplaces, network calls, cloud/AI services, or accounts. |
| UI-only undo/redo, validation copy, and deterministic example/error scenarios. | VAL Script editing, parsing, compilation, execution, or a scripting engine used to drive scenarios. |
| Explicitly simulated Save/Open and audio-status presentations. | Production persistence, project migration, real user-file loading/saving, or an implied application/audio backend. |

Use only the JUCE UI/event capabilities necessary for a future UI implementation; do not instantiate audio-device or processor services just because JUCE provides them. Do not start the production Application, Infrastructure, or Audio runtime to make prototype screens appear functional. A plain in-memory presentation model is sufficient; it is not a fake production adapter reporting success.

The fixture catalog may use the blueprint's illustrative Source, Level, and Output roles, with authored names, ports, ranges, and descriptions. These are visual study materials, not implemented sound generators or a finalized component inventory. Connection eligibility is a small declared UI rule set or authored scenario, not DSP validation.

### Truthful simulation contract

- Keep a visible, screen-reader-accessible banner: **"UI prototype - no audio, DSP, scripting, or disk saving. Changes are temporary."**
- Mark every simulated state inline, not only in facilitator instructions. A scenario card saying "Simulated: Running" must never change the actual capability indicator, which remains "Audio unavailable in this prototype."
- Retain the blueprint's Start Audio and Stop Audio positions, labels, and menu entries, but disable their commands with an adjacent explanation. Shortcut invocation explains the unavailable capability rather than changing status or sounding a tone. Never request hardware access or use operating-system beeps.
- Explore running, inactive-edit, unavailable-output, and interrupted-output copy through named, predefined **Study Scenarios** under Help. The facilitator or participant may select a card explicitly; no timer, graph edit, or Apply action pretends to activate processing. Cards are separate from the current interactive patch and cannot overwrite its edits.
- Keep Signal Inspector values "Unavailable - no processing"; do not animate traces or invent live readings. Static scenario illustrations, if needed, must say "Illustration, not a measurement."
- Label project commands **Open Example**, **Open Demo Snapshot**, and **Save Demo Snapshot** in the prototype menus. Save captures only the current in-memory fixture state for reopening in the same process; it never claims "Saved to disk." Save As is omitted. Restart/reset discards snapshots, as disclosed.
- Simulated save failures come from an explicitly selected study scenario, not real filesystem operations. Cancel and error remain distinct outcomes. No native file picker should suggest that arbitrary user work can be opened or preserved.

These are intentional prototype deviations from the functional blueprint. They validate discoverability and comprehension, not audio behavior, persistence reliability, or backend integration.

## Usability questions

| Question | Observable evidence |
|---|---|
| Does the classic-workbench organization help without requiring prior IDE experience? | Where each audience looks first for Add, Properties, project navigation, and help; wrong turns and terminology explanations needed. |
| Can people understand a connection and construct one without precise dragging? | Correct endpoint identification, completion via the chooser, incompatible-port explanation, cancellations, and accidental edits. |
| Can people distinguish focus, selection, a numeric draft, and an accepted UI edit? | Predictions before committing, visible draft/error recognition, successful correction/cancel, and synchronized selection. |
| Can people recover from mistakes without losing their mental model? | Use of undo/redo, locating diagnostics, restoring hidden panels, and retaining context after dialogs. |
| Is the UI-only limitation unmistakable? | Participants correctly explain that no sound, hardware inspection, processing, or disk saving occurred, including after viewing a simulated Running card. |
| Can essential prototype tasks be completed without dragging or relying on color? | Keyboard-only task completion, meaningful screen-reader output, focus continuity, readable scaled layouts, and equivalent named actions. |

The study cannot establish sound quality, latency, realtime safety, DSP correctness, script usability, physical device compatibility, host compatibility, or complete product accessibility conformance.

## Screens and controls

Panels below are surfaces within one workbench, not independent documents or routes. Use one temporary project per window for the study; no multi-document or multiple-window project model is implied.

| Screen / surface | Planned controls and content | Planned interaction and limits |
|---|---|---|
| Main workbench | Project title with "Temporary / Changed" state; File, Edit, View, Patch, Audio, Help menus; toolbar with Open Example, Save Demo Snapshot, Undo/Redo, Add Component, disabled Start/Stop, Audio Settings; persistent prototype banner and status. | Start with an empty fixture and concise local guidance. New replaces temporary work only after confirmation when changed. Disabled audio controls remain discoverable through adjacent text/menu explanations; no fictional active engine. |
| Patch Canvas and Structure List | Named node cards and ports, explicit direction/type/channel text, selection/focus outlines, provisional and committed connection drawings; zoom, Fit Patch, Move Node, Connect/Disconnect and Remove actions. | Add/select/move/remove and draw connections in memory. Canvas/List switch retains the selected stable fixture ID. Expose both endpoints in text. Ordinary arrows navigate; movement requires explicit Move Node mode with announced step and exit. Zoom does not shrink application text. |
| Toolbox | Local search, role categories, named results, short purpose, port summary, Add and Local Help. | Enter/Add inserts at a visible location and focuses the new item. Search and empty-state guidance use fixtures only. Dragging may be evaluated as an optional convenience, never required. |
| Properties | Selection heading, identity/layout fields, parameter value/unit/range/default, numeric alternative to a slider, Reset, port details and inline diagnostics. | Invalid drafts remain visible; correction/commit changes only the presentation fixture. Escape cancels the draft without changing its last accepted value. A continuous gesture is one UI undo step. No audible/effective DSP value or automation engine is implied. |
| Project Explorer | Temporary project, current patch, fixture node instances, diagnostics with counts and Locate actions. | Selecting a node synchronizes Canvas/List/Properties/Inspector. No empty script/module folders, filesystem tree, or file-per-node claim. A diagnostic navigates to its relevant field or endpoint. |
| Audio Settings mock dialog | Persistent "Mock settings - no hardware access" label; Windows/ASIO or macOS/CoreAudio fixture, fictional output entries, requested rate/buffer/channel mapping fields, effective-status explanation, Apply to Demo and Cancel. | Choices are study data, not enumeration. Apply updates only the in-memory preference draft and reports "Demo preference updated; no device opened." Effective configuration and latency stay unavailable. No WASAPI primary/fallback promise or real driver panel. |
| Signal Inspector | Selected node/port/connection metadata, direction/type/channels, status explanation, Local Help and Locate Node. | Follows selection without changing routing. All signal observations are explicitly unavailable. No waveform/spectrum, synthetic animation, or background signal generator. |
| Connect chooser | Named source, searchable destinations, eligibility/reason text, Connect and Cancel. | Keyboard and pointer paths produce the same fixture connection. Explain ineligible ports with authored UI rules. Confirm commits once; cancel removes only the provisional drawing. It is not graph preparation. |
| Examples and demo snapshots chooser | Bundled fixture descriptions and session-only snapshot names, Open and Cancel. | Explain that examples are static editing demonstrations and snapshots vanish on exit. Opening a different fixture follows temporary-change confirmation. No native file dialog or disk I/O. |
| Temporary-change confirmation | Save Demo Snapshot / Discard / Cancel, with explicit session-only warning. | Cancel leaves the fixture and focus unchanged. Save retains an in-process snapshot before the requested navigation; it does not make closing the process durable. Exit warns that all session data will be lost even after a demo save. |
| Local Help and Study Scenarios | First-edit instructions, glossary, component descriptions, named static state cards and Return to Workbench. | Offline content only. Scenario selection is explicit and reversible; opening it does not modify the current patch. Show simulated project/device/edit status separately from actual unavailable capabilities. |

Illustrative screen composition:

```text
+-----------------------------------------------------------------------+
| UI PROTOTYPE - no audio/DSP/scripting/disk saving; temporary changes     |
| File Edit View Patch Audio Help                                        |
| [Open Example] [Save Demo Snapshot] [Undo] [Add] [Start x] [Stop x]      |
+------------------+---------------------------------+------------------+
| Project Explorer | Patch Canvas / Structure List  | Properties       |
| Temporary patch  |                                 | Selected fixture |
| Nodes            | [Source] --> [Level] --> [Output]| Value / range    |
| Diagnostics      |                                 | Draft / error    |
|------------------|                                 | Local Help       |
| Toolbox / Search |                                 |                  |
+------------------+---------------------------------+------------------+
| Signal Inspector: metadata only; signal values unavailable             |
| Audio unavailable | Temporary changes | [View / Reset Layout]          |
+-----------------------------------------------------------------------+
```

This is a containment sketch, not a copied product interface, pixel specification, or committed component inventory.

## Conceptual view models and state ownership

These names describe responsibilities, not classes to generate or libraries to select. They can be combined where simple; do not create a service framework merely to support a study.

| Conceptual view model | Owned state | Commands / visible outcomes |
|---|---|---|
| WorkbenchViewModel | Current temporary-project identity, shared selection/focus target, capability explanation, active dialog, and derived change/history availability. | New, open chooser, show region/dialog/help, focus return, exit confirmation. Coordinates child presentation models without implying production use cases. |
| PrototypeProjectViewModel | One in-memory fixture: stable IDs, node positions, connection endpoint IDs, accepted display values, UI edit history, and session-only demo snapshots. | Accept fixture edits, undo/redo, capture/reopen demo snapshot, reset fixture; returns explicit UI outcomes. No audio graph, executable node definitions or project file format. |
| PatchEditorViewModel | Canvas/List mode, viewport, provisional connection and Move Node interaction state. | Select, propose add/move/remove/connect, cancel gesture, Fit Patch; changes accepted fixture state through the prototype project owner. |
| ToolboxViewModel | Search query, categories, result selection and authored descriptions. | Filter locally, Add, show explanation. No component discovery service or plugin catalog. |
| PropertiesViewModel | Selected item's presentation, numeric drafts/errors and current gesture scope. | Commit/cancel draft, reset, change displayed parameter; distinguishes an invalid draft from an accepted UI value. |
| ProjectExplorerViewModel | Tree expansion and derived nodes/diagnostics from the fixture. | Select/Locate without maintaining a second copy of patch truth. |
| AudioSettingsPreviewViewModel | Platform-specific authored choices, unapplied requested-value draft and last accepted demo preference. | Apply to Demo/Cancel. Effective configuration stays unavailable; no backend handle, start operation or asynchronous device result. |
| SignalInspectorViewModel | Selected metadata and explicit unavailable-observation explanation. | Locate, local explanation; no sample storage, polling or processing dependency. |
| LayoutViewModel | Region visibility, split proportions and selected narrow-window secondary panel for this session. | Show/hide, resize through pointer or keyboard alternative, Reset Layout, Focus Region; layout changes do not enter patch undo history. |
| StudyScenarioViewModel | Selected predefined card and its simulation label; independent of the interactive fixture. | Show/close state illustration, restore workbench focus. No script interpreter, state machine pretending to run audio, or timed engine activity. |

Dialog-specific state belongs to its invocation and is released on close. A shared selection ID, not cloned node data, drives Explorer, Canvas/List, Properties and Inspector. Focus and selection are separate: moving focus to a toolbar must not select another node.

For a future JUCE UI implementation, update views and these presentation models on the message/UI context using explicit ownership and scoped listeners. No audio thread, backend queue, application-wide event bus or production dependency injection container is needed. The production architecture's use-case boundaries remain future work; the prototype's local presentation fixture must not become an accidental production project model.

## Navigation and keyboard model

### Workspace and dialog navigation

- Keep patch editing in a stable central workspace. Explorer selection reveals an item; Toolbox Add focuses the inserted node; Properties and Inspector follow selection. Do not replace the entire screen to edit one parameter.
- Use View commands to show or focus each named region, switch Canvas/Structure List, Fit Patch, and Reset Layout. F6/Shift+F6 cycle visible regions in a documented order: Explorer, Toolbox, Canvas/List, Properties, Inspector; hidden regions are skipped. The banner/status remain available through ordinary accessible navigation.
- Open Audio Settings from toolbar, Audio menu or status explanation. Settings, Connect chooser, snapshot chooser and confirmations are bounded dialogs; Help/Study Scenarios may use a secondary panel. No nested dialog is required for a core task.
- Focus enters each dialog at its heading or first meaningful control; Escape/Cancel returns to the invoker or a surviving logical item. Closing a panel moves focus to the workbench, not an invisible control. A validation error does not trap focus.
- Connect shows source and destination names before confirmation. Escape cancels provisional wiring; it is not a Stop command. Delete explicitly includes attached visual connections, and undo restores them.
- Selecting an unavailable/missing-definition scenario never inserts a substitute component. Show the explanation and preserve the current temporary work.

### Prototype shortcut subset

Retain the blueprint's proposed mappings where the action exists, with labeled menu/button alternatives and native text-field behavior.

| Action | Windows | macOS | Prototype meaning |
|---|---|---|---|
| New / Open | Ctrl+N / Ctrl+O | Command+N / Command+O | New temporary fixture / demo chooser, not real file access. |
| Save Demo Snapshot | Ctrl+S | Command+S | Session-only snapshot; explicitly not disk saving. No Save As shortcut. |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z | Command+Z / Command+Shift+Z | Focused text history or fixture edit history according to context. |
| Add / Connect | Ctrl+K / Ctrl+Shift+K | Command+K / Command+Shift+K | Toolbox search / named-endpoint chooser. |
| Focus Properties | Ctrl+Shift+P | Command+Shift+P | Preserves invalid drafts without silently committing them. |
| Audio Settings | Ctrl+, | Command+, | Mock settings; macOS application Settings convention. |
| Start / Stop | Ctrl+Enter / Ctrl+Shift+Enter | Command+Return / Command+Shift+Return | Unavailable explanation only; never simulated playback. |
| Cycle regions | F6 / Shift+F6 | F6 / Shift+F6 if available | Named Focus Region alternative covers Fn/media-key and OS conflicts. |
| Remove / Cancel | Delete / Escape | Backspace/Delete / Escape | Graph-context remove / cancel current UI interaction. Text fields retain normal editing. |

Do not intercept operating-system or assistive-technology shortcuts. Tab/Shift+Tab and arrows follow control semantics; test physical keyboard layouts and macOS keyboard-navigation preferences. The disabled Start/Stop footprint can test visibility and understanding only, not the reliability of a real stop path.

## Docking model

For this first prototype, **"docking" means fixed, resizable, collapsible regions**, not a general docking framework. This follows the blueprint's restrained baseline and makes layout usability testable without adding window-management complexity.

| Region | Default placement | Allowed prototype changes |
|---|---|---|
| Patch Canvas / Structure List | Center, always available. | Switch representation, zoom/fit canvas; no undocking or closure of the only editor. |
| Project Explorer and Toolbox | Left, vertically divided. | Resize/collapse individually; show/focus through View. |
| Properties | Right. | Resize/collapse; reopen on request, preserving current selection. |
| Signal Inspector | Bottom. | Resize/collapse; reopening does not start observations or change the fixture. |
| Menus, toolbar, prototype banner and status | Shell perimeter. | Remain discoverable; do not let panel resizing hide capability warnings or the disabled audio-control explanation. |

Splitters need a keyboard-accessible alternative, such as a named Adjust Panel Size action with discrete steps. Do not require precise dragging to restore space. Define logical minimum sizes from usable controls/text and evaluate them during the study rather than inventing pixel commitments.

At narrow sizes or enlarged text, collapse secondary regions into a named panel selector, showing one secondary panel at a time alongside the editor where space permits. At still smaller sizes, allow a full-width secondary panel with an explicit Back to Patch action; never overlay essential content without navigation. Preserve selection, draft values and focus restoration across these transitions.

Reset Layout restores the default region arrangement and safe focus without altering patch content, demo snapshots or edit history. Layout preferences last only for the study session; no disk settings store is introduced.

Exclude floating panels, drag-to-dock targets, arbitrary tab stacks, detachable windows, cross-monitor restoration, layout import/export and third-party docking packages. Broader docking remains an open product decision, not a requirement unlocked by this document's title.

## Usability-validation scenarios and measures

Use tasks grounded in the blueprint but substitute visual editing and explanation for its audible-result tasks. Tell participants the UI-only boundary before starting, then check whether the interface continues to communicate it without reminders. Use authored offline fixtures and reset between independent tasks; do not present facilitator actions as application automation.

| Scenario | Participant task and observable completion | Measures to record |
|---|---|---|
| Orient and explain | From the empty workbench, locate Add, Properties, local help and unavailable audio controls; explain what this prototype can and cannot do. | First location tried, wrong turns, assistance, time to discovery, and mistaken expectations about sound or disk saving. |
| Build a visual path | Add the illustrative Source, Level and Output cards; connect the shown compatible ports and describe the path in their own words. | Independent/assisted/failed completion, time, endpoint errors, accidental actions, and explanation accuracy. No sound is an expected result. |
| Connect without dragging | Using only keyboard navigation and Structure List, connect named endpoints, inspect an ineligible destination, cancel once and complete the intended connection. | Reachability, focus order, usable announcements, reason comprehension, cancellation integrity and any pointer dependency. |
| Edit and recover | Enter a value outside an authored range, correct or cancel it, make a slider gesture, then undo/redo that gesture. | Whether the invalid draft remains clear, previous value preservation, undo grouping, focus escape, and confusion between displayed and processed values. |
| Find and inspect | Select the same fixture node through Explorer and Canvas/List; locate a seeded UI diagnostic and inspect metadata. | Selection consistency, endpoint/diagnostic localization, unexpected focus loss, and whether "unavailable" is mistaken for zero. |
| Arrange and recover layout | Collapse a panel, resize another without dragging, increase text size, switch to a narrow-window layout, and restore the default. | Lost controls, truncation/overlap, keyboard completion, retained selection/drafts and successful panel recovery without facilitator intervention. |
| Understand mock settings | On the platform-appropriate fixture, change a requested setting, cancel, then Apply to Demo; explain requested versus effective state and the ASIO prerequisite example. | Preserved/canceled drafts, interpretation of unavailable effective values, and any belief that hardware was inspected or opened. |
| Preserve temporary work | Capture a demo snapshot, edit, reopen it, cancel a replacement, then inspect an explicit simulated-save-failure card and the exit warning. | Snapshot understanding, correct change marker, recovery choice, and whether participants understand that nothing survives process exit. This is not a persistence test. |
| Interpret future-state copy | Read named Simulated Running, Inactive Edit and Output Unavailable cards; identify which edit the card describes and why the real prototype remains silent. | State distinction, proposed recovery choice, uncertainty and any simulation/real-state confusion. No callback, prepared graph or device event is produced. |

### Study method and evidence

Recruit across the four named audiences, including people with and without classic-IDE experience. Include keyboard-only and screen-reader participants and evaluate on both Windows and macOS rather than assuming a shared framework makes behavior equivalent. Participant count, recruitment mix, supported OS/keyboard/assistive-technology configurations and study schedule remain study-design decisions, not invented commitments.

Use neutral prompts and observable task outcomes. Record completion as independent, assisted, or not completed; distinguish successful explanation from a lucky click. Measure task time from reading the task to the agreed visible outcome, and report denominators, interventions and prototype defects rather than hiding them in an average. Capture wrong turns, accidental destructive actions, recovery attempts, focus traps, label misunderstandings and a brief participant confidence/ease response after each task.

Timing and success rates are descriptive study observations, not performance or adoption promises. Do not set arbitrary percentage/time pass marks without a baseline. Compare revisions using equivalent task/fixture definitions and disclose learning effects. Keep participant feedback separate from facilitator inference and from defects caused by intentional prototype limitations.

Evaluate visible focus versus selection, non-color states, numerical alternatives, text scaling/high contrast, reduced motion and semantic graph navigation. Check Narrator and VoiceOver behavior hands-on. Record exactly what was exercised; a completed small study is not a full accessibility-conformance claim.

Collect notes through an explicit, consented study process, not hidden telemetry. Prefer anonymous task codes and observations; screen/audio recordings are not needed by default. Keep results local and minimize captured personal content. Study note-taking is outside the product prototype and does not add logging, networking or recording features to it.

### Review and iteration criteria

Before calling an iteration ready for study, verify the UI-only boundary, consistent fixture selection, reversible UI edits, clearly labeled demo snapshots/scenarios, keyboard escape paths, and recovery of hidden panels. Any device access, generated sound, DSP/script execution or real file-save success claim is a scope violation, not a useful extra.

After study, prioritize blockers that prevent essential tasks, cause accidental loss of temporary work, trap keyboard users or mislead people about simulated capabilities. Revise labels, navigation or layout and repeat the affected scenarios. Report unresolved barriers and audience/platform differences rather than claiming the whole product is usable.

The expected research output is an evidence-backed decision about the workbench organization, connection chooser/Structure List, Properties editing, constrained panel layout and status wording. It is not approval to add audio, DSP, scripting, plugins or production persistence. Those require a separate implementation assignment and retain the PRD's release and quality decisions.

## Assumptions and deferred decisions

- A single temporary project, fixture set, session-only snapshots and fixed-region layout are deliberate study simplifications, not production-format or multi-document decisions.
- Exact example labels/ranges, dialog wording, splitter limits, narrow-layout thresholds and shortcut conflicts should be resolved through usability evidence.
- Static status cards are for comprehension testing only; they do not prove users can operate live audio controls or understand audible effects.
- How much JUCE component customization is needed for accessible canvas/list semantics must be checked during a later UI implementation; no framework-level accessibility guarantee is assumed.
- Broader docking, persistent layouts, multiple windows, project-file dialogs, audio integration, signal visualization, VAL Script, VST3 hosting and plugin export are deferred. Their existence in other architecture documents does not expand this first prototype.

This plan is documentation only. No UI, backend, audio path, DSP engine or scripting capability is implemented by it.
