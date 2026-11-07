# Guitar Tuner Module Plan

**Status:** feature plan only. No pitch detector, tuner interface or DSP is
implemented by this document.

## Objective

Specify a usable chromatic tuner that helps a musician tune a guitar. The
requested “pitch detection visual module” is realized as this focused
instrument-oriented tuner by default, rather than duplicating it with a
second generic pitch-display module.

## Confirmed requirements

- The user explicitly wants a tuner module for guitar tuning.
- Treat it as a module in the catalog, with each placed instance a separate
  node. Coordinate catalog placement with
  `04-ModuleCategoriesAndInsertionPlan.md`.
- Preserve output-only processing: an input device is not required for the
  engine in general, but the tuner needs an actual signal source. A guitar
  connected to a physical input is one source; a generator can supply a test
  tone. Do not automatically open an input device or start playback.
- Use the existing signal-capture/worker architecture where suitable.
- Keep callback work bounded, allocate/prep off-callback and show truthful
  confidence, silence, stale and invalid states.

## Investigated architecture and boundaries

`docs\SignalVisualization.md` documents the current `SignalCaptureBuffer`,
shared `AnalyzerEngine`, preallocated transport/results, worker-side analysis,
source/rate/channel provenance, resource admission and capture continuity
resets. Existing modules are Oscilloscope, Spectrum Analyzer, Peak Meter and
RMS Meter. The Spectrum Analyzer reports a dominant FFT bin, explicitly not a
guaranteed fundamental; a guitar tuner cannot simply rename its frequency
readout.

`docs\DspModules.md` says `AnalyzerNode` is a pass-through observation
endpoint. `docs\AudioEngine.md` requires graph nodes to feed the Output and
does not provide implicit channel mapping. `src\VisualAudioLab.Presentation\src\ModulePanels.cpp`
maps visualization catalog items to an `Analyzer` node. `docs\DesktopWorkflow.md`
requires explicit Start and documents the current transport behavior. The
tuner should follow the same no-autoplay and no-hidden-capture guarantees.

## Proposed product behavior and design targets

### Signal and tuning display

- Analyze one selected mono signal channel from a real graph node. In the
  first version, support a monophonic note; chords/polyphonic guitar analysis
  are explicitly deferred.
- Display detected note name **and octave**, measured frequency in hertz,
  deviation in cents, and a centered visual needle/indicator that distinguishes
  flat, in-tune and sharp. Pair colors with text, shape and accessible state.
- Include a textual status for no source, stopped, silence, low confidence,
  noisy/ambiguous signal, stale data and valid stable pitch. Do not show a
  stale pitch as live.
- Provide a reference tuning frequency with a clearly stated default proposal
  of A4 = 440 Hz, plus a standard six-string guitar reference preset proposal
  (low E through high E). Treat the range of adjustable reference values,
  preset editability and alternate tunings as open product decisions.
- Make the tuner an observation/pass-through module: it does not mute, alter
  gain, retune audio, or monitor a device by itself. Any audio monitoring
  remains under the existing explicit transport/output controls.

### Detection and qualification proposals

Use a monophonic fundamental estimator suited to plucked strings, such as a
time-domain autocorrelation/YIN-family method with confidence and harmonic
checks; compare it with a hybrid estimator if real guitar samples demonstrate
octave errors. A raw FFT maximum alone is insufficient because a guitar's
strong second harmonic may exceed its fundamental. Keep algorithm choice
provisional until deterministic and recorded-sample qualification is reviewed.

Proposed initial test targets—not settled product guarantees:

- Handle standard-tuned guitar fundamentals from approximately 82.4 Hz (low
  E2) through 329.6 Hz (high E4), plus practical detuning margins.
- On clean, stable monophonic test tones in that range, report steady-state
  pitch within ±1 cent of the reference.
- After a pluck, settle to a stable indication within 500 ms when the input
  remains measurable; expose confidence rather than forcing a note through
  the decay/noise floor.
- Provide a documented smoothing/hold behavior that is responsive but does
  not make the needle flicker. Reset/mark stale on discontinuities, source
  changes, graph revision or transport epochs.
- Qualify silence, broadband noise, low-level input, clipping, vibrato,
  harmonics, detuned notes and competing notes. No-note/ambiguous is a valid
  result state; it is not an exception or an invented nearest note.

These values should be validated with synthetic signals and representative
guitar recordings before being promoted to product guarantees. Avoid claiming
the physical MiniFuse or any other interface has been qualified.

## Ordered implementation steps

1. Approve the visual guideline gate and interaction states in
   `01-VisualDesignGuidelines.md`; define the tuner layout at normal, stopped,
   no-source, low-confidence, stable and stale states.
2. Confirm how a placed analyzer node schedules capture and how the user
   chooses its mono source/channel under the current bus constraints. Avoid
   adding a second capture or analysis service.
3. Define a pitch-result contract carrying source/node, channel, sample rate,
   frame/sample position, confidence, frequency/note estimate and validity.
   Respect graph revision, stream epoch and producer-drop/discontinuity
   metadata.
4. Prototype detector candidates in the worker/test layer. Select the
   fundamental estimator against low-E/high-E and harmonic-heavy guitar
   material; quantify latency, octave errors, confidence behavior and
   bounded memory/CPU before choosing an algorithm.
5. Prepare all detector windows/scratch and bounded history off-callback.
   Capture only copies the already-supported selected mono channel. Never
   allocate, perform pitch math, format text or update UI on the audio callback.
6. Add the tuner as an analysis/pass-through module and an Analysis-category
   catalog item only after implementation is available. The indicator shows
   note/octave, frequency, cents, reference and confidence with an accessible
   textual equivalent.
7. Define source lifecycle: no source before explicit Start; reset or mark
   stale on Stop, source change, graph revision, sample-rate change and
   discontinuity. Tuner visibility never opens a device or changes transport.
8. Update `docs\SignalVisualization.md`, `docs\DspModules.md`,
   `docs\DesktopWorkflow.md` and the module catalog only when the feature is
   implemented and its state/limits are verified.

## Non-goals

Polyphonic pitch/chord transcription, alternate/custom tunings as a guaranteed
feature, automatic guitar input selection, audio monitoring or mute controls,
pitch correction, MIDI, hardware probing, and automatic playback. Do not
create a second generic pitch visualization by default; reconsider only if a
separate use case is approved.

## Acceptance checks

- Synthetic clean tones at each standard guitar string reference and between
  semitones render correct note/octave and cents direction; boundary behavior
  and reference-frequency changes are covered.
- Tests include strong second/third harmonics, weak fundamental, noisy input,
  silence, low level, clipping, vibrato, detuning, multi-note ambiguity,
  dropped pages and source/epoch changes.
- Validity/confidence states distinguish no source, stopped, stale, silence
  and ambiguous pitch. No fabricated zero-Hz or stale “in tune” display.
- Analyzer capture and detector processing allocate no memory on the audio
  callback; bounded worker storage/admission and UI update rates are measured.
- A generator can feed the tuner without a capture device, and a guitar-input
  workflow uses only an explicitly selected input configuration. Neither
  inserting nor showing the tuner starts audio.
- Visual/presentation tests verify text, note/octave, cents, confidence,
  keyboard/screen-reader labels, scaling and reduced motion.

## Dependencies, risks and open decisions

Depends on `01-VisualDesignGuidelines.md`, output/source behavior from
`03-OutputOnlyProcessingPlan.md`, and category/insertion semantics from
`04-ModuleCategoriesAndInsertionPlan.md`. Settle reference-frequency range,
whether alternate tunings belong in v1, algorithm selection and target
qualification only after evidence. Real guitar/device testing is required
before claiming product accuracy; injected and synthetic tests alone do not
qualify a physical input chain.
