# Visual Audio Lab — visual design language

**Status:** proposed product direction, not a description of a shipped redesign.
This document defines an audio-first visual system for future design and
implementation work. It does not change application code or claim user-testing
results.

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

The current desktop already has real graph editing, a searchable 14-entry
module catalog, parameter/script editing, native audio settings, explicit
transport, project persistence and live scope/spectrum/peak/raw-RMS views.
Its present visual structure still reads as a developer-style shell: a menu
strip, command toolbar, separate status/banner strips, left browser, bordered
canvas, right inspector, and a permanently docked bottom Audio Settings area.
Dock selectors and panel headings consume attention; the browser is a uniform
list; nodes use similar rounded rectangles; and the active signal view occupies
a small inspector region rather than leading the experience. The captured
running desktop shows a live selected oscilloscope, but large blank dock
surfaces and repeated borders frame it more strongly than the measurement.

That observation describes the prototype, not an evaluation with users.
The design below proposes a calmer, signal-forward hierarchy while preserving
access to the current functionality. Reverb-tail displays, compressor
gain-reduction traces, richer plugin-style editors, color-coded control/MIDI
graphs, workspace presets and a more adaptive docking model are **design
proposals**. They are not claims about current DSP telemetry or delivered
features.

The design borrows broad interaction principles—precise, visually legible
processor controls; immediate graph feedback; and instrument-like focus—from
the named audio products. It does not reproduce their proprietary layouts,
artwork, typography, curves, icons or signature visual treatments.

## 2. Color system

Color has separate jobs: **surface depth**, **signal semantics**, **interaction
state**, and **module identity**. Never ask one hue to communicate all four.
Dark does not mean monochrome: surfaces are neutral blue-charcoal; audio
measurements use warm gold; control information uses soft blue; MIDI uses
green; processor families add restrained edge accents; and errors remain
unmistakably red.

### Foundation tokens

| Token | Hex | Role |
|---|---|---|
| `canvas.deep` | `#0B1118` | Main visualization/canvas background; deepest surface. |
| `surface.panel` | `#121B24` | Default inspector, browser and shell surface. |
| `surface.raised` | `#1B2834` | Floating editor, selected row, raised card; use sparingly. |
| `surface.well` | `#0F171F` | Plot well, numeric readout well and text-entry field. |
| `line.subtle` | `#31475B` | Low-priority grouping boundary, not text. |
| `line.strong` | `#71869A` | Essential divider, control outline or graph support mark. |
| `text.primary` | `#F1F4F7` | Labels and readings on dark surfaces. |
| `text.secondary` | `#B2C0CC` | Supporting labels, units and explanations. |
| `text.disabled` | `#8493A2` | Inactive controls; pair with shape/state cues, not the sole explanation. |

### Signal, category and state tokens

| Token | Hex | Meaning and use |
|---|---|---|
| `signal.audio` | `#FFD166` | Audio signal, waveform, spectrum trace and active audio route. Warm amber/gold. |
| `signal.control` | `#8EC9FF` | Control-rate/modulation signal or control connection. Soft blue; distinguish from an audio wire with dash/shape. |
| `signal.midi` | `#8FE0B3` | MIDI/event route. Green; pair with pulse/dot and a label, never hue alone. |
| `family.source` | `#FFDA8A` | Source/generator identity rail and source glyph. |
| `family.dynamics` | `#F28E6B` | Dynamics family rail; coral is an identity color here, not a warning state. |
| `family.filter` | `#B7A1FF` | Filters/EQ family rail. |
| `family.modulation` | `#67D6C1` | Modulation family rail. |
| `family.delay` | `#6EB8C7` | Delay/time-based family rail. |
| `family.analysis` | `#8EC9FF` | Analysis instrument rail, with a meter/plot glyph. |
| `family.logic-script` | `#D5C4FF` | Logic and Script identity rail; pair with an explicit symbol/text. |
| `state.good` | `#8FE0B3` | Connected/ready/running state, only when confirmed. |
| `state.warning` | `#FFC078` | Degraded, stale or recoverable warning; label the reason. |
| `state.error` | `#FF8A8A` | Failure, invalid route or stopped-on-fault state. |
| `state.focus` | `#F4F7FA` | Keyboard focus ring. |
| `docs.neutral` | `#B2C0CC` | Documentation/help content; never imply that docs are a live signal. |

Family colors appear as small rails, corner marks or glyphs—not broad,
high-saturation card fills. Audio wires remain amber across the patch. A
selected signal route may brighten and widen, but must not be recolored as a
different signal type. State overlays (warning/error/stale) are orthogonal to
family identity: for example, an EQ can have a violet family mark and an amber
audio route plus an orange warning badge.

### Contrast and color-vision rules

Ratios below use WCAG relative luminance and are verified against the listed
dark backgrounds. Text pairings exceed 4.5:1; large labels and non-text
indicators still benefit from these strong pairings. `line.strong` is a
non-text boundary, not body copy.

| Foreground | `canvas.deep` `#0B1118` | `surface.panel` `#121B24` | `surface.raised` `#1B2834` |
|---|---:|---:|---:|
| `text.primary` `#F1F4F7` | 17.18:1 | 15.75:1 | 13.59:1 |
| `text.secondary` `#B2C0CC` | 10.21:1 | 9.36:1 | 8.08:1 |
| `signal.audio` `#FFD166` | 13.15:1 | 12.06:1 | 10.41:1 |
| `signal.control` `#8EC9FF` | 10.79:1 | 9.89:1 | 8.54:1 |
| `signal.midi` / `state.good` `#8FE0B3` | 12.17:1 | 11.16:1 | 9.63:1 |
| `state.warning` `#FFC078` | 11.79:1 | 10.81:1 | 9.33:1 |
| `state.error` `#FF8A8A` | 8.36:1 | 7.66:1 | 6.61:1 |
| `family.source` `#FFDA8A` | 14.13:1 | 12.95:1 | 11.18:1 |
| `family.dynamics` `#F28E6B` | 8.00:1 | 7.33:1 | 6.33:1 |
| `family.filter` `#B7A1FF` | 8.63:1 | 7.91:1 | 6.83:1 |
| `family.modulation` `#67D6C1` | 10.80:1 | 9.91:1 | 8.55:1 |
| `family.delay` `#6EB8C7` | 8.44:1 | 7.74:1 | 6.68:1 |
| `family.logic-script` `#D5C4FF` | 11.89:1 | 10.90:1 | 9.41:1 |
| `state.focus` `#F4F7FA` | 17.63:1 | 16.17:1 | 13.95:1 |

Color alone never distinguishes a route, category, selection, clipping or
staleness. Use words, glyphs, line patterns, port shapes, geometry and motion
with color. Supply a high-contrast mode that increases boundary and focus
weight without changing signal meaning. Validate final component states,
including disabled, hover, selected, error, and overlays, not just tokens.
Allow reduced-motion preferences; no essential diagnostic may be encoded only
in flashing or animation.

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
| Source | 14 / 20 px | VAL Script editing; line numbers and diagnostics preserve clear hierarchy. |

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
| Analysis (Oscilloscope, Spectrum Analyzer, Peak Meter, RMS Meter) | Blue instrument rail; recognizable scope/FFT/bar glyph; measurement is the body. | Scope shows the real bounded min/max envelope; Spectrum shows current worker result; meters show calibrated measured values. |
| Logic | Blue/neutral logic glyph; distinct control ports and dashed control wires. | No such module is currently in the catalog; reserved for future, explicitly typed nodes. |
| Scripts | Lavender `ƒ`/page glyph, not a terminal prompt; explicit input/output count and compile state. | No source code on the canvas; use the inspector/full editor. |
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

Signal is the hero, but only measured signal is hero. The four current
visualization products are calibrated around amplitude 1.0 full scale:

- **Oscilloscope:** display the true captured min/max envelope over a labeled
  time span and amplitude range. Mark trigger/source and channel.
- **Spectrum Analyzer:** show calibrated peak-amplitude dBFS against logarithmic
  frequency; state FFT size/bin spacing and distinguish the dominant bin from
  a guaranteed fundamental.
- **Peak Meter:** label **sample peak**, not true peak. Distinguish current
  display and held peak.
- **RMS Meter:** label **raw RMS**; full-scale sine reads -3.01 dBFS. Do not
  imply AES17 sine-reference offset or loudness/LUFS.

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

## 7. Workspace concepts

Workspaces change visible emphasis, panel arrangement and visualization scale;
they do not change DSP behavior, device configuration or project data unless
the user explicitly saves a layout. Preserve panel identity, selected source
and keyboard focus when switching. Beginners should not be trapped in a
simplified mode; expose an obvious “Show all”/workspace switch.

| Workspace | Main canvas and visible surfaces | Emphasis |
|---|---|---|
| **Beginner** | Patch Canvas, guided Module Browser, one large Signal Inspector, compact Properties. Audio Settings collapsed but reachable. | Short guided chain, clear source→processor→analysis→output examples, named units and “what changed?” explanations. No empty project panels dominating. |
| **Patch Design** | Wide Patch Canvas, compact searchable Module Browser, contextual Properties, optional Signal preview. | Routing and node grouping dominate; route highlight and quick-add are prominent. Device details remain in transport/status. |
| **Audio Analysis** | Large scope/spectrum/meter region, compact patch overview or selected-path strip, source selector and analysis controls. | Calibrated measurements, provenance and channel/time/rate context; graph remains available but secondary. |
| **DSP Debugging** | Patch Canvas plus persistent multi-view Signal Inspector, diagnostics rail and selected-node Properties. | Trace selected path, compare relevant observations, show gaps/stale/invalid states and node diagnostics. No claim of per-processor telemetry absent a contract. |
| **Mastering** | Large spectrum, peak and RMS views; restrained patch strip and selected processor editor. | Measurement confidence, sample-peak vs RMS definitions, sample rate/channel and output warnings. This is a future layout concept, not a mastering chain or true-peak/LUFS feature. |

### Proposed main workspace wireframe

```text
┌ Visual Audio Lab ──────────────────────────────────────────────────────┐
│ [Workspace ▾] [Patch ▾]                          ● LIVE 48 kHz / 128  │
│  setup / file / undo are quiet, compact actions      [Stop] [Audio…]  │
├───────────────────────────────────────────────────────────────────────┤
│                                                                       │
│   SIGNAL / selected measurement                           (Level 1)   │
│   ┌ Scope | Spectrum | Peak | RMS ────────────────────────────────┐  │
│   │ measured signal; scale, channel, units, live/stale provenance │  │
│   └───────────────────────────────────────────────────────────────┘  │
│                                                                       │
│   PATCH FLOW                                              (Level 2)   │
│   [Source] ─────── [Dynamics] ─────── [Filter] ─────── [Analysis]    │
│      selected signal path glows; unrelated patch stays quiet          │
│                                                                       │
├───────────────────────────────────────────────────────────────────────┤
│  Selected: Compressor · Threshold −18 dB · Ratio 4:1 · Bypass        │
│  [Properties] [Signal] [Help]                     Browser ▸  Devices ▸│
└───────────────────────────────────────────────────────────────────────┘
```

**Annotations:** (1) Signal receives the largest stable area and remains
available when the graph is dense. (2) Patch is a readable flow map, not the
only view of state. (3) Controls are contextual and small until selected.
(4) Device/project administration collapses to a compact status/action surface
and expands only on intent. (5) A clear, confirmed live/stopped indicator and
Stop remain visible; hiding device settings never hides safety-critical state.

Phase 1 Audio Setup follows this operation hierarchy: compact Start/Stop,
optional Refresh / Retry, driver-supported Control Panel, and persistent state
above endpoint/port choices. Backend selection enumerates automatically and
endpoint selection loads capabilities automatically; there is no Detect
banner/button competing with the signal workspace. Basic controls use actual
driver-labeled mono ports or contiguous stereo pairs, not first-N channel
counts. Advanced ports reveal wider contiguous groups without pretending to
offer arbitrary routing or mono-to-stereo engine conversion. Discovery does not
start playback; hardware-initialization warnings and errors remain visible in
the occasional setup dock. See [DesktopWorkflow.md](DesktopWorkflow.md).

## 8. Interaction, accessibility and performance rationale

- Prefer direct manipulation: drag a module onto the patch, connect typed ports,
  select a route, then adjust controls in context. Keep searchable browsing as
  an accessible alternative to drag-and-drop.
- Signal-route selection should show the upstream source and downstream
  destination, and allow return to the whole patch in one action. Do not reorder
  or edit a patch merely because the user inspects it.
- Put device scan/inspection behind explicit actions, state what those actions
  may do to a driver, and never autoplay on launch, project load or workspace
  switch. Audio is not “safe” merely because it is a visual laboratory.
- Use keyboard traversal parallel to pointer operation: stable focus order,
  named controls, Enter/Space activation, Escape to leave transient gestures,
  visible focus, and shortcuts discoverable in menus. Make graph operations
  possible without precise motor control.
- For screen readers, pair every plot with a concise textual summary, selected
  metric/value, units, validity, source and freshness. Provide a keyboard
  navigable measurement cursor/table; a bitmap plot alone is not accessible.
- Respect reduced motion and system scaling. Measurements must remain usable
  at 200% text scaling and at narrow/small-screen sizes; collapse side surfaces
  before shrinking plot labels or removing controls.
- Every high-rate visualization needs a bounded shared producer, offscreen
  suspension and a defined maximum refresh rate. User-visible animation is not
  permission to increase callback work.
- Use animation only to show verified change: meter decay, a new frame, active
  route or selection transition. Avoid blinking, infinite decorative pulses and
  fake activity when stopped or unavailable.

## 9. Design tradeoffs and open choices

**Signal-first versus patch-first.** A dominant plot improves observation and
teaching but consumes space needed to arrange large networks. The workspace
presets and adjustable signal/patch split let the user choose without forcing
one permanent composition.

**Color identity versus color accessibility.** Warm audio, blue control and
green MIDI make the graph easier to scan, but color perception varies and
routes can cross. Redundant line patterns, glyphs, labels and port geometry
preserve meaning without hue.

**Embedded visualization versus legibility/performance.** Miniature meters can
make the patch feel alive, but only a few can be read at a time and many
simultaneous displays cost GPU/CPU. Use selected/visible nodes, semantic zoom
and shared analyzer products; reserve full plots for the inspector/editor.

**Compactness versus discoverability.** Collapsing administration reduces
chrome but can conceal unfamiliar operations. Provide clear workspace names,
visible transport/setup affordances and an all-panels path; do not hide required
device setup behind undocumented gestures.

**Family color versus semantic state color.** A coral Dynamics rail must not
look like an error. Keep family marks small and stable; put status in a
separate icon, text badge and border, and check their composite contrast.

**Editor richness versus product truth.** Transfer curves and plugin-like
controls make a professional instrument, but require versioned parameter
metadata, correct units/ranges, well-defined telemetry, and explicit stopped/
stale behavior. Do not draw a sophisticated visual for data that the processor
does not actually expose.

**Design approval questions:** Should first launch open Beginner or Patch
Design? Should the large Signal region default to the selected analyzer or to
the last pinned source? Is the Mastering preset in scope before true-peak and
loudness measurement contracts exist, or should it remain a clearly labeled
measurement-only layout? These choices affect defaults and product promises,
not the proposed color/type tokens.

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
