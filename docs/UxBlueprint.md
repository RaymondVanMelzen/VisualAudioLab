# Visual Audio Lab: UX Blueprint

## Intent and status

Create a welcoming desktop workbench for people who remember Turbo Pascal, Delphi, or Visual Basic: discoverable menus, a central working surface, a component toolbox, a project tree, a property inspector, and an obvious run/stop loop. Borrow familiar interaction principles, not another product's distinctive artwork, branding, exact layout, or proprietary interface.

This is a proposed UX design, not an implemented interface or a tested accessibility claim. It follows [ProductVision.md](ProductVision.md), [PRD.md](PRD.md), and [FunctionalArchitecture.md](FunctionalArchitecture.md). Platform and processing states follow [TechnicalArchitecture.md](TechnicalArchitecture.md), [AudioPlatformStrategy.md](AudioPlatformStrategy.md), and [DspArchitecture.md](DspArchitecture.md).

Windows and macOS share the same core patching workflow, using platform conventions for menus, dialogs, shortcuts, and assistive technology. Core creation, examples, help, audio use, and local save/reopen work offline without an account, subscription, cloud service, or AI service.

The revised implementation direction is C++20 with JUCE and CMake; primary standalone audio paths are Windows/ASIO and macOS/CoreAudio. This is still a UX proposal, not a JUCE implementation. Future VST3 hosting and plugin export must fit the architecture, but are not part of the introductory workflow or assigned to any named release.

## Experience principles

- **A workbench, not an imitation:** Use readable typography, ordinary controls, clear focus, and comfortable spacing. Nostalgia comes from directness and predictability, not low-resolution decoration or tiny targets.
- **Visual first:** Users can build and hear a patch without typing source code. Toolbox and property inspection provide the familiar designer experience.
- **Discoverable depth:** Start with a small useful collection and complete examples; explain terms locally. Do not lead with a marketplace, scripting editor, or empty advanced settings.
- **Truthful audio state:** Saved, edited, prepared, and active are different states. A moving control or a successful save does not prove that the changed patch is audible.
- **User-controlled sound:** Starting audio is explicit; Stop remains easy to reach. No automatic restart through a newly selected physical output.
- **Reversible work:** Distinguish cancellation, undo, local saving, and audio stopping. A device failure must not discard the patch.

## Main window

Use one project workspace per window as the initial UX assumption. Multi-window and simultaneous audio-session behavior require a product decision; a tab-like document title does not commit to a multi-project implementation.

The window contains:

- A title showing project name and an explicit unsaved marker/text.
- Menus: File, Edit, View, Patch, Audio, and Help. On macOS, application Settings/About/Quit live in the application menu; the menu bar is system-hosted.
- A compact toolbar with New/Open/Save, undo/redo, Add Component, Start Audio, Stop Audio, and Audio Settings.
- Left-side Project Explorer and Toolbox regions, central Patch Canvas, right-side Properties, and a collapsible bottom Signal Inspector.
- A persistent status area showing selected output/mode, audio state, active-versus-edited revision status, and actionable warnings. Avoid unexplained diagnostic counters in the introductory view.

Do not require detachable/dockable panels in the MVP. Recommend resizable and collapsible regions, named View-menu commands, and Reset Layout. Responsive narrow-window behavior should collapse secondary regions into accessible named panels rather than obscure Stop or shrink text indefinitely.

### Wireframe: default workbench

```text
+--------------------------------------------------------------------------+
| Visual Audio Lab - Untitled [Unsaved]                                     |
| File  Edit  View  Patch  Audio  Help                (system menu on macOS) |
| [Open] [Save] [Undo] [Add Component]   [Start Audio] [STOP AUDIO] [Settings]|
+-----------------+-------------------------------------+------------------+
| PROJECT EXPLORER| PATCH CANVAS                        | PROPERTIES       |
| Project         |                                     | Selected: Level  |
|   Current patch | [Source] --> [Level] --> [Output]    | Name     Level   |
|   Components    |                                     | Gain     0.25    |
|   Diagnostics 1 | Selected: Level                     | Unit     linear  |
+-----------------+                                     | [Local Help]     |
| TOOLBOX         |                                     |                  |
| Search [      ] |                                     |                  |
| Generation     |                                     |                  |
| Shaping        |                                     |                  |
| Level / Output |                                     |                  |
+-----------------+-------------------------------------+------------------+
| SIGNAL INSPECTOR: Level output | Status: Stopped | Value: unavailable     |
+--------------------------------------------------------------------------+
| Audio: Stopped | Output: Not selected [Configure] | Patch: Edited         |
+--------------------------------------------------------------------------+
```

Labels are illustrative, not a finalized component inventory or pixel specification.

## Patch canvas

Nodes have a name, concise role, named input/output ports, rate/channel descriptions, and clear selection/focus outlines. Show audio/control distinctions using text or shape as well as color. Connections remain traceable at normal zoom; a selected connection exposes both endpoints in text.

Support add, select, move, connect, disconnect, and remove. Toolbox activation places a node at a visible insertion location and focuses it; drag-and-drop is an optional faster path, not the only path.

**Connection workflow:** Choose a source port, invoke Connect, choose a compatible destination from a searchable list, then confirm. Pointer users may also drag a cable. Explain incompatible destinations rather than simply making them impossible to discover. Canceling leaves no accepted connection. A provisional cable is visually and accessibly different from a valid route.

Provide a synchronized **Structure List** view of nodes, ports, and connections for keyboard/screen-reader use. Selection is shared with the canvas and Properties. Reading order follows a stable documented node order, not cable-crossing geometry. Port details include direction, rate, channel shape, and connected endpoints.

Arrow-key movement applies only in an explicit Move Node mode, with an announced step size and an exit command. Outside that mode, arrows navigate the logical structure. This avoids making ordinary accessibility navigation accidentally rearrange a patch.

Canvas zoom affects the drawing, not application text size. Provide Fit Patch and a displayed zoom value. Do not require a mouse wheel, middle-button pan, pinch gesture, or precise cable hit target for essential actions.

Deletion names what will be removed, including attached connections. Ordinary deletion is undoable. Unsupported graph changes show localized diagnostics and an explicit inactive state; the last valid graph may remain audible only under the DSP recovery policy.

## Toolbox

Provide searchable local components grouped by useful roles such as generation, shaping, level, and output. Exact categories and inventory follow product design. Each result includes a readable name, short purpose, port/rate summary, and local help.

Search works without a network connection. Empty results suggest local terms/categories, not online installation. Distinguish built-in available components from missing project definitions; do not offer a placeholder that silently changes sound.

Double-click/Enter on a result, or an Add action, inserts it into the current patch. Return focus to the inserted node and announce its name. Bundled examples are available through File/Open Example and local Help, not mixed into a marketplace.

## Properties panel

Show properties for the selected project, node, or connection. With nothing selected, show concise guidance rather than a blank inspector.

Separate identity/layout properties, editable parameters, read-only port information, and diagnostics. Parameter controls show value, unit, range, default/reset action, and a numeric editing alternative to knobs/sliders. Label actions and explain disabled controls.

Validate entries before commitment. Keep an invalid numeric draft visible with an inline explanation; do not silently clamp it, change the last valid DSP value, or trap focus. Escape cancels the draft, and explicit correction/commit updates the accepted value.

For continuous gestures, provide responsive qualified live updates but make the entire drag one undoable edit. This does not imply sample-accurate UI timing. Preparation-dependent edits display Pending/Inactive until activation succeeds.

When qualified automation is present, distinguish base, automated target, live override, and effective value according to the DSP design. Do not promise automation lanes or recording in the MVP. Reset and override release are different actions and must be labeled accordingly.

## Project explorer

Represent the current local project, its current patch, its contained component instances, and project diagnostics. Selecting a component locates/focuses it on the canvas or Structure List. Selecting a diagnostic reveals the affected node/port and recommended action.

Do not imply a file-per-node model, multiple patches, or source folders before the project format supports them. Future reusable modules and VAL Script definitions appear only when their capabilities are approved and available; do not show permanently empty advanced folders to beginners.

Clearly distinguish project identity from the machine's device preference. On cross-platform open, preserve patch meaning and present output reassignment as an audio setting, not a corrupt project.

## Audio settings

Provide a named dialog/panel accessible from the toolbar, Audio menu, and a visible status link.

**Windows:** Show ASIO as the primary path and require selection of a suitable installed driver/output. If none is available, show the prerequisite and keep audio stopped while allowing editing/saving/local help. Never select an arbitrary driver or imply that built-in outputs necessarily work. WASAPI Shared/Exclusive are possible future compatibility options, not required choices or an automatic fallback; show them only if separately approved and delivered.

**macOS:** Show CoreAudio and available outputs. Do not display Windows-only modes or claim equivalent exclusivity semantics.

Separate requested and effective device, sample rate, channel mapping, and buffer/period information. Show unsupported choices and reasons where known. Distinguish nominal period, backend-reported latency estimate, and a measured result; never label a buffer duration as total audible latency.

Apply validates and prepares settings outside audio processing, keeping slow work off the UI where platform/JUCE affinity permits. Changing a running configuration shows that audio will stop and requires confirmation; application succeeds only when the effective result is known. Cancel discards unapplied drafts, not the user's saved patch. A configuration preview must not unexpectedly start sound.

Explain driver/device access restrictions where applicable. If a future WASAPI Exclusive mode is introduced, warn about coexistence and offer only delivered alternatives, not a promised Shared fallback. On unavailable device or failed open, show the reason and recovery actions; do not silently switch mode/output or resume through speakers after unplugging headphones.

### Wireframe: audio settings

```text
+---------------------------------------------------------------+
| Audio Settings                                                |
| Platform: Windows                                             |
| API / mode: [ASIO                         v]                   |
| Output:     [Selected installed interface v]                   |
| Requested rate: [48000 v]   Buffer: [128 frames v]             |
| Output mapping: [Left -> 1] [Right -> 2]                       |
|                                                               |
| Effective: Stopped; no active stream                           |
| Last reported output latency: unavailable                      |
| Note: Actual values are confirmed when the stream opens.       |
|                                                               |
| [Cancel] [Apply]                Apply does not start audio.     |
+---------------------------------------------------------------+
```

Native button ordering and control behavior follow each platform. Values are examples, not promised device capabilities.

## Signal inspector

The initial inspector presents the selected port/connection name, direction/rate/channels, relevant values/units, actual audio state, and observation availability. For signal data not supplied by the initial runtime, say "Not available" rather than invent a reading.

An unavailable observation, stopped graph, disconnected port, and actual zero signal are distinct states. Selection changes the inspected target, not signal routing. Opening a visualizer must not make a disconnected branch start processing or otherwise change audible behavior.

Waveform, spectrum, peak/RMS displays, freezing traces, and more elaborate measurement are optional discovery scope, not implied MVP functions. If introduced, pair displays with accessible numerical summaries, units, scale descriptions, and manual/low-rate announcements.

Visualization updates are throttled off the audio callback and may skip display frames under load. A slow or collapsed inspector must never stall sound. Do not announce every sample or rely on moving graphics to communicate a fault.

### Wireframe: inspection and revision feedback

```text
+------------------------------------------------------------------+
| Signal Inspector: Level.Out                                      |
| Port: Audio output, mono | Connected to: Output.Left               |
| Audio: Running | Observation: Not available in this configuration  |
|                                                                  |
| [Optional future trace area; not an MVP commitment]                |
| [Local explanation] [Locate node]                                 |
+------------------------------------------------------------------+
| Patch edit: Inactive - incompatible connection                    |
| Audible: Previous valid revision | [Locate issue] [Stop Audio]     |
+------------------------------------------------------------------+
```

## End-to-end workflows

| Workflow | User path | Required outcome and recovery |
|---|---|---|
| First sound from an example | Open a bundled local example, read brief guidance, choose/confirm output, review listening-level guidance, then Start Audio. | A complete generation/level/output path is understandable. No login or download is required; failure opens actionable settings, not a silent fallback. |
| Build from a blank patch | New Project, Add Component through toolbox, set parameters, connect ports, validate, then Start. | Keyboard and pointer paths reach the same accepted patch; invalid wiring is explained without losing edits. |
| Explore a sound | Select node, adjust a parameter or type its value, inspect state, undo the gesture if needed. | UI remains responsive; accepted and effective values/state are distinguishable. Expensive changes show pending status. |
| Connect without dragging | Select node/port in Structure List, invoke Connect, select a named compatible destination, confirm. | Both endpoints and validation outcome are announced; Cancel preserves the previous graph. |
| Save and continue offline | Save locally, modify, close with Save/Discard/Cancel, reopen. | Save failure retains the unsaved marker and offers retry/another destination. Only a confirmed successful snapshot is marked saved; newer edits stay unsaved. |
| Move Windows/macOS work | Open the supported project on the other platform, review missing definitions/compatibility, reassign local output, explicitly Start. | Supported topology/values remain intact; device differences never trigger silent component substitution. |
| Recover after output loss | Receive an audio-failed notification, inspect the reason, select output, apply, explicitly restart. | Preserve work and clearly stop sound; no automatic playback on a different physical device. |
| Resolve a failed live edit | Follow inactive-revision feedback to the connection/property, undo or correct it, wait for successful activation. | Show what is still audible; Save does not dismiss the processing failure or imply activation. |

Local help explains component purpose and the first-sound path. Optional online links are visibly labeled and never required to finish these workflows.

## Proposed keyboard shortcuts

This is a proposed default map, not a promise that all chords are available on every keyboard/layout. Use Ctrl on Windows and Command on macOS where indicated; macOS Control and Command are not interchangeable. Every shortcut has a labeled menu/button equivalent.

| Action | Windows | macOS | Scope and caveats |
|---|---|---|---|
| New / Open / Save | Ctrl+N / Ctrl+O / Ctrl+S | Command+N / Command+O / Command+S | Project window; native file-dialog shortcuts take precedence while a dialog has focus. |
| Save As | Ctrl+Shift+S | Command+Shift+S | Explicit destination selection. |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z | Command+Z / Command+Shift+Z | Focused text field uses text history; otherwise patch history. |
| Add Component / toolbox search | Ctrl+K | Command+K | Main workbench only; focuses search without modifying the patch. |
| Connect selected port | Ctrl+Shift+K | Command+Shift+K | Patch/Structure List context; opens destination chooser. |
| Focus Properties | Ctrl+Shift+P | Command+Shift+P | Does not commit an invalid draft automatically. |
| Start Audio | Ctrl+Enter | Command+Return | Main workspace; disabled when invalid/not ready. Never a play/stop toggle. |
| Stop Audio | Ctrl+Shift+Enter | Command+Shift+Return | Application-wide, including app-owned dialogs/editors; not a system-global hotkey. |
| Audio Settings | Ctrl+, | Command+, | Command+, belongs in macOS application Settings; Windows offers the same action through Audio. |
| Cycle workspace regions | F6 / Shift+F6 | F6 / Shift+F6 where available | macOS/laptop media keys may require Fn or conflict; use View/Focus Region when unavailable. |
| Local component help | F1 | Help menu | F1 may be a hardware key on macOS; no mandatory Fn shortcut. |
| Remove selected graph item | Delete | Backspace/Delete key | Graph context only; text fields retain editing behavior. |
| Cancel current interaction | Escape | Escape | Cancel provisional wiring, move mode, or draft; not an implicit Stop Audio command. |
| Navigate/activate | Tab, Shift+Tab, arrows, Enter | Tab, Shift+Tab, arrows, Return | Contextual standard controls; macOS keyboard-navigation preferences affect native focus behavior. |
| Fit Patch | View menu command | View menu command | No reserved default chord until keyboard/layout testing. |

Do not intercept OS accessibility shortcuts, Command+Space/Spotlight, Alt+F4, Command+Q, or screen-reader modifier combinations. Native quit/close still follows unsaved-work confirmation and controlled audio shutdown.

Before release, test the map on physical layouts, input methods, assistive technologies, and platform settings. Any conflicting default must have an accessible alternate/menu path. Shortcut customization/import is not a committed MVP feature.

**Stop limitations:** The dedicated chord is routed before ordinary app text commands, but cannot override OS-secure dialogs, another foreground app, a blocked UI process, or failed hardware. Keep a visible Stop action in app-owned audio/edit dialogs; do not describe the shortcut as guaranteed hearing protection.

## Accessibility

- **Keyboard completion:** Add, select, inspect, edit, connect, disconnect, move, delete, undo, save, configure output, start, and stop all have non-drag keyboard paths. No keyboard trap in the canvas or an invalid field.
- **Semantic graph alternative:** Expose Structure List hierarchy, names, directions, endpoint relations, connection counts, and selection through the accessibility layer. A custom-drawn cable alone is insufficient.
- **Predictable focus:** Clearly indicate focus separately from selection. Restore focus after dialogs, deletion, or closing a panel to a meaningful surviving item. Newly inserted nodes receive focus; removing a node announces its removal.
- **Understandable values:** Announce control labels, units, ranges, editable values, errors, and effective/override state when applicable. Sliders/knobs have numeric alternatives and do not require color or audio perception to interpret.
- **Status announcements:** Announce start/stop/fault and operation outcomes once at appropriate urgency. Keep a reviewable diagnostics area; throttle routine measurement announcements and never stream sample changes to a screen reader.
- **Visual access:** Support application text scaling, high contrast, visible cable/port focus outlines, and distinguishable non-color states. Recommend WCAG 2.2 AA contrast benchmarks for the interface, while recognizing that a desktop conformance claim requires broader validation.
- **Motion and hearing:** Honor reduced-motion preferences where available, allow pausing optional animation, avoid flashing fault indicators, and communicate audio activity textually. Do not rely on audible alerts or play unexpected test tones.
- **Pointer access:** Use forgiving port/connection targets and offer selection/connection dialogs. Do not make hover the only way to reveal help or essential controls.

Validate with Windows Narrator and macOS VoiceOver, keyboard-only users, scaling/high-contrast settings, and representative patch tasks. JUCE use alone is not proof of accessibility; component accessibility handlers, custom canvas semantics and platform behavior need hands-on testing.

## Future plugin UX boundaries

Architecture support is required, but these workflows are not implemented or assigned to Releases 0.1, 0.5, 1.0 or 2.0:

- **VST3 hosting:** If delivered, distinguish installed third-party processors from bundled components. Make discovery/scan failures, missing dependencies and trust/isolation limits visible; never scan/load binaries merely by focusing a toolbox result. Native plugin editors are separate windows with explicit lifecycle/focus behavior and cannot be assumed accessible or realtime-safe.
- **Plugin export:** If delivered, use an explicit validation/package workflow identifying eligible graph inputs/outputs, exposed parameters, resources, target format/platform and actionable failures. Export is distinct from Save and from audio-file export; no arbitrary-patch export or third-party binary bundling is promised.
- **External-host operation:** An exported plugin must process without an editor. If an editor is provided, show host-owned audio/transport state; do not offer ASIO/CoreAudio device selection or claim that standalone Start/Stop controls the host's hardware. Bypass/mute/stop semantics, UI reuse, host shortcuts and transport integration need separate design and qualification.

Keep all of this out of the first-sound path. Hosting/export must not introduce accounts, online licensing steps or code/toolchain prerequisites into the core standalone visual workflow.

## Scope and open decisions

The proposed 0.1/0.5 loop prioritizes the basic workbench, canvas/Structure List, toolbox, properties, local project navigation, explicit audio settings/status, and elementary inspection. Stable 1.0 strengthens clarity, compatibility, and accessibility evidence.

Reusable-module editing follows the PRD's proposed 2.0 discovery. Advanced signal displays, docking, multiple documents/audio sessions, automation lanes, and VAL Script editing require separate scope decisions. If script editing is introduced, show source/diagnostics and compiled-versus-active state without making code a prerequisite for patching.

Still unresolved: final component labels, exact panel sizing and small-window behavior, keyboard conflict resolution, platform support matrix, automation-control details, output-level safeguards, and supported assistive-technology coverage. Future plugin discovery/isolation messaging, export eligibility/formats/toolchain UX and external-host editor/control behavior require separate scope decisions.

Validate the workflows with all four target audiences before claiming that classic-IDE familiarity improves usability. This blueprint contains no source code, implementation scaffolding, or finished UI assets.
