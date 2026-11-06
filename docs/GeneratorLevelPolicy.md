# Generator Level Policy

**Status:** implemented in the task-oriented Signal Generator and Noise
Generator editors. The canonical policy resolves the level-reference
ambiguity in [`SignalGeneratorUxRedesign.md`](SignalGeneratorUxRedesign.md).
The UI reads the existing captured node-output analysis; it does not add
callback-side metering or imply that an unrouted source is being measured. The
current generator contracts and their implementation status are tracked in
[`modules/SignalGeneratorToolkit.md`](modules/SignalGeneratorToolkit.md).

## Canonical Output Level

For deterministic waveform generators, **Output Level** means waveform peak
amplitude relative to digital full scale, expressed in dBFS:

`Output Level (dBFS peak) = 20 × log10(Apeak / AFS)`

where `Apeak` is the magnitude of the generated waveform's peak sample and
`AFS` is the full-scale sample amplitude. The corresponding linear amplitude
scale is `10^(Output Level / 20)`. It describes the generated digital source;
it is not a voltage, speaker level, sound-pressure level, safe listening
level, or calibration result.

The level control specifies a **target source peak**, not the current measured
meter value. Deterministic periodic waveforms use peak-normalized shapes, so
their generated peak follows the configured Output Level. Other deterministic
signals retain their existing envelope/impulse timing and report the actual
sample peak separately. A live meter reports the captured signal observed at
the generator node output.

Examples for a target of **-12 dBFS peak**:

| Waveform | Target peak | RMS relationship | Expected RMS |
|---|---:|---|---:|
| Sine | -12 dBFS | A sine's RMS amplitude is peak amplitude divided by `sqrt(2)`. | approximately -15.01 dBFS |
| Square | -12 dBFS | A bipolar square wave has RMS amplitude equal to peak amplitude. | -12 dBFS |

These are generated-waveform relationships; a measured post-processor or
captured signal can differ because of routing, processing, mute, transport,
measurement window, or capture conditions.

## Deterministic waveform generators

Use the peak-dBFS reference consistently for all deterministic waveform
output, including sine, square, triangle, sawtooth, swept tones, bursts, clicks,
impulses and DC. Preserve each generator's timing, waveform normalization,
envelope and transport semantics; the shared reference does not make their
waveforms or measurement windows interchangeable.

The visible control is named **Output Level** and includes its reference in
the accessible label and nearby help, for example:

```text
Output Level
-12.0 dBFS peak
Target peak amplitude of the generated waveform
```

In explanatory copy, distinguish **Target peak** (the configured source
amplitude) from **Measured sample peak** and **Measured RMS** (observations of
the actual routed/captured signal). Editing Output Level does not assert that
a live meter will immediately read the same number: the meter may be stopped,
muted, disconnected, stale, observing a different route point, or measuring a
transient with a window that includes silence.

DC is deterministic, so its waveform amplitude follows the same peak-dBFS
definition: the peak magnitude is `abs(value)`, and its dBFS equivalent is
`20 × log10(abs(value))` for nonzero `value`; zero is silence. Polarity remains
the sign of the sample. The current DC module exposes an exact signed
sample-value control rather than an Output Level control; this policy does not
silently convert, rename or remove that existing control. If a future editor
shows a dBFS Output Level for DC, it must preserve the separate polarity and
exact-value meaning, including a clear representation of zero/silence.

## Noise generators

Noise uses the same dB-to-linear amplitude scale (`10^(dB/20)`) internally for
consistency with deterministic waveform generators. Its persisted legacy
parameter key is still `levelDbRms` so existing projects and automation remain
loadable; the task editor presents its value as **Output Level · target peak
reference**. The stochastic sequence has variable crest factor, so its
measured sample peak may be below or above that nominal scale. Do not add a
hidden limiter, normalize each short measurement window to a fixed peak, or
call the configured target a peak ceiling. This relabeling preserves the
existing generated sample scaling; it does not assert a fixed noise RMS or
crest factor.

The Noise Generator prominently displays **Measured RMS** as well as a
**Measured sample peak** when valid measurements are available. RMS is the
primary evaluation readout for noise; the Output Level control retains its
canonical peak-reference meaning. The view must label these as different
quantities:

```text
Output Level                 Measured RMS
-12.0 dBFS peak              -18.2 dBFS RMS
Target amplitude scale       Live · 300 ms window

Measured sample peak
-8.4 dBFS · latest 50 ms
Noise peaks vary by realization.
```

The numerical noise readings above are illustrative only; they are not a
specified crest factor or a promise of an exact RMS-to-peak offset. White and
Pink retain their distinct stochastic generation and spectral behavior. The
choice of peak-referenced scaling must not be presented as a guarantee of
noise flatness, a fixed RMS value, or a fixed maximum sample.

## Live meters and honest measurement labels

Display two observations where the signal view has room:

1. **Peak** — label the current implementation's measurement **Sample peak**.
   The existing Peak Meter measures sample peaks over its latest 50 ms
   observation interval, with explicit warm-up validity and worker-side peak
   hold/release. It is not an intersample or true-peak measurement. Do not use
   `dBTP` or the unqualified label “True Peak” unless an oversampled true-peak
   meter is implemented and validated.
2. **RMS** — label **RMS** or **Raw RMS** with its observation window and
   dBFS reference. The existing RMS Meter reports raw RMS over a rolling
   300 ms window by default (configurable from 10 to 1000 ms) and requires
   complete contiguous coverage. A full-scale sine reads about -3.0103 dBFS
   RMS under this amplitude reference; a full-scale DC or bipolar square wave
   reads 0 dBFS RMS.

Both meters describe the **actual signal at the named measurement tap**, not
the generator parameter. State that point in the view (for example,
“Generator output,” “After Gain,” or “Captured input”) only when the pipeline
really measures that point. The task editor reads the shared analysis result
captured at the generator node's output, before downstream processors. A
capture exists only when that generator is connected to the active Output and
the bounded observation service admits it. If there is no valid capture, show
**Stopped**, **Waiting for samples**, **Unavailable**, **Stale**, or the actual
error, instead of zero or a live-looking meter. The existing worker calculates
the measurement; the audio callback only writes its bounded capture buffer.

Transport and source states affect what the meter should communicate:

| Condition | Display behavior |
|---|---|
| Stopped / not yet captured | Retain configured Target Output Level; mark live measurements stopped or waiting. Do not substitute the target for a meter reading. |
| Running with fresh valid samples | Show sample peak and RMS from the selected measurement tap, with units, freshness and the relevant windows. |
| Muted | Identify **Muted**. If the measured tap is downstream of mute and has valid samples, its actual reading may show silence; otherwise report the tap's real state. Do not infer measured zero from the mute toggle alone. |
| One-shot finished | Identify completion and silence-until-restart where applicable. Measurements follow the actual tap and window, including any silence in that interval. |
| Stale, incomplete, unsupported or failed capture | Stop live animation and label the precise unavailable/stale/degraded/error state. Never present stale data as current. |
| Downstream Gain or other processing | Meter the signal at its named location. Explain that downstream processing may change measured peak and RMS independently of the generator's target. |

The existing analysis contracts are documented in
[`SignalVisualization.md`](SignalVisualization.md). Metering stays on the
existing bounded worker path; it does not add audio-callback UI work.

## Product and safety language

- Use **dBFS peak** for the deterministic source control's reference, and
  **Measured sample peak** / **Measured RMS** for observations. Always retain
  units in labels, accessible names and screenshots.
- Use a consistent minus sign and tabular digits so readings are easy to scan.
- Do not describe a digital target as “calibrated,” “safe,” volts, dBu, SPL or
  acoustic output. Physical calibration requires an independently calibrated
  analog chain and suitable measurement equipment.
- A configured peak target is not a limiter or protection threshold. Do not
  promise that later Gain, DAC, amplifier or transducer stages cannot clip or
  produce hazardous sound pressure.
- Preserve explicit transport Start/Stop, no autoplay and the chosen hardware
  device/channel setup. A level policy must not select devices, start playback
  or alter routing implicitly.
- Follow the signal-first hierarchy, neutral charcoal surfaces, semantic
  audio cyan, crisp focus/selection and restrained local glow in
  [`VisualDesignLanguage.md`](VisualDesignLanguage.md). Meter traces are
  measurement identity, not decoration; stopped and stale states remain
  visibly non-live.

## Implementation boundary

Newly inserted Signal Generator and Noise Generator nodes start with an
adjustable **-6 dBFS peak target** to avoid the previous extremely quiet
starting level. This is a starting value, not a fixed output, limiter or
calibration; users can change it and may attenuate downstream with Gain. Older
projects keep their saved values, and projects without a saved level retain the
legacy DSP fallback. DC keeps its exact signed-value control. Noise retains its
legacy serialized key and numeric linear gain behavior while its UI semantics
are now explicitly peak-referenced. Automation, mute and transport behavior
remain intact.
