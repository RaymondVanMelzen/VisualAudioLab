# Signal Generator UX Redesign

**Status:** implementation reference for the shipped task editors. The current
Signal Generator and Noise Generator expose task-oriented controls, shape-only
previews and measured generator-node Peak/RMS readouts through the existing
capture worker. The remaining wireframes describe the intended hierarchy and
copy; they do not claim every refinement below is implemented. The
signal-generation and measurement capabilities described as current below
come from
[`modules/SignalGeneratorToolkit.md`](modules/SignalGeneratorToolkit.md) and
the current editor. User-confirmed level semantics are implemented and
documented in
[`GeneratorLevelPolicy.md`](GeneratorLevelPolicy.md); those semantics are a
design decision applied while preserving saved parameter keys and values.
Newly inserted Signal Generator and Noise Generator nodes start at an adjustable
-6 dBFS peak target; existing projects retain saved levels and legacy fallback
behavior.

## Design intent

Make a generator feel like a small, capable instrument rather than a dump of
processor metadata. In ten seconds, a musician should be able to choose a
signal goal, set its defining musical or test value, understand whether it will
reach an output, and see an honest preview of what is happening.

The editor uses four focused layouts—**Fixed Frequency**, **Sweep**, **Musical
Sweep**, and **Noise Generator**—reached from a five-goal chooser. The
**Calibration Signal** goal is a guided entry into the fixed-tone or noise
layout, not a fifth DSP mode or a claim that the app calibrates an analog
system. Once a user chooses a goal, keep that task and its few primary controls
in the foreground; move implementation state into closed disclosure sections.

The visual language is the existing charcoal instrument bench: signal cyan
for audio and plots, neutral raised surfaces, crisp structural boundaries,
large tabular values, and restrained local glow only for genuine focus/live
emphasis. The editor adapts general principles from contemporary synthesizer
interfaces—zoned control groups, persistent value labels and prominent signal
visuals—without copying any named product's layout, artwork, curves, icons or
signature styling. Keep the patch canvas visible and fixed; the Signal
Inspector remains a separate floating window rather than restoring docking.

## What this implementation ships

- Signal Generator now opens with a five-goal chooser: Fixed tone, Frequency
  sweep, Musical sweep, Noise and Calibration signal. Choosing a goal changes
  the saved DSP mode/waveform through the existing validated edit path; it
  never starts transport or alters routing.
- Fixed Frequency, Sweep, Musical Sweep and Noise layouts expose their
  task-defining controls with human labels. The Calibration signal goal
  requires a second choice between a 1 kHz sine and Pink-noise reference; these
  are digital source presets, not analog calibration.
- Both Signal Generator and standalone Noise Generator have shape-only
  waveform/trajectory previews and a two-bar meter with numeric sample-peak
  and RMS values. Measurements come from the captured generator node output,
  before downstream processors; no capture yields an explicit waiting,
  stopped, stale or unavailable state.
- Existing metadata rows and transient parameter/automation controls begin
  collapsed behind separate **Advanced** and **Automation** disclosures.
  **Developer** reveals raw node/parameter identifiers and engine ranges.
  Existing saved values, automation mappings, mute/transport behavior and
  legacy Noise Generator key `audio.param.levelDbRms` are retained.
- The task editor uses the shared capture/analysis worker. A selected
  generator keeps its bounded capture demand active while the Properties tab
  is open. The waveform and sweep graphics are static shape/trajectory
  previews, not live samples; the numeric meters are the actual captured node
  output.

The editor does not yet display a per-node route-readiness badge or
device-channel label; users continue to use the fixed canvas rails and Audio
Setup shell for route/device status. The runtime still blocks invalid or
unrouted output as before. Root-note selection remains a MIDI-number slider,
and User Scale editing remains the existing validated semitone-entry field;
the note-picker wireframe below is a design target, not shipped functionality.

## What exists today and what this proposal changes

**Implemented today**

- Seven zero-input generator modules: Signal Generator, Impulse Generator,
  Click Generator, Burst Generator, Chirp Generator, Noise Generator and DC
  Generator. They require an explicit route to Output and explicit transport
  Start; insertion and editing never start playback.
- Signal Generator provides Sine, Square, Triangle, Sawtooth, White Noise,
  Pink Noise and DC waveforms. Its mode is Fixed Frequency, Continuous Sweep
  or Musical Sweep. It supports frequency/note entry, sweep endpoints,
  duration, curve, transport, musical scale/root/step, level, mute and phase
  invert. Square, Triangle and Sawtooth are explicitly aliased.
- The standalone Noise Generator offers White/Pink, a level in dBFS RMS,
  mute and a deterministic seed. Other source types have their own distinct
  semantics: periodic tones and several transients use peak dBFS; DC exposes
  an exact signed sample value.
- Non-generator modules continue to use the metadata-derived inspector.
  Signal Generator and Noise Generator now have task editors; their saved-base
  rows and runtime override/automation actions remain in collapsed sections.
- The existing canvas node preview is a worker-decimated waveform envelope.
  The selected generator context and sample-frame sweep progress appear with
  the inspector's shared analysis views. A separate floating Signal Inspector
  reuses the shared scope, spectrum, peak, RMS, correlation and tuner pipeline.
- Analysis is measured from admitted capture data. Stopped, stale, unavailable
  and failed states are distinct; a preview or plot must not invent live
  samples. Existing dBFS readings are digital references, never physical
  voltage or SPL calibration.

**Task-editor direction**

- A goal chooser and four dedicated, task-labeled generator layouts replace
  the always-open generic parameter grid as the normal editor.
- Waveform and sweep previews, explicit output-route guidance, an output-level
  measurement and compact transport state become part of the task view, subject
  to the real-data constraints below.
- Automation/control-source editing and processor identifiers move into
  collapsed **Automation** and **Developer** disclosures. They are not removed
  from the project or engine.
- User-facing copy uses **Frequency**, **Output Level**, **Sweep Time** and
  similar names, not implementation keys. Existing saved parameters and
  projects remain facts to preserve; a future implementation must explicitly
  map labels to keys and must not silently discard values.

## Entry flow: choose a goal, then configure it

On first insertion or an unsaved/new generator, show the goal chooser in the
Property Inspector. A saved generator opens directly into its current task
layout; a compact **Change goal** action returns to the chooser without
changing the graph node identity. Choosing a goal configures the editor, not
the audio transport, patch wiring or hardware. Nothing auto-connects or starts.

```text
SIGNAL GENERATOR                                      [Change goal]
What do you want to make?

┌────────────────────┐  ┌────────────────────┐
│ 〰  Fixed tone      │  │ ↗  Frequency sweep │
│ Choose one pitch   │  │ Move between Hz    │
└────────────────────┘  └────────────────────┘
┌────────────────────┐  ┌────────────────────┐
│ ♫  Musical sweep   │  │ ▧  Noise            │
│ Step through notes │  │ White or pink      │
└────────────────────┘  └────────────────────┘
┌───────────────────────────────────────────┐
│ ◉  Calibration signal                     │
│ Choose a digital reference tone or noise   │
└───────────────────────────────────────────┘

Route: Not connected to Main Output        [Show patch]
Playback: Stopped                          [Start remains in shell]
```

The icons above are role descriptions for this wireframe only, not proposed
assets. Use text labels and assistive names as the authoritative cues.

### Goal transition rules

| Goal | Destination layout | Intent |
|---|---|---|
| Fixed tone | Fixed Frequency | Establish a stable pitch or test frequency. |
| Frequency sweep | Sweep | Define start, end and time trajectory in Hz. |
| Musical sweep | Musical Sweep | Step through a selected scale and root. |
| Noise | Noise Generator | Choose White/Pink and understand its RMS/crest behavior. |
| Calibration signal | Guided choice, then Fixed Frequency or Noise Generator | Choose a digital sine reference or pink-noise reference; show measurement and analog-chain limits before Start. |

For Calibration Signal, the second step offers **Reference tone** and
**Pink-noise reference**; choosing one opens the corresponding existing
layout with an explicit digital reference preset and a persistent
“Digital reference only — no voltage or SPL calibration” note. Do not add a
new analyzer, route or hardware selection behind the user's back. Show its
Output Level as an adjustable digital target with the reference defined in
[`GeneratorLevelPolicy.md`](GeneratorLevelPolicy.md); never imply that the
preset calibrates an analog system.

For an existing node whose saved state cannot be represented by the new
layout, show **Saved settings need attention** with the legacy values intact,
identify the invalid or unsupported field in ordinary language, and offer
**Review settings** / **Keep current values**. Do not coerce it to a new goal
or overwrite project data on selection.

## Shared editor frame

Each configured layout retains the same top-level rhythm:

1. **Identity and intent:** “Signal Generator” and the selected goal/mode.
2. **Primary control:** the one defining value or choice, large and editable.
3. **Signal preview:** a clearly identified waveform or frequency trajectory.
4. **Output observation:** measured level only when a valid measurement is
   available; otherwise a named state, never a fake meter.
5. **Connection and transport context:** route destination, audio setup state,
   stopped/running/muted state; Start/Stop remains the shell's explicit global
   transport action.
6. **Optional details:** Advanced, Automation and Developer, all collapsed by
   default and keyboard-accessible.

Use a responsive single column in a narrow inspector. At comfortable width,
put the primary controls left and the signal/measurement area right; do not
make the inspector horizontally scroll to reach its main action. Keep labels
above values, values visually primary, units adjacent and stable, and the
tabular-figure behavior from the typography system. Do not compress essential
copy below the established readability floor to preserve a dense grid.

## Four task layouts

### 1. Fixed Frequency

**Primary question:** what tone or periodic shape, at what frequency?

```text
SIGNAL GENERATOR                                  Fixed Frequency
Fixed tone                                        [Change goal]

Waveform
[ Sine ▾ ]       Sine gives a clean fundamental.

Frequency
┌──────────────────────────────────────────┐
│ 440.00                                  Hz│
└──────────────────────────────────────────┘
Enter Hz, MIDI note or note name (for example A4).

Output Level
−12.0 dBFS peak · Target source peak

Waveform preview                 Live measurements
┌──────────────────────────┐     ┌─────────┐
│   ─╲      ╱─             │     │  -9.0   │  dBFS RMS
│     ╲____╱               │     │  -6.0   │  dBFS peak
└──────────────────────────┘     └─────────┘
Measured sample peak · latest 50 ms
Measured RMS · latest 300 ms

Route  Main Output · Not connected       [Show patch]
Output device  Select in Audio Setup      [Open Audio Setup]

▸ Advanced     ▸ Automation     ▸ Developer
```

The illustration is a generic sine shape, not copied product artwork. The
preview is either (a) a labeled static shape derived from the selected
waveform, or (b) a measured live capture with freshness state; these must look
different. The output meter is a measured level at a named point in the graph,
not a computed consequence of the level setting. Do not call it “Output level”
until the measurement tap is defined and available; otherwise use **Measured
level** plus the capture target.

**Primary controls:** Waveform (Sine, Square, Triangle, Sawtooth, DC where the
combination is valid); Frequency with Hz/MIDI/note entry; mute. Keep a
one-line aliasing notice beside non-band-limited Square/Triangle/Sawtooth.
Frequency supports the current explicit grammar (`440 Hz`, `MIDI 69`, `A4`,
`A4 (A=442 Hz)`); invalid or ambiguous text receives a specific inline error,
not an inferred interpretation.

**Advanced:** reference A4 tuning (400–480 Hz); phase invert; any additional
waveform-specific values only where the DSP truly exposes them. Do not expose
a raw parameter range as the primary control.

**Calibration entry:** start from Sine at 1 kHz as a suggested digital test
tone only. Keep any selected reference level explicitly digital; prompt users
to verify interface gain, output channel and connected equipment. No voltage,
SPL or external calibrator reading is implied.

### 2. Sweep

**Primary question:** where does the sweep begin and end, and how long does it
take?

```text
SIGNAL GENERATOR                                           Sweep
Frequency sweep                                       [Change goal]

Start Frequency               End Frequency
[ 20.0 ] Hz                    [ 20,000 ] Hz

Sweep preview — target frequency vs time
20 Hz  ─╮
        ╰────────╮
                 ╰────────────────────────── 20 kHz
       0 s                              5.0 s
Curve   [Logarithmic ▾]       Sweep Time  [ 5.00 ] s
Travel  [Ascending ▾]         Playback    [One-shot ▾]

Measured level  No live capture                    [Open Signal Inspector]
Route  Main Output · Connected                     [Show patch]
Playback  Stopped · press Start in the shell

▸ Advanced     ▸ Automation     ▸ Developer
```

The sweep preview is a **target trajectory** calculated from requested
parameters, not an analyzer result. The current supported curves are Linear
(Hz), Logarithmic (exponential Hz/time), and Exponential Curvature; label each
definition honestly. Ascending and descending sweeps are both valid. Display
duration, direction and transport as words and values. While running, show
current sweep position only if it comes from the existing sample-frame
progress; label one-shot completion and loop progress. A finite one-shot
ends in silence after its active duration; a loop continues until Stop.

Use Sine as the default sweep waveform. If the chosen current implementation
allows Square/Triangle/Sawtooth sweeps, keep them available in **Advanced**
with the explicit aliasing warning. Do not offer Noise or DC as a sweep shape
unless the signal model defines what that means. Do not add a deconvolution,
response curve or room result that the analysis path does not compute.

### 3. Musical Sweep

**Primary question:** which notes, from what root and at what step rate?

```text
SIGNAL GENERATOR                                      Musical Sweep
Step through a scale                                  [Change goal]

Root Note                    Scale
[ C2 ▾ ]                     [ Major ▾ ]
Reference A4  440 Hz          Steps are discrete notes, not a glide.

Step Duration
[ 0.50 ] s per note

Note path preview — intended notes
C2  ●────●────●────●────●────●────●  B2
    C    D    E    F    G    A    B

Playback  [One-shot ▾]       Measured level  Stopped
Route  Main Output · Connected                       [Show patch]

▸ Advanced     ▸ Automation     ▸ Developer
```

Offer Chromatic, Major, Natural Minor, Major Pentatonic, Minor Pentatonic and
User Scale. Root uses explicit note/MIDI naming and the saved A4 reference.
Step Duration is in seconds; explain “each note is a defined step target, not
a continuous pitch glide.” User Scale uses a dedicated note-picker/list with
ascending semitone offsets and a visible root-relative preview, not a raw
comma-separated storage string. Validate that entries are nonempty, ascending,
unique, 1–12 values, and that root plus the highest offset stays within MIDI
0–127. State an error next to the offending scale entry and preserve the
unsaved draft.

The preview draws the requested note sequence, not measured pitch. During
transport, show the current note only if the existing sample-frame-derived
step progress is available. If settings produce a one-shot, mark completion;
do not make an ended source look live.

### 4. Noise Generator

**Primary question:** White or Pink, and what RMS reference?

```text
NOISE GENERATOR                                  Noise
Noise colour                                      [Change goal]

[ White ]   [ Pink ]                 [Mute]

Output Level
−12.0 dBFS peak
[──────────────●────────────────────]

Noise preview                         Measured RMS
┌───────────────────────────────┐     -18.2 dBFS RMS
│ ·  ▂ · ▆ ▃ · ▅ · ▂ ▇ ·      │     Live · 300 ms window
└───────────────────────────────┘
Measured sample peak   -8.4 dBFS · latest 50 ms
Stochastic waveform · peaks vary

Pink noise is an approximation; see spectral limits.
Route  Main Output · Connected                        [Show patch]

▸ Advanced     ▸ Automation     ▸ Developer
```

White and Pink are the only current colors. Show the measured RMS window and
the capture target; show peak separately only if measured. A stochastic
preview must not imply a deterministic upcoming sequence. If stopped, use an
explicit “Stopped”/“Preview shape” treatment rather than a fake animated
signal. A random seed/reset control belongs under Advanced, where deterministic
repeatability can be explained.

**Calibration entry:** Pink-noise reference is a digital test source, not a
flatness or room-calibration guarantee. Carry the toolkit's known approximation
limits and require an independently calibrated analog chain for SPL/voltage
work.

## Output Level and live Peak/RMS policy

The level reference is now settled: deterministic waveform **Output Level** is
the target waveform peak amplitude relative to digital full scale, in dBFS.
For example, a sine set to -12 dBFS peak measures about -15.01 dBFS RMS; a
bipolar square at -12 dBFS peak measures -12 dBFS RMS. The level remains an
adjustable target in this policy; it does not mean every deterministic source
is fixed at -6 dBFS.

Noise uses the same peak-referenced amplitude scale internally, while its
prominent measurement is **Measured RMS** because noise is evaluated by RMS.
Its stochastic sample peaks vary and are not a fixed ceiling. Show **Measured
sample peak** as a separate live observation when valid. Existing shared
meter contracts use the latest 50 ms for sample peak (not true peak) and a
300 ms raw RMS window by default. Label the actual measurement point and
window; the existing shared capture meter is not automatically a
generator-local or post-Gain tap.

The internal scale follows the canonical target-peak amplitude factor
`10^(Output Level / 20)`. It is an amplitude reference, not a measured bound on
the random sequence. Do not imply that the configured value equals measured
RMS or the largest realized sample.

This resolves the prior open question about peak versus RMS policy. The request
for a fixed -6 dB output is superseded: Output Level is adjustable, not
universal or immutable. The earlier request for a more audible starting level
is reflected by the new-node -6 dBFS peak target described above; it is not a
limiter, physical safety guarantee or migration of existing projects. DC
remains an exact signed sample-value control in the current implementation; its
deterministic amplitude is equivalent to peak dBFS by magnitude, with sign
preserving polarity. Existing saved values and parameter keys remain intact.
The
canonical policy, display labels, examples, mute/transport behavior and
calibration limits are detailed in
[`GeneratorLevelPolicy.md`](GeneratorLevelPolicy.md). In particular, do not
show dBTP without a validated oversampled true-peak meter, and do not imply
voltage/SPL calibration or that a digital level is physically safe.

## Preview, measurement and visualization hierarchy

Keep the three established visualization levels coherent; they are different
views over shared data, not three independent meters:

| Level | Proposed generator UX | Data contract |
|---|---|---|
| **Embedded node** | Compact waveform envelope, source identity and stopped/live/stale mark. It remains secondary to the graph and route. | Reuse the current worker-decimated min/max preview at its existing cadence and visibility rules. Do not add an FFT or animate synthetic data. |
| **Property Inspector** | Goal-specific editor with static waveform/target-sweep preview, selected-node context and the shared scope/spectrum/peak/RMS/tuner tabs. Keep the task controls above the fold; measurements can expand below or open the shared inspector. | Static shape/trajectory is labeled **Preview**. Captured measurements name source/target, window, units and freshness. A missing capture is unavailable, not zero. |
| **Dedicated floating Signal Inspector** | Existing independent resizable monitor, retargetable/pinnable/freezable according to its current behavior; use it for larger waveform, spectrum and readouts while editing. | Reuse the same analysis session/worker; no duplicate DSP work, hidden Analyzer insertion or routing change. Do not restore global docking. |

Live sample-peak and RMS displays follow the confirmed policy, but remain
subject to actual valid measurements. The task editor reads the shared capture
at the generator node output before downstream processors; the UI names that
tap and uses the existing 50 ms sample-peak and 300 ms RMS windows. The
separate Signal Inspector can observe other admitted graph points. Distinguish
the source setting, measured signal at a tap, device output and acoustic
level; do not invent the latter two.

## State and feedback model

| State | Visible treatment and action |
|---|---|
| **Goal choice** | A five-item goal selector opens on Fixed tone by default. Changing the goal updates the existing DSP mode/waveform through the validated saved-edit path; it never starts transport. |
| **Stopped, valid** | Controls and static previews are available. Measurement area says “Stopped” or “No live capture”; no moving trace. Route readiness is separate from edit validity. |
| **Running** | Shell transport remains authoritative. Show “Running” with current fixed frequency/sweep note or progress only when sample-time data exists. Any measured meter is live only with valid fresh samples. |
| **Muted** | Persistent textual “Muted” state plus mute control; measured output should show silence only if actually measured. Oscillator/transport position is retained as the engine specifies; do not imply the source is stopped. |
| **One-shot complete** | Say “Sweep complete — output is silent until restarted” or equivalent for the selected mode. Do not leave a live-looking waveform/meter. |
| **Not routed** | “Not connected to Main Output” with **Show patch**. Explain that this source cannot be heard through the configured output yet; never auto-wire. |
| **Device not ready / output missing** | Keep configuration editable; Start remains blocked by the explicit shell setup flow. Identify Audio Setup as the place to choose the exact output device/channel. No fallback or automatic device choice. |
| **Measured data waiting/stale/degraded/unsupported** | Label the exact state and last-valid time/coverage where available. Do not continue animation or show stale samples as live. Offer the relevant signal view, not a fake zero. |
| **Invalid entry** | Preserve the user's text, focus the field, explain accepted form/range and show the unit. Do not commit partial values, clamp silently or change the current sounding setting. |
| **Unsupported/legacy saved combination** | Preserve saved project values and show a recoverable diagnostic. Offer to keep or explicitly revise them; never substitute a different mode/waveform behind the user. |
| **Runtime/automation error** | Announce the rejected target, failed source or latched silence state in plain language. Keep details in Automation/Developer; never show an unchanged-looking “success.” |

Warnings about raw aliasing, stochastic peaks, DC offset, looped output and
equipment risk appear next to the affected action/control with icon and text.
Use semantic warning colors only as a secondary cue; no flashing or color-only
status.

## Advanced, Automation and Developer

### Advanced

Collapsed by default. Contains specialist signal settings only: phase invert,
reference tuning, seed and reset, custom scale editor, supported waveform
limitations, and lower-level curve/curvature detail where needed. Present a
user term first, with optional technical definition. Do not put primary
frequency, endpoints, duration, scale or noise color here.

### Automation

Collapsed by default with a compact summary such as **“No modulation”** or
**“Frequency follows LFO”**. The existing engine supports typed scalar
parameter mapping for eligible continuous/smoothed parameters and transient
live/scheduled events; it does not provide persistent automation lanes or
arbitrary recorded curves.

The proposed normal automation editor uses user concepts: **Destination**
(Frequency, Sweep Time, etc., only if eligible), **Source** (LFO, one-shot
Envelope), and source-specific values (rate, attack/hold/
release, declared range). Show **Base value** as the saved value only when
needed for context; do not expose “saved base,” “live target,”
“apply/release override,” “automation now,” “disable automation,” or
“schedule automation target” as the primary interaction. Put explicit
one-shot runtime actions under an **Advanced live control** sub-section with
plain-language outcomes and clear transient/not-saved status. Keep discrete
parameters (waveform, scale, mute, seed and transport) out of mapping choices.

If no host runtime callback/scheduler is connected, disable unavailable
automation actions with an adjacent explanation. Do not show a control as
successful when it cannot reach the engine. Invalid ranges, queue rejection,
expired events and source faults remain visible, with a concise user message
and an expandable technical detail.

### Developer

Collapsed by default and visually subordinate. It may show stable graph
identity, processor type, raw parameter keys/values (`frequencyHz`,
`levelDb`, `levelDbRms`), sample rate, render/capture epoch, graph revision,
queue/rejection details and diagnostics when those are genuinely available.
Raw identifiers are copyable and labeled **Developer details**. Never use
them as the only accessible name or hide the user-facing diagnosis in this
section.

## Interaction, keyboard and accessibility

- **Pointer:** the goal selector opens on click; primary value fields support direct
  text entry and drag only when the control has a clear value/readout.
  Fine-adjust gestures are explicit and repeatable; no accidental transport
  start when editing. **Show patch**, **Open Audio Setup** and monitor actions
  retain their existing product-level behavior.
- **Keyboard:** logical Tab order follows task title → primary control →
  preview/measurement context → route actions → disclosures. Arrow keys
  operate focused choice groups and sliders; Home/End reach documented bounds
  where applicable; Enter commits a valid field; Escape restores the last
  committed value only after clearly announcing that action. Focus remains
  visible independently of selection/glow.
- **Screen readers:** each editable value exposes a plain name, current value,
  unit, valid range and mute/transport state. Graphical previews announce
  “static waveform shape preview,” “target sweep trajectory,” or the measured
  source and freshness. Meters announce units and window; updates are
  throttled/coalesced, not spoken on every audio frame.
- **Color and motion:** use words, glyphs and meter geometry with color.
  Cyan means audio only; analyzer green means measured analysis only; warning
  and error retain their explicit text. Reduced-effects mode keeps all state
  cues. No blinking, persistent haze, glow around body copy, or essential
  meaning encoded by animation.
- **Sizing:** use the bundled UI family, tabular figures and current readable
  role sizes; give controls at least the existing 40 px pointer-target
  guidance (44 px preferred for touch-oriented use). The narrow inspector
  becomes a single-column stack without clipped labels or horizontally
  scrolling primary controls.

## Hardware and listening safety

Generators are intentionally useful without a capture device, but audio is
heard only when explicitly routed to Output and started. Preserve explicit
Start/Stop, exact device/channel setup, no device fallback, and no autoplay.
The task editor reminds users that routing is configured on the canvas and
device setup in Audio Setup; muted state is shown when the saved mute parameter
is active. An output that is unmuted or looping must be clearly identified.

Keep the toolkit's warning that headphones, loudspeakers and connected
equipment can be damaged by unexpected or high-level test signals. A fixed
digital level does not make an analog setup safe. Avoid promising that
-6 dBFS is universally safe: DAC gain, amplifier gain, transducer sensitivity,
monitor volume and user distance determine physical exposure. DC is a special
case and must not be offered as a speaker-calibration substitute.

## Remaining implementation refinements

The current task editor implements the core musician-first control flow, static
preview, measured Peak/RMS, and collapsed Advanced/Automation/Developer
sections. The following details remain design targets rather than shipped
claims: a route-readiness badge and direct **Show patch** action; named-note
root selection instead of the current MIDI-number control; a graphical
root-relative User Scale editor instead of the existing semitone-entry field;
and a dedicated persisted state for the Calibration Signal choice. The
existing scope/spectrum/peak/RMS/tuner tabs and separate floating Signal
Inspector remain available through the existing Signal view.

The four layouts map to the existing `Signal Generator` mode and standalone
`Noise Generator`. Impulse, Click, Burst, Chirp and DC remain separate
modules. Static parameter-derived previews are visually labeled separately
from worker/capture-derived measurements. Saved project fields, automation
bindings, graph identity, Start/Stop behavior, DC exact-value semantics and
unknown project fields remain intact.

**Level policy confirmed:** deterministic waveform Output Level is target
peak dBFS; noise uses that same peak-referenced scaling with prominent measured
RMS plus sample-peak readouts. The requested semantics do not force a fixed
-6 dB value and do not remove the adjustable target. DC remains an exact
sample value. The task editors use the existing level fields and saved keys;
noise's historical `levelDbRms` key remains compatible while its UI now
describes the shared peak-referenced amplitude scale. The displayed live
measurements remain sample peak and RMS, never true peak or physical
calibration.
