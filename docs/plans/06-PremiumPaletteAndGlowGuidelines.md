# Premium Palette and Glow Guidelines

**Status:** implemented in the JUCE presentation theme and reconciled with
`docs/VisualDesignLanguage.md`. The user approved this refined palette and its
roles for implementation. MIDI and control remain reserved semantics because
those signal capabilities are not shipped.

## Objective and constraints

Retain the requested dark instrument-studio character and saturated signal
accents while making hierarchy, state, selection and accessibility dependable.
The supplied palette is implemented as one semantic theme, reconciled with the
existing signal-first and fixed-shell direction rather than added as a
competing design system.

Inspiration from Arturia, FabFilter and Native Instruments means precision,
confident hierarchy, legible controls and polished feedback. It does not mean
copying proprietary artwork, product layouts, typography, curves, icons or
signature visual treatments, nor does this document claim verified parity.

## Approved palette and token roles

| Token | Hex | Role and limits |
|---|---|---|
| Background | `#1B1D20` | Deep neutral shell and canvas foundation. |
| Panels | `#262A2D` | Default neutral panel surface; do not use color alone to separate panels. |
| Text | `#F2F1EE` | Primary text, labels and normal error copy. |
| Audio | `#00C2D1` | Audio signal identity and audio-route trace; pair with direction/port labels. |
| MIDI | `#CC5EFF` | Reserved MIDI/event identity only if that feature is implemented; do not imply MIDI ships. |
| Control | `#E4793A` | Reserved control/modulation identity only if implemented; do not use as warning or success. |
| Analyzer | `#9DDC4A` | Analyzer identity/readout trace, not general success or “ready” status. |
| Selected | `#F39A57` | Selection accent for outlines, handles and a compact state marker; not a broad selected fill. |
| Warning | `#D7A93F` | Warning icon/edge and labeled warning state; never the only indication. |
| Error | `#D45353` | Error icon, marker or crisp accent only on the panel; not normal-size text. |

The supplied tokens describe colors, not shipped modules or signal types. In
particular, MIDI and control are reserved semantics and must not be used to
suggest that either capability is available before implementation.

### Supporting tokens

These supporting values are implemented. Keep surfaces neutral so saturated
colors remain attached to signal meaning.

| Token | Candidate | Use |
|---|---|---|
| `surface.raised` | `#30363A` | A raised menu, selected list row or transient dialog section; use sparingly. |
| `surface.input` | `#17191C` | Text-entry wells and numeric input fields. |
| `border.structural` | `#73808A` | Crisp boundary where structure must remain visible, including panel separation and essential graph/control edges. |
| `border.required` | `#91AAB2` | Required/invalid-field boundary, paired with a label or error text; do not convey required status by color alone. |
| `text.secondary` | `#B7BDC1` | Supporting labels, units and non-primary instructions. |
| `text.muted` | `#9AA3A9` | Lower-priority metadata only when it remains readable at the actual surface and scale. |
| `text.disabled` | `#7B848A` | Disabled control text only with a disabled shape/state cue; not for important instructions or measurements. |
| `state.hover` | `#343B40` | Subtle neutral lift plus pointer/keyboard state; do not change signal identity. |
| `state.pressed` | `#3A4247` | Short-lived neutral pressed surface, with a crisp edge or depressed geometry. |
| `state.focus` | `#F2F1EE` | Keyboard-focus outline; use a distinct double/dashed treatment when a control is also selected. |
| `state.errorText` | `#FF8A8A` | Contrast-validated optional error-copy color; current default is primary text plus the original red icon/accent. |
| `text.onAccent` | `#121416` | Foreground for filled accent buttons. Do not put the supplied off-white text on bright accent fills. |

The raised and input surfaces are intentionally close in luminance
to the panel. Their edges must be crisp and structural; do not mistake a subtle
surface shift for a control boundary. The text-disabled candidate is
decorative/inactive only and does not meet the normal-text target on all
surfaces; disabled controls still need readable names or adjacent
explanations.

## Contrast assessment

Ratios use opaque sRGB relative luminance and are rounded to two decimals;
automated presentation checks cover the implemented palette pairings.
WCAG AA reference thresholds are **4.5:1 for normal text** and **3:1 for
large text and necessary non-text graphics**. Decorative separators have no
minimum contrast requirement, but must not be the only way to locate an
interactive control. Validate final JUCE-rendered states and scaling; do not
rely on theoretical token ratios after alpha compositing.

### Supplied semantic colors on the panel

| Foreground on `#262A2D` | Ratio | Proposed treatment |
|---|---:|---|
| Text `#F2F1EE` | 12.81:1 | Passes normal-text target. |
| Audio `#00C2D1` | 6.64:1 | Passes; suitable for labeled traces/marks. |
| MIDI `#CC5EFF` | 4.56:1 | Barely passes normal text. Do not dim it casually; verify actual font weight, size and compositing. |
| Control `#E4793A` | 4.89:1 | Passes, with limited margin; reserve for control semantics. |
| Analyzer `#9DDC4A` | 8.80:1 | Passes; use only for analysis identity/data. |
| Selected `#F39A57` | 6.59:1 | Passes as an outline/handle accent; selection still needs shape/state cues. |
| Warning `#D7A93F` | 6.64:1 | Passes for a warning mark; pair with text or iconography. |
| Error `#D45353` | 3.56:1 | Fails normal text. Keep it to icon/accent use on this panel. |

The panel/background contrast, `#262A2D` against `#1B1D20`, is only **1.17:1**.
The raised and input surfaces are also subtle: `#30363A` against the
panel is **1.18:1**, and `#17191C` against the panel is **1.22:1**. Use
`border.structural` or spacing/elevation cues for required boundaries; never
make these close surface values carry structure by themselves.

### Candidate support tokens

| Foreground / boundary | Background | Ratio | Intended use |
|---|---|---:|---|
| `text.secondary` `#B7BDC1` | `#1B1D20` | 8.90:1 | Supporting copy. |
| `text.secondary` `#B7BDC1` | `#262A2D` | 7.62:1 | Supporting copy. |
| `text.secondary` `#B7BDC1` | `#30363A` | 6.45:1 | Supporting copy on raised surface. |
| `text.muted` `#9AA3A9` | `#262A2D` | 5.64:1 | Secondary metadata. |
| `text.muted` `#9AA3A9` | `#30363A` | 4.77:1 | Lowest approved normal-text pairing; validate at scale. |
| `border.structural` `#73808A` | `#262A2D` | 3.57:1 | Necessary control or panel boundary. |
| `border.structural` `#73808A` | `#30363A` | 3.02:1 | Minimum approved necessary boundary pairing. |
| `border.required` `#91AAB2` | `#262A2D` | 5.92:1 | Required/invalid field boundary. |
| `state.errorText` `#FF8A8A` | `#262A2D` | 6.38:1 | Optional error copy; current default uses primary text and a red accent. |
| `state.errorText` `#FF8A8A` | `#30363A` | 5.40:1 | Optional error copy on raised surface. |
| `text.onAccent` `#121416` | Audio `#00C2D1` | 8.47:1 | Filled audio-accent button. |
| `text.onAccent` `#121416` | MIDI `#CC5EFF` | 5.81:1 | Filled MIDI-accent button only if MIDI exists. |
| `text.onAccent` `#121416` | Control `#E4793A` | 6.24:1 | Filled control-accent button only if control feature exists. |
| `text.onAccent` `#121416` | Analyzer `#9DDC4A` | 11.23:1 | Filled analyzer-accent button, if needed. |
| `text.onAccent` `#121416` | Selected `#F39A57` | 8.41:1 | Selected filled action, only if a filled style is required. |
| `text.onAccent` `#121416` | Warning `#D7A93F` | 8.47:1 | Filled warning action; retain warning semantics in icon/text. |
| `text.onAccent` `#121416` | Error `#D45353` | 4.54:1 | Filled destructive/error action, at minimum margin; prefer an explicit destructive button style and retest. |

Do not render normal-size text over alpha-blended colors or glow. Use a
validated opaque surface for text and a separate outline/icon for semantic
color. In particular, off-white `#F2F1EE` is not a suitable label color on
these bright accent fills.

## Semantic and interaction-state rules

| Element/state | Primary cue | Secondary cue | Must not imply |
|---|---|---|---|
| Audio cable or waveform | Audio accent plus line direction/port labels | Width/selection geometry | MIDI/control or node selection. |
| MIDI/event route | Reserved violet plus event glyph or patterned line | Direction and label | A shipped MIDI feature before one exists. |
| Control/modulation route | Reserved orange plus distinct line style/glyph | Direction and label | Warning, success or selection by hue alone. |
| Analyzer visualization | Green plot/mark plus units and source | Clearly labeled scope/meter/tuner identity | That audio is running when stopped or stale. |
| Selected node/port/cable | Crisp outline, handles and selection label/state | Selected orange accent `#F39A57` | A change of signal type. Never recolor a cable as the sole selection cue. |
| Keyboard focus | Separate high-contrast white ring, preferably dashed or double when selected | Visible traversal and activation state | Selection, hover or signal identity. |
| Warning/error | Icon/shape plus explicit diagnostic copy | Gold/red accent edge or badge | A module family or signal category. |
| Hover/pressed/disabled | Neutral surface lift/depression and enabled state | Border, pointer/focus and readable name | A semantic color change. |

The selection orange and control orange are close in hue. Keep selection
meaning in **geometry**: outline the selected node, expose its handles and
show a selected-state label or inspector context. Retain the selected accent
only as a reinforcing cue. A selected control cable must remain recognizably
control-semantic by its line style/label; selected and focused states must be
distinguishable at once.

Analyzer green is not a generic “good” color. Warning and error must never be
encoded as analyzer/control/audio color. The current product may use only the
semantics it actually implements; future MIDI/control colors remain reserved.

## Glow and material treatment

Glow is optional, local emphasis—not a substitute for contrast, hierarchy or
selection geometry.

* **Allowed:** a soft halo around a focused control, the selected node's outer
  edge, or a live signal point/trace when that source is genuinely live. Keep
  the crisp core/outline fully visible.
* **Implemented bounds:** selected nodes receive three nested, bounded strokes
  extending at most **9 logical pixels** beyond the node, at **0.12, 0.16 and
  0.20 opacity** before compositing. Their crisp orange outline and handles
  remain visible with glow disabled. These are lightweight strokes, not a
  blurred full-panel effect.
* **Performance:** no full-panel blur, repeated large-area Gaussian effect or
  per-frame allocation. Prefer a small cached gradient or a bounded,
  low-opacity JUCE drawing pass; do not regenerate blurred assets in the
  audio callback or couple visual effects to DSP work.
* **Forbidden:** glowing body text, persistent haze around every panel,
  always-on pulse/blink, broad rainbow fills, animated warning/error, and
  glow that is required to see focus, selection, clipping or route direction.
* **Reduced effects:** `View > Reduced effects (no glow)` disables the selection
  halo for the current application run. It keeps the same crisp outline,
  handles, labels, focus ring, icon and state. The setting is not persisted.

Keep the shell neutral and instrument-like rather than turning every card or
module into a saturated tile. Use the supplied accents as small, deliberate
marks, traces, handles and focused controls. Use the four-pixel spacing
rhythm, typography hierarchy, unit labels and native scaling/accessibility
principles in `docs/VisualDesignLanguage.md`; do not shrink dense copy to make
the palette fit.

## Implemented usage and reserved semantics

* **Module:** a neutral charcoal body, a concise title/role, and one narrow
  family rail or glyph. The analyzer uses a green rail; other current nodes
  remain neutral. A selected node gains an orange outline/handles while its
  audio ports remain cyan.
* **Cable:** audio routes use cyan and preserve endpoint/direction geometry.
  Selection adds an orange outer stroke while the cyan core remains visible.
  Control cables are future semantics and are not represented as shipped.
* **Meter:** keep its background neutral, scale/ticks readable and units
  explicit. Use a green analyzer trace only while measured data is available;
  show stopped/stale/absent state in text or symbol, not by switching to
  generic green “ready.”
* **Tuner:** the shipped tuner uses a neutral readout, explicit note/octave and
  cents, a centered in-tune mark, and textual confidence/silence/stale
  feedback. The Analyzer color emphasizes the measured needle; warning is
  separately labeled.
* **Audio Setup:** neutral dialog and fields; required/invalid controls use a
  crisp boundary plus a label/diagnostic. Selection, keyboard focus, pending,
  unavailable and failure must remain independently identifiable.

## Accessibility and visual acceptance checklist

- [x] Normal text meets 4.5:1 on its actual opaque surface; large text and
      necessary non-text indicators meet 3:1. Decorative boundaries are not
      the only cue for interactive state.
- [ ] MIDI, analyzer and muted text remain legible at the lowest supported
      size and in actual JUCE rendering; no alpha/compositing assumption is
      used to claim a text contrast pass.
- [x] Accent-filled buttons use the validated dark foreground, including the
      red/destructive variant; off-white text is not placed on bright fills.
- [ ] The distinction between signal type, selected state, keyboard focus,
      hover, warning and error survives grayscale and common color-vision
      deficiencies through shape, text, line style and geometry.
- [ ] Focus remains visible with glow disabled; selected-plus-focused state
      is unambiguous. Keyboard navigation and screen-reader labels describe
      action and direction, not just color.
- [x] Audio/MIDI/control/analyzer semantics are applied only to implemented
      features. Stopped, absent and stale data do not animate or look live.
- [x] No glow obscures labels, routes, port identity, grid, meter scale or
      tuner cents. Reduced-effects mode preserves the complete state meaning.
- [ ] Review at agreed minimum window size, 100/150/200% Windows scaling and
      representative macOS scaling; check keyboard and high-contrast modes.
- [ ] Visual checks use screenshots from the actual build, labeled with
      platform, scale and build/source commit; synthetic and injected-device
      screens are not presented as physical-hardware qualification.

## Bounded implementation and review sequence

1. Approval of this exact palette and role document is recorded in the
   implementation request.
2. Contrast ratios are codified in focused presentation checks; the complete
   JUCE shell, graph nodes/cables, visualizers and focus/selection states use
   the semantic theme tokens.
3. Representative synthetic JUCE screenshots and Release builds are the
   validation evidence. Actual 100/150/200% scaling and macOS remain unverified
   and must be completed before claiming cross-platform visual qualification.
4. The canonical `docs/VisualDesignLanguage.md` records the active palette
   while distinguishing shipped behavior from future signal semantics.

**Non-goals:** changing DSP semantics, claiming vendor parity, implying
unimplemented MIDI/control/oscillator features, redesigning all screens, or
using glow as a permanent animated visual layer.
