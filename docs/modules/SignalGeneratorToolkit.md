# Signal Generator and Test & Measurement Toolkit

**Status:** implementation is in review qualification. The seven zero-input
DSP generator classes, editor controls, typed per-parameter LFO/Envelope
mappings, output-only rendering, worker-backed node previews,
generator-aware inspector context, floating Signal Inspector and eight stopped
`.valab` examples are implemented. The Windows Release build and all 18
non-device CTest tests pass on the current tree; native discovery is
deliberately excluded to honor the no-device-probe requirement. Representative
UI evidence remains a separate qualification task. Nothing here establishes
hardware calibration, acoustic
qualification or plugin hosting.

This design extends the current [DSP module framework](../DspModules.md),
[visualization pipeline](../SignalVisualization.md), [project format](../ProjectFormat.md)
and [visual design language](../VisualDesignLanguage.md). It does not replace
the app's explicit Start/Stop transport, fixed shell or no-auto-playback rule.
Project loading, module insertion and parameter editing never start audio.

## Purpose and guarantees

The toolkit provides repeatable, visible, configurable signals for DSP
development, debugging, education, calibration workflows, acoustic
experimentation and analysis. Generators are audio source nodes: they have no
upstream audio input and may feed an Analyzer, effect, or Output. A valid
example is `Signal Generator -> Spectrum Analyzer -> Output`; no Input module
or input device is required. A generator without a path to Output is not
audible. An output-capable device by itself does not imply a graph is producing
non-silent audio.

Core guarantees:

* Each module starts from deterministic, documented state and remains
  continuous across arbitrary callback/block subdivisions.
* Start is always an explicit user action. There is no auto-start, implicit
  hardware selection, hidden limiter or output-device fallback.
* Parameter values are finite and domain-checked before reaching the renderer.
  Persisted base values are distinct from transient automation, overrides and
  effective values.
* Audio callbacks do bounded work using prepared state only: no allocation,
  deallocation, lock, graph compilation, logging, UI call or unbounded loop.
* Captured visualization/measurement work is worker-side, bounded and marked
  with its target, epoch, sample coverage and freshness.
* A dBFS value is a digital reference, not a voltage, sound-pressure level,
  safe listening level or device calibration.

The existing DSP framework has a zero-input `GeneratorNode`,
preparation-time allocation, block-partition-independent render ownership,
sample-timestamped parameter event queues and an analyzer worker. This work
adds seven product sources, saved examples and a bounded per-parameter control
mapping path. These mappings are explicit project settings, not visible
control cables or additional audio inputs. Retired VAL Script nodes remain
opaque legacy project data and are rejected by the runtime. Persistent automation curves and
arbitrary plugin hosting remain outside this implementation.

## Module set and user-facing behavior

The Module Browser exposes seven independently insertable and executable
modules in a **Generators** category. They may share DSP helpers and editor
components, but each has a stable graph type, own node identity/state,
meaningful controls and persistence. Insertion never wires modules for the
user. A module is silent until explicitly started and connected through the
patch to Output.

| Module | Signal and v1 controls | Behavior and honest limits |
|---|---|---|
| **Signal Generator** | Sine, Square, Triangle, Sawtooth, White Noise, Pink Noise, DC; frequency/mode, sweep endpoints/time/curve/loop, musical scale/step, level, mute and phase invert. | Free-running or finite sweep transport. Raw square/saw/triangle are not band-limited and alias; identify this in the editor. Pulse, band-limited waveforms and user wavetables are future work. |
| **Impulse Generator** | Peak level, one-shot or periodic, interval. | Emits a single sample at each scheduled trigger frame; it is not a finite-width click, sweep or acoustic impulse-response measurement system. |
| **Click Generator** | BPM or explicit interval, level, click duration/tone. | Emits a short deterministic transient for timing and event checks. A click is a finite shaped pulse, not a one-sample impulse. |
| **Burst Generator** | Frequency, burst duration, repetition/one-shot, level and envelope. | Emits a bounded tone burst, default example 1 kHz for 100 ms. The envelope is explicit; it does not promise calibrated loudspeaker or room results. |
| **Chirp Generator** | Start/end frequency, sweep time, linear/logarithmic curve, level, one-shot/loop. | Emits a deterministic swept sine. Supports frequency-response experimentation; no deconvolution, impulse-response estimator or room-measurement calibration is implied. |
| **Noise Generator** | White or Pink, level, mute and deterministic seed/reset controls. | White noise is independent seeded pseudorandom noise. Pink noise is a bounded filtered/Voss-style approximation whose supported band, normalization and measured spectral tolerance are specified in the verification report. Brown/blue noise are future work. |
| **DC Generator** | Exact value from -1.0 through +1.0, mute. | Outputs a stable value; no dither, limiter or unrequested smoothing. Value is represented exactly at each sample. Useful for control/DSP debugging, not a safe speaker test signal. |

Signal Generator's DC waveform and the standalone DC Generator are both
available intentionally: the first is convenient for waveform comparison in
one versatile source; the second makes the exact bounded control value the
primary operation. They have separate names and cannot silently map to one
another on project load.

Newly inserted Signal Generator and Noise Generator nodes start at an adjustable
-6 dBFS peak target; this is a digital amplitude reference, not a physical
voltage/SPL calibration or protection limit. Users can change Output Level or
attenuate downstream with Gain. Existing projects retain saved levels, while
older project files that omit a level retain the legacy DSP fallback. DC
defaults to 0, and a generator never starts itself. The UI warns that
headphones, loudspeakers and connected equipment can be damaged by unexpected
or high-level test signals. There is no hidden limiter: values above full scale
remain above 1.0 in the floating-point graph and may clip in a later
device/output stage.

## Frequency entry and musical semantics

Frequency entry is explicit and unambiguous:

* `440 Hz` or `440` with the Hz field selected means 440 Hz.
* `MIDI 69` means MIDI note 69.
* `A4` means the note A in octave 4 under the saved `referenceAHz` control
  (400-480 Hz, default 440).
* `A4 (A=442 Hz)` explicitly overrides that reference for this entry. The
  bare string `A442` is rejected with a diagnostic; it is never guessed to
  mean an octave, note, or reference frequency.

Equal temperament maps MIDI note `m` to
`f(m) = fA * 2^((m - 69) / 12)`, where `fA` is the configured A4 reference.
The default is A4 = 440 Hz. Entering MIDI 69 at A4 = 442 Hz yields exactly the
configured reference. Accidental spellings use conventional enharmonic
mapping (`C#4` = `Db4`); note names are case-insensitive, octave integers are
required, and negative octaves are accepted only if the resulting frequency
falls inside the node's validated range. The application uses the documented
MIDI convention C4 = MIDI 60, A4 = MIDI 69.

Musical Sweep uses an explicit root MIDI note and one of Chromatic, Major,
Natural Minor, Major Pentatonic, Minor Pentatonic or User Scale. A custom scale
is saved as `audio.scaleIntervals`, a comma-separated list of 1-12 integer
semitone offsets from the root. Entries must be ascending, unique, nonnegative,
and stay within MIDI 0..127 after root transposition. An empty, unsorted,
duplicate, or out-of-range user scale is rejected before activation. Step duration is
positive and finite; each note is a defined step target, not a continuous
pitch glide. A step is applied at the first sample frame of its quantized
duration. Duration rounding and final-step behavior are specified under
transport below.

## Sweep mathematics and sample timing

For duration `T > 0`, sample rate `Fs`, and endpoints `f0, f1`, prepare the
integer duration `N = max(1, floor(T * Fs + 0.5))` frames. The normalized sweep
position at frame offset `n` is `u = min(n / N, 1)`, with `0 <= n < N` during
the active sweep. `u = 1` is the exclusive endpoint at the first frame after
the sweep. This makes one-shot output exactly N frames independent of host
block sizes. Loop retriggers at the same endpoint boundary, and the next loop's
first sample uses `u = 0`. A one-shot emits silence after its N active frames
until transport is explicitly restarted.

Three choices have distinct definitions and retain exact endpoints:

* **Linear in Hz:** `f(u) = f0 + (f1 - f0) * u`.
* **Logarithmic:** `f(u) = f0 * exp(u * ln(f1 / f0))`, requiring positive
  endpoints. This is exponential frequency versus time, with constant slope
  in log-frequency; it is not separately mislabeled as another exponential
  mode.
* **Exponential curvature:** `f(u) = f0 + (f1 - f0) *
  expm1(k * u) / expm1(k)` for finite `k != 0`, and the linear equation at
  `k = 0`. This is a true exponential-shaped frequency-versus-time curve with
  normalized endpoints. Positive k spends more time near the start; negative k
  spends more time near the end. The UI labels and documents k; it does not
  claim this is logarithmic spacing.

Ascending and descending sweeps use the same equations; logarithmic ratios
remain positive for either direction. Near-equal endpoints use a numerically
stable limit rather than dividing by a near-zero denominator. Invalid
nonpositive logarithmic endpoints, duration, curvature, or overflow are
reported before the plan becomes active.

Oscillator phase is a continuous cycle accumulator. Fixed-frequency processing
uses `phase[n+1] = wrap(phase[n] + f[n] / Fs)` and waveform lookup/evaluation
uses `2*pi*phase[n]`; no phase reset occurs at ordinary block boundaries.
Continuous sweeps use the analytic integral of their selected frequency
function to derive phase from absolute sample offset, avoiding a callback-size
dependent approximation. A general frequency-modulation control input is not
implemented yet. At a sample-rate change the graph is stopped
and re-prepared by the existing engine lifecycle; phase resets to the
documented start phase on explicit restart, never because a UI repaint or
callback boundary occurred.

For non-looping one-shot, the trigger frame is the first generated frame after
explicit Start. Looping repeats at the exclusive sweep endpoint. A
new prepared plan resets transport phase; there is not yet a separate
sample-timestamped retrigger command. Ordinary continuous level/frequency
targets use the node's declared sample-time smoother. Mute, waveform, scale
and transport are discrete changes with event-frame semantics. A waveform
change applies at the accepted event frame and preserves phase.

## Amplitude, normalization and digital references

The user-confirmed target level reference and proposed live Peak/RMS display
policy are documented in
[`../GeneratorLevelPolicy.md`](../GeneratorLevelPolicy.md). That decision
does not itself change the implementation described below: until a separate
implementation lands, existing parameter ranges, defaults, project values
and DSP normalization remain as currently implemented.

The common Level control spans -80 dBFS through +12 dBFS and is a peak
amplitude reference for deterministic periodic waveforms:
`A = 10^(dBFS / 20)`. Sine, square, triangle and sawtooth are normalized to
unit peak before scaling. Phase invert negates the resulting signal; it does
not alter oscillator phase. Mute emits exact zeros without destroying
oscillator/sweep position, so unmuting resumes the current transport.

White and Pink Noise Level is explicitly **dBFS RMS**, not peak dBFS. The
random process is normalized to unit RMS before its level factor; individual
samples may exceed the level and full scale because noise has a variable crest
factor. The UI labels the RMS reference and does not promise a fixed peak.
Impulse/Click/Burst/Chirp controls label their peak amplitude or envelope
convention in the editor. RMS measurements always name their window and
reference; a full-scale sine has approximately -3.01 dBFS RMS under an
amplitude-1.0 reference.

The DC module exposes exact sample value from -1.0 to +1.0; it is not converted
from dBFS and does not offer a fictitious phase or frequency parameter.
Silence is 0. A requested non-finite/out-of-domain value is rejected rather
than clamped. No generator uses an automatic limiter or hides full-scale
exceedance. The downstream meter may report digital sample peaks over
0 dBFS; this says nothing about analog voltage or acoustic SPL.

## Impulse, click, burst and noise algorithms

* **Impulse:** one nonzero frame per trigger, at the exact trigger sample; all
  other frames are exact zero. Period is integer frames, rounded from interval
  seconds at the current sample rate. One-shot produces one impulse at frame
  zero per Start. Periodic mode produces at frame zero and every Nth frame.
* **Click:** periodic interval is `60 / BPM` seconds, or explicit interval,
  rounded once to nearest sample and bounded to at least one frame. A click is
  a deterministic short decaying tone burst (default 1 kHz, 5 ms, Hann-like
  taper) with low default peak level. It has its own count/period; it is not a
  one-sample impulse. BPM changes become eligible at a discrete next-click
  boundary to avoid splitting one interval.
* **Burst:** sinusoidal carrier gated for the prepared duration. A short
  raised-cosine fade at onset and offset is parameterized in seconds and
  clamped to at most half the burst duration. Duration uses the same nearest
  sample rounding as sweeps. Repeating bursts have a separate interval and
  must not overlap unless the chosen version explicitly implements overlap.
  Version 1 rejects overlapping period/duration configuration.
* **White noise:** a per-instance fixed-state PRNG with an explicit 24-bit saved seed,
  reset determinism and no shared mutable/global RNG. Use a documented
  bounded distribution; normalize generated white samples to unit RMS in
  expectation. Repreparing or resetting with the same seed reproduces the
  same sequence. Seed changes are discrete and take effect on explicit reset.
* **Pink noise:** bounded per-instance filtered white noise with fixed prepared
  state and no callback allocation. A seeded offline test measures five-bin
  DFT power density around octave centers from 46.9 Hz through 6 kHz; adjacent
  octave estimates must stay within 6 dB for that deterministic test sequence.
  This is a coarse V1 spectral check, not a calibrated flatness specification.
  At 48 kHz, seed 7331 and 65,536 frames, the latest deterministic run measured
  white RMS 0.100120210, pink RMS 0.100000497, and adjacent-octave density
  changes of -1.615659, -3.224157, +3.909935, +1.980782, -1.001047,
  -3.825437 and +4.488968 dB. The shipped filter is not asserted flat beyond
  that measured range. Brown/blue noise and user-selected spectral shaping
  are future work.

## Automation and typed control integration

The engine supports bounded live and sample-frame-scheduled scalar parameter
events. Generator editors also provide an explicit typed scalar mapping for
continuous (smoothed) destination parameters. Mapping metadata is stored as
scalar project fields under `audio.control.<parameter>.<field>`; the UI
requires the user to select both a destination parameter and source type.
This is a parameter mapping, not a draggable control wire or an audio input.
Audio edges remain separate; no generator acquires an upstream audio port.

The mapping equation is `effective = base + offset + depth * source`. LFO is a
sine with normalized range [-1, 1], phase reset by explicit transport Start
and rate 0.01-20 Hz. One-shot Envelope is a linear 0..1 attack, hold, and
linear release started by explicit transport Start; its time is derived from
prepared sample frames. Both use instance-local render state. All sources are
evaluated against sample time, never UI refresh or host block length, and
execute without callback allocation, deallocation, locks, logging, or UI calls.

Only parameters with a continuous smoothing policy can be mapped; discrete
waveform, mode, scale, mute, seed, transport, and trigger choices reject the
mapping. Declared source extrema plus offset/depth must stay inside the
destination's physical domain for the saved base and for subsequently
accepted parameter events. Related mapping fields are saved as one undoable node edit and project
round-trip through the existing scalar parameter format. Runtime scheduled
events remain session-ephemeral; persistent automation curves/arrangements
are separate and not implied by these mappings.

Base parameter values and mapping fields persist through the existing
project scalar format; runtime scheduled events remain session-ephemeral.
Unknown project data continues to be preserved. Persistent automation
curves/arrangements remain a separate feature, not a capability implied by
these mappings.

## Visualization and monitor design

The three visualization levels share the existing capture and analysis
pipeline. Node previews are worker-decimated scope envelopes; the inspector
combines shared measurement tabs with generator context, and the independent
floating Signal Inspector reuses the same session and worker:

| Level | Generator presentation | Data and state |
|---|---|---|
| **Node preview** | Visible scheduled nodes render a worker-decimated min/max waveform envelope at 10 Hz when the canvas is sufficiently zoomed; stale data is muted and capture admission failure is explicit. | It is a compact waveform preview, not a per-node FFT or a promised second channel. FFT work stays off paint. |
| **Inspector** | The selected module editor provides parameter and typed-control mapping editors. The shared inspector provides scope, spectrum, peak, RMS and tuner views; generator context shows configured signal values and sample-frame-derived sweep progress. | Capture status is Stopped, Waiting, Live, Stale, Degraded, Frozen, Removed, Unsupported or an admission/source error. There is no separate requested-versus-measured frequency overlay or event counter. |
| **Analysis monitor** | A resizable independent floating Signal Inspector displays the five shared plots, target, pin/freeze state and generator context. It shares the analyzer worker and does not change routing or transport. | Global docking is intentionally not restored. Monitors remain subject to shared capture and component admission limits. |

Current shared Oscilloscope, Spectrum Analyzer, Peak Meter, RMS Meter,
Correlation Meter and Guitar Tuner views remain the measurement surfaces.
Correlation is mean-removed L/R covariance normalized by the 300 ms RMS
window's standard deviations; it is unavailable for mono sources or windows
without measurable channel variance. The new generators can
feed those nodes and the explicit Output. No generated analytic spectrum is
substituted for captured measurement. Generator-specific event counts,
noise-band metadata, DC statistics, and separate requested-vs-measured
overlays remain future UI work.

Predefined patches save actual source, analysis and output nodes with accurate
connections and parameter values. Opening an analysis view does not inject a
hidden Analyzer node or change graph routing.

Visuals follow the current premium neutral shell and approved semantic colors
in `VisualDesignLanguage.md`: crisp signal trace, restrained local glow,
readable numeric values and clear selection/focus. The generator controls
remain secondary to live signal measurements; level safety and fault feedback
take precedence. All view-specific work stays off the callback and obeys the
shared worker's resource/admission limits.

## Saved example projects and honest workflow

These are required `.valab` projects, not merely names in documentation.
Each project has a valid graph and scalar parameters, opens stopped, and
requires explicit Start. Where a physical Input or analyzer capture is
required, the project states which device setup the user must select; it does
not auto-select or claim measurements without capture.

| Project | Required saved routing and use | Claim boundary |
|---|---|---|
| **Sine Wave Test** | Signal Generator (sine, fixed 440 Hz, quiet level) -> Output. | Digital tone check only; no calibrated voltage/SPL. |
| **Frequency Sweep Analyzer** | Signal Generator (20 Hz to 20 kHz log sweep) -> Spectrum Analyzer -> Output. | Frequency trajectory/spectrum visualization; no response measurement without a valid measured path/reference. |
| **Room Measurement** | Chirp Generator -> Output, plus Input -> Analyzer as a separately documented capture path. | User must select/configure both physical paths and external microphone/preamp. No deconvolution, latency compensation, SPL calibration or room metric is claimed unless later implemented and qualified. |
| **Speaker Calibration** | Noise Generator -> Output, plus optional explicit Input -> Analyzer capture. | Digital pink/noise workflow only; user calibrates the complete analog chain independently. Warn about hearing/equipment levels. |
| **Compressor Test Bench** | Signal Generator or Burst Generator -> Compressor -> Output; analyzer side observation where supported. | Demonstrates DSP parameters; does not imply plugin hosting. |
| **EQ Measurement Patch** | Chirp Generator -> EQ -> Output; explicit optional analyzer/input path for measured response. | Does not claim bypass comparison is an acoustic measurement or that a transfer-function estimator exists. |
| **Reverb Analysis Patch** | Impulse Generator -> Reverb -> Output, with downstream Analyzer observation. | Shows digital impulse/tail behavior; no physical room IR/deconvolution. |
| **Latency Measurement Patch** | Click Generator -> Output and optional explicit capture input -> Analyzer. | Sample scheduling/capture workflow only. No round-trip latency or loopback compensation is claimed. |

Each project includes a note in its graph or accompanying user guide on the
required connections, output safety and measurement limits. A loaded project
never starts audio. A missing input device does not block generator-only
playback, while the measurement workflow indicates which results need capture.

## User safety and calibration workflow

1. Verify the selected output and physical equipment before starting; no
   native probe/playback is performed by tests or documentation generation.
2. Begin with the quiet generator default, keep the output muted/low while
   wiring, then explicitly unmute and Start. Raise the level cautiously.
3. Treat +12 dBFS, unnormalized DC and noise crest peaks as potentially
   over-range digital signals. Protect hearing and equipment independently;
   the application does not provide an analog safety guarantee.
4. Distinguish digital level from calibrated voltage/SPL. Calibration needs
   a known interface output/input, transducer/microphone sensitivity,
   preamp gain, reference level and verified acoustic environment, none of
   which is inferred from a dBFS meter.
5. For room/speaker workflows, document the microphone/preamp/placement,
   output chain, channel mapping, sweep range and level, capture continuity,
   background noise, sample rate and analysis window. Mark unmeasured stages
   unavailable, not zero or “calibrated.”
6. Stop explicitly after a test; a looped signal continues until Stop. Closing
   a monitor or project never substitutes for transport Stop.

## Requirement-to-surface and verification map

| Requirement | Primary surfaces | Required verification |
|---|---|---|
| Seven separate source modules; no upstream audio/input device required. | `ModuleCatalogItems.h`, module insertion, `AudioGraph`, `GeneratorNode` factory and `RuntimePanel`. | Catalog and editor integration tests; executable generator -> Output graphs; reject accidental audio input/fan-in; offline render without hardware. |
| Fixed Hz, MIDI and note entry; tuning A4=440/442. | Generator editor and frequency parser/formatter. | Conversion vectors, octave/accidental parsing, A442 rejection, diagnostics and persisted round-trip tests. |
| Linear/log/exponential-curvature sweeps and musical scales. | Shared sweep DSP and editor. | Exact endpoint, monotonic ascending/descending, positive-domain, near-equal, sample-rate, duration rounding and block-partition invariants. |
| Sample-accurate Free Run/One-Shot/Loop/retrigger. | Engine event path, generator state and transport UI. | Explicit Start/Stop only, first/last frame and loop boundaries, arbitrary block sizes, restarts and rate reprepare. |
| Level, dB, mute, invert, exact DC. | Typed parameter definitions, editor and persistence. | dB conversion/over-range/no hidden clipping, exact DC values, event smoothing/discrete semantics. |
| White/Pink statistical noise. | Noise DSP and editor seed/reference labels. | Deterministic reseed, RMS/crest statistics and measured band/octave slope tolerance across sample rates. |
| Impulse, Click, Burst, Chirp. | Four module editors and shared event/tone primitives. | Exact impulse placement/count, interval rounding, overlap rejection, burst envelope duration, chirp endpoint/phase invariants. |
| Node preview, inspector and floating monitors. | `NodeView`, `ModuleEditor`, `VisualizationWidgets`, `VisualizationSession`. | UI path tests, worker data freshness, open/close/re-target/freeze behavior, bounded rendering workload and saved monitor state. |
| Oscilloscope/Spectrum/Peak/RMS/Correlation and patches. | Existing analysis service, analyzer catalog and `.valab` examples. | Correct generator routing, stereo correlation sign/variance and mono-unavailable state, project load/edit/save/reload, explicit Start and stopped-on-open. |
| Automation, control signals, LFO, Envelope. | Typed parameter mapping and scheduler. | Type/range/cycle/fan-in rejection, discrete timestamp semantics, sample/block invariance, queue saturation/errors, no allocations/locks. |
| Safety, portability and truthful calibration. | Generator editors, audio setup/transport, docs/examples. | Defaults are quiet; no autoplay/fallback; over-range disclosed; room/latency/calibration caveats in each relevant example. |

## Implementation status and acceptance gates

The architecture contract, seven DSP source types, graph/catalog mapping,
base editor controls, typed parameter mapping, custom frequency/scale entry,
source-relative sweep progress, worker-backed node preview, floating monitor,
stereo correlation meter and eight offline-loadable example projects are in
place. The generator task editor now groups fixed tone, frequency sweep,
musical sweep, noise and calibration-reference goals; provides mode-specific
controls, static waveform/sweep previews, a clear output mute, and worker-
measured sample Peak/RMS readouts; and keeps engine parameter/control rows
inside collapsed Advanced, Automation and Developer disclosures. Noise keeps
its legacy persisted `levelDbRms` key while the editor labels it as a
peak-reference target.

The Windows x64 Release build passes all 18 non-device CTest tests, including
DSP allocation instrumentation, generator-level math, visualization and panel
integration. Native device discovery is excluded to avoid probing hardware.
Software-rendered default and constrained-width task-editor screenshots were
captured without starting transport in an earlier review. The current
retirement PR updates the Windows review bundle but does not refresh those
screenshots. This evidence does not qualify physical audio output, acoustic
calibration, macOS or multi-monitor DPI.

No phase claims hard realtime deadlines based only on average CPU or a
lock-free queue. User hardware, ASIO/vendor drivers, acoustic transducers,
calibration, plugin hosting and macOS/ARM require their own qualification.

## Current/future boundary

**Implemented:** zero-input DSP source nodes for all seven generator modules;
prepared DSP lifecycle and per-node state; base parameter layers and bounded
sample-frame events; typed per-parameter LFO and one-shot Envelope mappings
for continuous parameters; output-only transport;
deterministic note/custom-scale parsing; scalar project persistence; the
musician-focused Signal Generator and Noise Generator task editors with
mode-specific controls, shape previews, output mute and live sample Peak/RMS
display; source-relative sweep progress; worker-decimated node scope
previews; generator-aware inspector context; a shared floating monitor;
eight saved stopped projects; and the analyzer worker with
scope/spectrum/peak/RMS/correlation/tuner views.

**Still missing or unqualified:** visible control cables/independent draggable
control nodes, persistent automation curves, separate requested-versus-
measured overlays/event counters, separately persisted monitor geometry, and
final release/evidence delivery. This work does not
provide calibrated voltage/SPL, physical room impulse response,
deconvolution, loopback latency compensation, plugin hosting, band-limited
oscillators, Brown/Blue noise, or physical device/room qualification.

As implementation lands, replace “planned” claims with precise versioned
capability and test evidence. Do not retain obsolete limitations after code
and tests have actually removed them.
