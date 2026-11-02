# Output-Only Processing Plan

**Status:** execution plan only; it specifies behavior and verification, not
an oscillator implementation.

## Objective

Make the no-input path an intentional supported workflow: a user can select an
output device, build a graph with an internal source, and explicitly start
processing without configuring a capture endpoint. Distinguish device-output
availability from a graph that actually produces signal.

## Confirmed requirements

- An input device is not a prerequisite for starting audio processing.
- Internal signal generation must work with device input disabled.
- Audio starts only after explicit Start. There is no hidden input selection,
  fallback endpoint, automatic playback or silent device substitution.
- A fixed-frequency oscillator is a requested future module, with free
  frequency and note-step-quantized frequency modes. It is not shipped today.
- Preserve mono/stereo bus behavior and the earlier proposed needs for
  mono-input/stereo-output effects and explicit mixer/routing semantics.
  Do not imply those adapters or a mixer are implemented.

## Investigated current behavior

`src\VisualAudioLab.Audio\src\AudioGraph.cpp` accepts `inputChannels == 0`;
nonzero input width must equal graph output width. The output bus must have
1–32 matching channels. A nonempty runtime graph requires exactly one Output,
each node accepts at most one upstream source, all nodes must feed Output,
and there is no implicit summing. The empty graph is allowed and renders
silence; an Output without a source also renders silence.

`src\VisualAudioLab.Presentation\src\RuntimePanel.cpp` offers
**None (output only)**. `docs\DesktopWorkflow.md` documents a zero-input
constant VAL Script source example wired to Gain/analyzer/Output, proving that
a script source can already emit an internal signal in offline preview. It is
not the requested oscillator UI or product module.

`GeneratorNode` exists as a source extension point, and
`tests\DspTests.cpp` has a `ConstantGenerator` test fixture. It is not a
product oscillator (`docs\DspModules.md`). `tests\AudioEngineTests.cpp`
verifies output-only stream preparation and that Input receives silence when
input is absent. `tests\GraphTests.cpp`, `tests\ScriptTests.cpp` and
`tests\RuntimeIntegrationTests.cpp` are relevant graph/desktop coverage.

## Ordered implementation steps

1. Trace runtime readiness and Start validation from Audio Setup through
   `RuntimePanel`, backend request construction, `AudioEngine` and
   `AudioGraph::prepare`. Confirm that absence of a capture endpoint never
   blocks output-only Start and that no stale input token is reused.
2. Keep the native device request explicit: output endpoint and selected
   output channel mask are required; input is explicitly none/zero. Reject
   invalid selected input state with a visible message instead of silently
   choosing another endpoint.
3. Define graph source requirements separately from device configuration.
   Preserve current explicit Output bus, channel-width checks, graph
   reachability and no-auto-summing constraints. A connected zero-input
   source feeding Output must be a supported runnable graph.
4. Integrate the future oscillator as a real source/generator module only in
   a separately approved implementation task. Its frequency control must
   support continuous/free frequency and note-step quantization. It must
   honor the selected graph bus width, have bounded realtime work, and be
   independent per node instance. Do not use a fixed DC constant as an
   oscillator or mark a scripted example as equivalent.
5. Define silence truthfully: output selected + empty graph or unconnected
   Output means valid silence, not an implicit generator or setup failure.
   Show distinct UI guidance that output is configured but the patch currently
   has no signal-producing path.
6. Retain the current rule that device input width is zero or matches output
   width until an explicitly designed input/output bus adapter exists. A
   mono-input/stereo-output effect requires a future explicit graph layout
   decision. A future Mixer must define gains, input count, summing order and
   channel behavior; never auto-sum graph fan-out.
7. Keep transport user-controlled. Graph edits, project load, generator
   insertion, discovery and device selection never start playback.

## Non-goals

Do not implement the oscillator as part of this plan unless a later task
explicitly executes that feature; no tuner, mixer, arbitrary routing, implicit
mono/stereo conversion, device fallback, or automatic transport is included.
Do not claim output-only physical hardware qualification from injected tests.

## Acceptance matrix

| Device input | Graph | Expected result |
|---|---|---|
| None / zero channels | Empty graph | Explicit Start may run output-only; emitted output is silence and UI explains no signal path. |
| None / zero channels | Output only | Valid silence; no hidden capture endpoint. |
| None / zero channels | Existing verified zero-input Script source → Output | Output signal follows the script; no oscillator claim. |
| None / zero channels | Future oscillator source → Output | Once oscillator feature is separately implemented, generated signal works with no capture endpoint. |
| Selected input | Width mismatches output | Clear validation failure; no coercion, truncation or fallback. |
| Any | Disconnected/dead branch, multiple upstream sources | Existing explicit graph validation remains; no implicit summing. |

Add integration cases asserting input endpoint absence, zero input channel
request, output-only capability state, explicit Start, exact output mask,
generated samples for the verified script path, silence semantics, failure
messages for invalid widths, and no playback on launch/load/insertion.
Use injected backend fixtures and deterministic buffers; do not probe physical
hardware unattended.

## Dependencies, risks and open decisions

This plan follows the visual-guidelines gate in
`01-VisualDesignGuidelines.md`. The generator feature depends on later
approval of its module contract and frequency-control UX. Confirm whether an
empty graph remains a valid silent stream (current engine behavior says yes)
and whether the UI should show a nonblocking “no signal path” status before
Start. Keep these questions separate from the confirmed rule that capture
input is optional.
