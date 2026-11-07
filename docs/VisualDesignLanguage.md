# Visual Audio Lab — visual design language

**Status:** the palette and shell direction below are active implementation;
feature and future signal-semantics proposals are labeled separately. This
document is the visual-language reference, not a claim of user-testing,
hardware qualification or commercial-product parity.

## 1. Product premise: listen with your eyes

Visual Audio Lab is a **visual audio laboratory**: a place to shape a signal,
see how it behaves, and understand why. It is not an IDE with audio-themed
panels, nor a DAW that happens to contain a node graph. The signal is the
primary object; the patch is its map.

The interface should make three questions feel immediate:

1. **What is the signal doing?** Give measured waveform, spectrum and level the
   strongest visual presence.
2. **Where is it flowing?** Make sources, destinations and the selected path
   readable without asking the user to decode a graph of generic boxes.
3. **What changed it?** Put the selected processor's meaningful controls next
   to an honest before/after or input/output observation.

The emotional target is inviting, inspiring, educational, professional and
exploratory. A user should feel they are at a responsive instrument bench:
curious enough to experiment, safe enough to understand what Start will do,
and supported by measurements instead of unexplained decoration. Avoid the
corporate, administrative feeling of a grid of equal-weight panes and nested
tool chrome.

### Attention hierarchy

| Priority | Content | Visual treatment |
|---|---|---|
| 1 — Hear/see | Live or frozen signal, spectrum, sample peak/raw RMS, and explicitly available processor analysis | Largest, highest-contrast area; animated only while genuinely live and useful. |
| 2 — Follow | Patch routing and the selected signal path | Clear node silhouettes; highlighted upstream-to-downstream route; quiet unrelated graph. |
| 3 — Shape | Processor controls, bypass and supported live actions | Contextual, well-spaced controls with units, ranges and immediate measured response. |
| 4 — Operate | Device, file, project, workspace and diagnostics | Calm, compact access; promoted only when setup, error or recovery needs attention. |

This is an attention order, not a statement that configuration is unimportant.
An error or hazardous output condition must temporarily outrank a beautiful
plot. The hierarchy is designed to put the product's unique value—observable
signal behaviour—above persistent window administration.

### What the current prototype shows

The current desktop already has real graph editing, a searchable built-in
module catalog, parameter editing, native audio settings, explicit
transport, project persistence and live scope/spectrum/peak/raw-RMS views.
Its present visual structure still reads as a developer-style shell: a menu
strip, command toolbar, separate status/banner strips, left browser, bordered
canvas, and docked tools with controls for relocating panels. Dock selectors
and panel headings consume attention; the browser is a uniform list; nodes use
similar rounded rectangles; and the active signal view occupies a small
inspector region rather than leading the experience. The captured desktop shows
a live selected oscilloscope, but large blank dock surfaces and repeated
borders frame it more strongly than the measurement. The screenshot is dated
test evidence; it is not a live usability or physical-device audit.

That observation describes the prototype, not an evaluation with users.
The design below proposes a calmer, signal-forward hierarchy while preserving
access to the current functionality. Reverb-tail displays, compressor
gain-reduction traces, richer plugin-style editors, color-coded control/MIDI
graphs and workspace presets are **design proposals**. They are not claims
about current DSP telemetry or delivered features. The shell direction is now
fixed: the patch canvas remains visible and cannot collapse; eligible side
panels may collapse and reopen in place; panels do not dock, float or
re-dock. Audio Setup is a dedicated dialog opened from the Audio menu.

The design borrows broad interaction principles—precise, visually legible
processor controls; immediate graph feedback; and instrument-like focus—from
the named audio products. It does not reproduce their proprietary layouts,
artwork, typography, curves, icons or signature visual treatments.

## 2. Color system

Color has separate jobs: **surface depth**, **signal semantics**, **interaction
state**, and **module identity**. Never ask one hue to communicate all four.
The approved palette uses neutral charcoal surfaces, cyan audio identity,
restrained semantic accents, crisp geometry and local, optional glow.

### Foundation tokens

| Token | Hex | Role |
|---|---|---|
| `background` | `#1B1D20` | Deep neutral shell and patch-canvas foundation. |
| `surface.panel` | `#262A2D` | Default browser, inspector and panel surface. |
| `surface.raised` | `#30363A` | Raised rows and transient controls; pair with clear boundaries. |
| `surface.input` | `#17191C` | Text-entry wells and plot backgrounds. |
| `border.structural` | `#73808A` | Crisp panel, control and graph boundary; not body text. |
| `border.required` | `#91AAB2` | Required/invalid boundary paired with explicit text or state. |
| `text.primary` | `#F2F1EE` | Primary text and readings. |
| `text.secondary` | `#B7BDC1` | Supporting labels, units and instructions. |
| `text.muted` | `#9AA3A9` | Lower-priority metadata and stale readings on approved surfaces. |
| `text.disabled` | `#7B848A` | Disabled control only, with non-color state cues. |

### Signal, category and state tokens

| Token | Hex | Meaning and use |
|---|---|---|
| `signal.audio` | `#00C2D1` | Audio ports, routes, waveforms and measurements; label direction and channel. |
| `signal.midi` | `#CC5EFF` | Reserved event identity; use only if MIDI support ships. |
| `signal.control` | `#E4793A` | Reserved modulation/control identity; never warning or success. |
| `analysis` | `#9DDC4A` | Analyzer trace and valid measurement identity, not generic “ready” status. |
| `state.selected` | `#F39A57` | Crisp selection outlines, handles and compact markers; not broad fills or signal recoloring. |
| `state.warning` | `#D7A93F` | Warning icon/edge with labeled diagnostic; never the sole cue. |
| `state.error` | `#D45353` | Error icon/marker and crisp accent, not normal-size text on the panel. |
| `state.errorText` | `#FF8A8A` | Optional contrast-validated error copy. |
| `state.hover` | `#343B40` | Neutral hover surface. |
| `state.pressed` | `#3A4247` | Neutral pressed surface. |
| `state.focus` | `#F2F1EE` | High-contrast keyboard focus ring, geometrically distinct from selection. |
| `text.onAccent` | `#121416` | Foreground when a bright semantic accent is used as an opaque button fill. |

The JUCE theme centralizes these roles in `ShellTheme`. The current shipped
graph uses cyan for audio wires and ports, orange for selection geometry, and
green for analyzer identity. MIDI and control are reserved and are not applied
to imply unavailable capabilities. Analyzer green is not a generic success
color; warnings and errors use their own tokens and explicit diagnostics.

### Contrast and color-vision rules

Ratios use opaque sRGB relative luminance and are covered by focused
automated theme checks. WCAG AA reference targets are **4.5:1 for normal
text** and **3:1 for large text and necessary non-text graphics**. Decorative
separators are not required to meet a threshold, but must not be the only way
to locate an interactive control.

| Foreground / boundary | Surface | Ratio | Treatment |
|---|---|---:|---|
| `text.primary` `#F2F1EE` | `surface.panel` `#262A2D` | 12.81:1 | Normal text. |
| `text.secondary` `#B7BDC1` | `surface.raised` `#30363A` | 6.45:1 | Supporting copy. |
| `text.muted` `#9AA3A9` | `surface.raised` `#30363A` | 4.77:1 | Lowest normal-text pairing; validate rendered scale. |
| `signal.midi` `#CC5EFF` | `surface.panel` `#262A2D` | 4.56:1 | Barely passes; never dim without revalidation. |
| `state.error` `#D45353` | `surface.panel` `#262A2D` | 3.56:1 | Icon/edge only, not normal text. |
| `state.errorText` `#FF8A8A` | `surface.panel` `#262A2D` | 6.38:1 | Optional error copy. |
| `border.structural` `#73808A` | `surface.raised` `#30363A` | 3.02:1 | Necessary non-text boundary; use with shape/spacing. |
| `text.onAccent` `#121416` | `state.error` `#D45353` | 4.54:1 | Dark text on an opaque destructive button if used. |

The panel/background pairing is only 1.17:1, so structure relies on spacing
and crisp borders, not the slight surface change alone. Do not put normal-size
text over translucent colors or glow. Accent-filled buttons use the dark
foreground; off-white text does not sit on bright accent fills.

Selection is geometry first: a selected node has a crisp orange outline and
corner handles; a selected cable has an orange outer stroke while its cyan
audio core remains visible. Keyboard focus adds a separate high-contrast
outline. Hover and pressed states use neutral surface shifts, not signal hue.
These distinctions remain available when glow is disabled.

### Restrained glow and visual effects

Selected nodes may use three local nested strokes extending at most 9 logical
pixels, with 0.12/0.16/0.20 opacity. The crisp outline and handles carry the
meaning without glow. `View > Reduced effects (no glow)` disables this optional
decoration for the current run. No blurred panel effects, glowing text,
blinking, continuous pulse or audio-callback work is used. Meter/scope
animation remains tied to measured live data; stale and stopped data are
labeled rather than made to look live.

Color alone never distinguishes route, category, selection, clipping or
staleness. Use words, glyphs, line patterns, port shape, geometry and position
with color. Keep the shell neutral; use semantic color only for implemented
meaning and local interaction feedback. Allow reduced-motion preferences; no
essential diagnostic may be encoded only in flashing or animation.

## 3. Typography and spatial rhythm

Use the platform's high-quality UI sans for labels and instructions; use a
tabular-figure-capable sans for measurements so meter values do not jitter.
Expose frequency, time, gain and dB values with unit suffixes and consistent
precision. Use a monospaced face only for source code and exact technical
identifiers, not for the whole application. Do not copy a named brand's
distinctive wordmark or type treatment.

| Style | Size / leading | Use |
|---|---:|---|
| Display reading | 28 / 34 px | Dominant dB, frequency or transport reading when that measurement is the view's purpose. |
| Section title | 20 / 26 px | Selected processor/editor title. |
| Subsection | 16 / 22 px | Controls and visualization group names. |
| Body | 15 / 21 px | Instructions and primary controls. |
| Supporting | 13 / 18 px | Units, secondary controls and status detail; do not shrink dense copy below this. |
| Source | 14 / 20 px | Technical text and identifiers preserve a clear hierarchy. |

Use a 4 px base spacing rhythm: **4, 8, 12, 16, 24, 32, 48 px**. Keep 8 px
between a label and its control, 16 px between related controls, 24 px between
sections, and 32 px around major visualization groups. Use a minimum 40 px
pointer target (44 px preferred on touch-oriented hardware), with keyboard
focus bounds larger than small graph handles. Avoid ornamental frames around
every component; reserve a line for meaningful grouping or interaction state.

## 4. Node identity and signal routing

Nodes are instruments in a signal chain, not generic application windows. A
node's silhouette should communicate its role before the user reads its name.
Retain a consistent input/output grammar and stable hit areas, then distinguish
categories using icon shape, port treatment, one family rail and a category
caption. Never depend on color alone.

### Shared anatomy

- **Identity cap:** family glyph and name; optional short model/variant label.
  Avoid exposing internal IDs in the default view.
- **Signal body:** a live miniature only where there is real data and it is
  relevant to the module. Otherwise show a calm, labeled idle state—not a fake
  waveform.
- **Ports:** visibly different source/output and sink/input shapes; audio,
  control and MIDI routes vary by line pattern and inline type label.
- **Quick controls:** at most one or two frequently adjusted parameters in the
  compact node. Do not turn the graph into a spreadsheet of sliders.
- **State footer:** confirmed live/stopped/stale/error state and bypass. More
  detail belongs in the inspector.

### Category anatomy

| Family | Proposed visual identity | Compact-canvas behaviour |
|---|---|---|
| Audio Source Nodes (Input, future Generator) | Warm source glyph; stable source/output port on the right; left edge communicates external input, or no input for a generator. | Show input activity only when backend telemetry exists; otherwise clearly show stopped/unavailable. |
| Dynamics (Compressor) | Coral rail; detector/transfer icon; threshold and ratio as compact values. | Future gain-reduction trace only when the processor exports real reduction data; do not infer it from output level. |
| Filters (EQ) | Violet rail; frequency-curve glyph; compact frequency/gain/Q values for the selected band. | A small actual response curve is a proposed capability; it requires exposing coefficient/response data. |
| Modulation (Chorus and future modulators) | Teal rail; periodic glyph with restrained phase cue. | Show wet mix/rate values; do not animate an LFO phase that the module does not publish. |
| Delay | Cyan rail; echo/tap glyph and time/feedback values. | Optional real tap/feedback visualization only if bounded state is provided; avoid a decorative repeating waveform. |
| Analysis (Oscilloscope, Spectrum Analyzer, Peak Meter, RMS Meter, Guitar Tuner) | Blue instrument rail; recognizable scope/FFT/bar/tuning glyph; measurement is the body. | Scope shows the real bounded min/max envelope; Spectrum shows current worker result; meters show calibrated values; tuner shows its measured note, cents and confidence. |
| Logic | Blue/neutral logic glyph; distinct control ports and dashed control wires. | No such module is currently in the catalog; reserved for future, explicitly typed nodes. |
| Audio Output | Destination glyph and left-side input; port shape distinguishes sink. | Show confirmed route/level/over-range state only if measured. |

### Zoom and density

1. **Overview:** category glyph, node label and route only; maximize patch legibility.
2. **Working zoom:** selected nodes reveal their compact meter/miniature and
   one or two fast controls. Non-selected nodes remain quieter.
3. **Detail zoom:** show ports, labels, parameter values and optional embedded
   analyzer display. Hide detail below a threshold rather than rendering
   unreadable text.

At all zoom levels preserve route continuity and selection identity. A lasso,
focus ring or selected-route halo must not hide a port or waveform. For large
patches, draw a signal-flow highlight through only the relevant upstream/
downstream path and fade—not remove—other routes. Large-patch mode should
prioritize stable overview, culling and progressive detail over simultaneous
animation of every node.

## 5. Living visualizations and truthful states

Signal is the hero, but only measured signal is hero. The scope, spectrum and
meters use amplitude 1.0 as digital full scale; the tuner separately reports
pitch against its declared A4 reference:

- **Oscilloscope:** display the true captured min/max envelope over a labeled
  time span and amplitude range. Mark trigger/source and channel.
- **Spectrum Analyzer:** show calibrated peak-amplitude dBFS against logarithmic
  frequency; state FFT size/bin spacing and distinguish the dominant bin from
  a guaranteed fundamental.
- **Peak Meter:** label **sample peak**, not true peak. Distinguish current
  display and held peak.
- **RMS Meter:** label **raw RMS**; full-scale sine reads -3.01 dBFS. Do not
  imply AES17 sine-reference offset or loudness/LUFS.
- **Guitar Tuner:** show equal-tempered note and octave, signed cents, an
  independent centered marker, confidence and explicit silence/uncertain/stale
  states. A4=440 Hz is the reference; only a stable monophonic estimate may
  produce an in-tune indication (within +/-5 cents). Never substitute the
  Spectrum Analyzer's dominant bin for a pitch estimate.

Every view has named states with both color and text/icon:

| State | Presentation |
|---|---|
| No source / not scheduled | Neutral empty plot, “Select a running source”; never a zero trace. |
| Waiting / warming up | Subtle progress indicator and required contiguous coverage; no implied current measurement. |
| Live | Confirmed source plus rate, channel, epoch/revision provenance as appropriate; restrained motion. |
| Stale | Preserve last measured result, dim it, show “Stale” and age; never animate it as live. |
| Degraded / drops | Keep valid data but add a warning and capture-gap/drop count. |
| Stopped / removed | Clear current value or clearly freeze the last result with its timestamp; explain cause. |
| Invalid / unsupported | Error or neutral label with a reason; do not substitute a different source or metric. |
| Frozen | Clearly mark frozen and pause live-demand work. |

Proposed Reverb and Dynamics visualizations require actual DSP contracts.
A future reverb tail display needs a well-defined input excitation or captured
decay response, time reference, channel selection and lifetime; it cannot be
derived from the existing analyzer product. Compressor gain reduction needs
per-block or bounded per-sample detector/gain-reduction telemetry with units,
timestamp/revision, aggregation and drop semantics. EQ response needs published
coefficients/response. Until those contracts exist, show only current truthful
controls and ordinary signal capture.

Use reduced motion to replace scrolling traces, flashing peaks or pulsing
connections with a static snapshot plus a discrete “live” mark and numeric age.
Animation must respect configured refresh caps and offscreen demand suspension.
Do not increase capture or worker load simply because a view is visible at a
small size.

## 6. One module, three depths of interaction

Every module should preserve identity and parameter meaning across:

1. **Compact Canvas Node:** signal role, name, live miniature when truthful,
   bypass and at most one/two quick parameters.
2. **Inspector View:** contextual parameters with units/ranges, bypass, signal
   data and concise explanation. Selection may retarget it; pin explicitly
   separates it from patch selection.
3. **Full Editor Window:** optional larger, non-modal surface for detailed
   visual design, comparison and fine adjustment. It must not be a separate
   processor or independent source of truth.

Values edited in one surface mirror in the others. Dirty/invalid values remain
visible and are not silently applied. A full editor should return focus to the
patch and selected module when closed; multiple editors should not multiply
analysis consumers. Setup that can be handled inline must remain inline rather
than becoming a modal-dialog workflow.

### Proposed Reverb editor

A future Reverb editor could emphasize a measured tail/decay, early/late
balance, predelay, decay time and damping. These graphics are proposals, not
currently published reverb telemetry. A static response preview may be
computed off the audio callback from well-defined parameter data; a live decay
plot must be backed by an explicit capture/test protocol.

```text
┌ Reverb · Hall A ─────────────────────────── [Bypass] [Pin] ─┐
│                                                            │
│  [Tail / decay view — proposed data contract]               │
│  time →         early reflections   ···   decay             │
│  Input  ──┐                         [measured/frozen state] │
│           └─ [Predelay] [Decay] [Damping] [Width] ── Output │
│  Mix  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ 32%   │
│  Status: Stopped — view shows saved design parameters       │
└────────────────────────────────────────────────────────────┘
```

### Proposed Compressor editor

Use a clearly labeled transfer-curve diagram with input/output axes, threshold
and ratio, plus attack/release/knee controls and a gain-reduction history only
if the engine exposes actual gain-reduction data. Use a horizontal reduction
scale in dB with unity/no reduction explicitly marked. Do not present output
level as gain reduction.

```text
┌ Compressor · Bus 1 ──────────────────────── [Bypass] [Pin] ─┐
│ Transfer curve (input dB → output dB) │ Gain reduction (dB) │
│  output                              │  0 ───────────────   │
│    ╱  [threshold handle]             │    measured history │
│   ╱                                   │    if available      │
│  input                                │  -12 ─────────────  │
│ Threshold [−18 dB]  Ratio [4:1]  Knee [6 dB]                │
│ Attack [10 ms]  Release [120 ms]  Makeup [0 dB]             │
│ State: no reduction telemetry — controls only               │
└────────────────────────────────────────────────────────────┘
```

### Proposed Spectrum editor

Expand the existing calibrated analyzer rather than invent a second FFT.
Prioritize the spectrum and frequency ruler; expose FFT size, averaging,
window, channel, peak hold and refresh beside it. Show cursor frequency and
level with both numeric readout and accessible keyboard navigation. Advanced
PSD/ENBW, sample statistics and provenance can live in a collapsible
measurement rail. Retain the current peak-amplitude dBFS definition; do not
label it PSD or loudness.

```text
┌ Spectrum Analyzer · Source 3 ─────────────── [Pin] [Freeze] ┐
│ −0 dBFS ───────────────────────────────────────────────     │
│             ╭─ actual shared FFT result ─╮                  │
│ −60 dBFS ───╯                            ╰────────────      │
│ −120 dBFS ─────────────────────────────────────────────     │
│ 20 Hz       100 Hz       1 kHz       10 kHz       20 kHz    │
│ Cursor: 997.6 Hz · −12.4 dBFS · bin 2048 / 23.44 Hz         │
│ Window [Hann] FFT [2048] Average [100 ms] Channel [1]        │
│ Peak amplitude dBFS · dominant bin, not guaranteed fundamental│
└────────────────────────────────────────────────────────────┘
```

All diagrams are original text sketches for hierarchy and information
relationships, not pixel-perfect mocks.

## 7. Fixed workspace and panel behavior

The shell is a stable working instrument, not a freely rearranged collection
of docks. Keep the Patch Canvas visible, non-collapsible, and large enough to
edit at the minimum supported window size. Place the Module Browser and
Property Inspector in fixed side regions; allow eligible side regions to
collapse/reopen in place with a small icon button. The button must have a
visible focus state, an accessible name that changes between “Collapse …”
and “Show …”, and a tooltip. Do not imply dragging, docking, tab relocation or
floating. Preserve bounded panel resizing only where it does not recreate
docking behavior.

Audio Setup is occasional configuration, not a persistent panel. Open its
dedicated dialog from **Audio > Audio Setup** only. The main shell may show a
compact passive status such as stopped, running, setup required or fault; it
must not duplicate the setup action or imply that an available configuration
means playback is active. Start is always explicit; Stop remains findable.
Dialog open/close, project load, discovery, graph editing and panel visibility
never start audio.

### Fixed-shell composition

```text
┌ Visual Audio Lab ──────────────────────────────────────────────────────┐
│ [Project ▾] [Patch ▾]           Stopped · output configured [Start]   │
├───────────────────────────────────────────────────────────────────────┤
│ [Module Browser]  │ PATCH CANVAS (always visible) │ [Properties]     │
│ Search / categories│ selected route is legible     │ context controls  │
│                    │ nodes enter by select or drag │ [Signal]          │
│                    │                               │                   │
└───────────────────────────────────────────────────────────────────────┘
```

The wireframe is a structural proposal, not a pixel-complete design. A
selected signal view may occupy a contextual inspector tab or a separate
existing visualization surface; do not make the patch canvas disappear.
When a side panel collapses, its control remains at the same shell edge and
the canvas receives the released space. No main-view Audio Setup control is
shown; the Audio menu remains its sole entry.

The Audio Setup dialog follows a conventional device-configuration hierarchy:
backend and endpoint choices, automatically discovered capability controls,
current discovery/error state, and explicit transport actions outside the
dialog. Discovery remains asynchronous and errors remain actionable. Dialog
visibility is not transport state. See
[DesktopWorkflow.md](DesktopWorkflow.md).

## 8. Interaction, accessibility and performance rationale

- Prefer direct manipulation: drag a module onto the patch, connect typed ports,
  select a route, then adjust controls in context. Keep searchable browsing as
  an accessible alternative to drag-and-drop.
- Signal-route selection should show the upstream source and downstream
  destination, and allow return to the whole patch in one action. Do not reorder
  or edit a patch merely because the user inspects it.
- Keep device scan/inspection asynchronous, state what native probing may do
  to a driver, and never autoplay on launch, project load or dialog open. Audio
  is not “safe” merely because it is a visual laboratory.
- Collapsing a side panel only changes visibility, never its persisted content,
  graph state or transport. The canvas cannot be collapsed.
- Use keyboard traversal parallel to pointer operation: stable focus order,
  named controls, Enter/Space activation, Escape to leave transient gestures,
  visible focus, and shortcuts discoverable in menus. Make graph operations
  possible without precise motor control.
- For screen readers, pair every plot with a concise textual summary, selected
  metric/value, units, validity, source and freshness. Provide a keyboard
  navigable measurement cursor/table; a bitmap plot alone is not accessible.
- Respect reduced motion and system scaling. Measurements must remain usable
  at 200% text scaling and at the agreed minimum window dimensions; collapse
  side surfaces before shrinking plot labels or removing controls.
- Every high-rate visualization needs a bounded shared producer, offscreen
  suspension and a defined maximum refresh rate. User-visible animation is not
  permission to increase callback work.
- Use animation only to show verified change: meter decay, a new frame, active
  route or selection transition. Avoid blinking, infinite decorative pulses and
  fake activity when stopped or unavailable.

## 9. Design tradeoffs and open choices

**Signal-first versus patch-first.** A dominant plot improves observation and
teaching but consumes space needed to arrange large networks. Keep the patch
canvas permanently visible and let contextual signal inspection emphasize
measurements without replacing the graph.

**Color identity versus color accessibility.** Audio cyan and analyzer green
make current signal meaning easier to scan, while MIDI and control remain
reserved. Selection orange is close to the reserved control orange, so
selection must use outlines/handles and focus a separate high-contrast ring.
Redundant labels, line geometry and port shapes preserve meaning without hue.

**Embedded visualization versus legibility/performance.** Miniature meters can
make the patch feel alive, but only a few can be read at a time and many
simultaneous displays cost GPU/CPU. Use selected/visible nodes, semantic zoom
and shared analyzer products; reserve full plots for the inspector/editor.

**Compactness versus discoverability.** Collapsing side panels reduces chrome
but can conceal unfamiliar operations. Provide clear accessible collapse
controls, visible transport state and Stop, and keep Audio Setup discoverable
as a named Audio-menu item.

**Signal color versus semantic state color.** Analyzer green must not look
like generic success, and warning/error cannot borrow audio or control colors.
Keep signal marks small and stable; put status in a separate icon, text badge
and border, and check their contrast on the rendered surface.

**Editor richness versus product truth.** Transfer curves and plugin-like
controls make a professional instrument, but require versioned parameter
metadata, correct units/ranges, well-defined telemetry, and explicit stopped/
stale behavior. Do not draw a sophisticated visual for data that the processor
does not actually expose.

**Design approval questions:** Which eligible side panels should start open?
Should the Property Inspector remember its Properties or Signal tab? What is
the minimum supported window size for acceptance? These choices affect
defaults and density, not the color/type tokens.

## 10. Scope boundary

This is a visual language and interaction architecture proposal. It does not
claim that the current application has drag-out floating module editors,
workspace presets, category-specific node rendering, reverb tail analysis,
compressor gain-reduction telemetry, true-peak/LUFS mastering meters or
screen-reader plot navigation. The shipped scope and verified workflows remain
documented in [DesktopWorkflow.md](DesktopWorkflow.md) and
[SignalVisualization.md](SignalVisualization.md). Implementation proposals
should be split into testable UI and measurement contracts rather than
represented by static mock telemetry.
