# Typography and Font System

**Status:** target specification; partially implemented. Bundled faces, shared
role helpers, OpenType numeric/code features, theme fonts and canvas text tiers
are shipped by [implementation layer 1](plans/07-TypographyAndRoutingImplementation.md).
The accessibility/reflow preferences, richer editor and Glance-mode proposals
below remain targets, not a claim of completion or cross-platform qualification.
It complements `docs/VisualDesignLanguage.md` and
`docs/plans/01-VisualDesignGuidelines.md`.

## 1. Principles

Visual Audio Lab is used for hours, on one or two monitors, to read numbers
quickly. Typography is therefore treated as instrument labelling, not
document text.

1. **Values over labels.** A parameter value is the thing the user acts on. It
   is larger, brighter and heavier than its label. Labels are quiet.
2. **Numbers never jitter.** Every changing number uses tabular (fixed-width)
   figures, so meters and sweeps do not shimmer or shift neighbours.
3. **One UI family, one code family.** Fewer faces means a calmer,
   more instrument-like surface.
4. **Bundle, don't assume.** Fonts ship with the app so Windows and macOS
   render identically; OS fonts are fallbacks only.
5. **Never encode meaning by size or colour alone.** Pair with weight, unit,
   shape or text (accessibility, §10).
6. **Avoid the office look.** No default system-dialog styling, no
   Visual-Studio-style dense grey chrome; small caps-like section labels and
   generous value sizes give the instrument character.

## 2. Primary UI font

### 2.1 Candidates

| Font | Strengths | Weaknesses | Verdict |
|------|-----------|------------|---------|
| **Inter** | Designed for screens; tall x-height; excellent small-size hinting; variable weights (100–900); **tabular figures, slashed zero, case-sensitive forms**; OFL (bundleable); same on Win/mac | Neutral; common in web apps | **Selected** |
| Segoe UI / Segoe UI Variable | Native Windows, familiar | Not on macOS; reads as "Windows app"; no true tabular-figure control in all UI stacks | Windows fallback |
| SF Pro | Excellent, native macOS | Licence forbids bundling for Windows; macOS only | macOS fallback |
| Roboto | Open, widely available | Slightly narrow, "Android" feel; weaker numeral alignment | Last-resort fallback |
| IBM Plex Sans | Strong technical character, OFL | Wider letterforms cost node space; numerals less compact; heavier "corporate" tone | Rejected as primary |
| Source Sans 3 | Compact, open, readable | Dated proportions; low x-height at small sizes | Rejected |
| Manrope / DM Sans | Friendly, geometric | Looser, less precise at 10–11 px; weaker numerals | Rejected |
| Barlow / Rajdhani style display faces | Instrument-panel flavour | Poor legibility at body sizes and for long use | Display use only, not adopted |

### 2.2 Decision

**Primary UI font: Inter (variable).**

Why:

- Its small-size legibility is the best of the open candidates, which matters
  for 9–11 px node labels.
- OpenType features directly serve this product: `tnum` (tabular figures),
  `zero` (slashed zero, avoids 0/O confusion in IDs and channel names),
  `case` (all-caps labels sit correctly), `ss01`/`cv11` alternates where
  useful.
- It is the closest match to the clean-but-technical feel of FabFilter,
  Bitwig and Reaper's modern themes without looking like default OS chrome.
- A single bundled variable file gives every weight needed, so Windows and
  macOS are pixel-comparable, and weight can shift continuously (see §8 for
  zoom compensation).

### 2.3 Fallback stack (UI)

1. Inter (bundled)
2. Windows: Segoe UI Variable → Segoe UI
3. macOS: SF Pro Text (system-ui) → Helvetica Neue
4. Roboto → Arial (last resort)

Fallbacks keep the same *size table*; if the primary font fails to load, the
app must not change layout, only glyph shapes. Tabular-figure support is
checked at start-up; if a fallback lacks it, value fields reserve a fixed
width so numbers still do not shift.

## 3. Code font

### 3.1 Candidates

| Font | Strengths | Weaknesses | Verdict |
|------|-----------|------------|---------|
| **JetBrains Mono** | Designed for long coding sessions; tall x-height at small size; clear `0`/`O`, `1`/`l`/`I`; optional ligatures; OFL; bundleable on both OSes | Slightly tall line height | **Selected** |
| IBM Plex Mono | Distinctive, readable, OFL | Wider advance; softer serifs make dense code busier | Strong alternate |
| Cascadia Mono | Good, ships with Windows Terminal, OFL | Not native on macOS (bundleable, but less refined hinting at 11–12 px) | Windows fallback |
| Consolas | Familiar to Delphi/VS users, ClearType-tuned | Windows/Office-only licence; absent on macOS; thin on high-DPI | Fallback only |

### 3.2 Decision

**Standard code font: JetBrains Mono (Regular 400, Bold 700 for keywords).**
Ligatures are **off by default** so technical notation such as `<=`, `:=` and
`->` remains visually explicit.

Fallback stack (code): JetBrains Mono → Cascadia Mono → Consolas (Windows) →
SF Mono → Menlo (macOS) → monospace.

Used for: technical identifiers, expression and formula fields, hex/bit and
sample-index columns, log panes, and file paths in error text. It is **not**
used for ordinary numeric parameter readouts.

## 4. Data typography

### 4.1 Rule

**Numeric data uses Inter with tabular figures (`tnum`) and slashed zero
(`zero`) — not the monospace font.** Units follow the number and are typeset
smaller and quieter.

### 4.2 Rationale

- Monospace faces make short readouts like `48 kHz` look like code and widen
  every node. Inter tabular figures give equal-width digits with
  proportionate spacing around them, so numbers align and the UI keeps its
  instrument look.
- Parameter fields are narrow; tabular Inter is about 15% more compact than
  JetBrains Mono at the same size.
- Tabular figures prevent jitter when a meter or a drag changes `-18.4` to
  `-9.8`.
- The monospace font is reserved for *text that is code*, so users can read
  "monospace = something I can type or paste".

Exceptions where mono is allowed: multi-column numeric tables (FFT peak list,
sample dumps, MIDI/hex), and literal technical values.

### 4.3 Formatting rules

| Rule | Example |
|------|---------|
| Space between number and unit (thin/regular space, non-breaking) | `48 kHz`, `3.2 ms` |
| Unit is 80% size of the number, medium-low contrast, same baseline | **48** <sub>kHz</sub> |
| Minus sign is a true minus (U+2212), never a hyphen | `−18.4 dB` |
| Fixed decimals per parameter, never trailing jitter | `−18.4 dB`, not `−18.40` then `−18.4` |
| Use SI prefix switch at thresholds | `999 Hz` → `1.00 kHz` |
| Sample rates | `48 kHz`, `44.1 kHz`, `96 kHz` |
| Buffer/latency | `64 samples` · `1.3 ms` · `Round trip 5.8 ms` |
| Frequencies | `440 Hz`, `1.25 kHz`; note names in secondary text: `A4 +0 ct` |
| Levels | `−18.4 dB`, `−∞ dB` (U+221E), `0.0 dBFS`; `LUFS`, `LU`, `dBTP` for loudness |
| Unknown / unavailable value | `—` (em dash), never `0` or `NaN` |
| Locale | Decimal separator follows the OS locale; internal values never change |

### 4.4 Colour and weight for values

| Element | Weight | Colour role |
|---------|--------|-------------|
| Value number | 600 | Primary text (highest contrast on the surface) |
| Unit | 500 | Secondary text |
| Label | 500, small, uppercase option | Tertiary text |
| Value out of safe range / clipping | 700 + icon/underline | Warning/danger plus a non-colour cue |

## 5. Typography hierarchy

All sizes are in **logical px at 100% scale**, scaled by OS DPI and the user
text-size preference (§9). Line height is given as a multiple of the size.

| Role | Font | Weight | Size | Line height | Notes |
|------|------|--------|------|-------------|-------|
| Application title | Inter | 600 | 17 | 1.2 | Title bar / about; tracking −0.01 em |
| Panel title | Inter | 600 | 15 | 1.3 | Sentence case, tracking +0.01 em |
| Section title | Inter | 600 | 14 | 1.3 | UPPERCASE, tracking +0.04 em, tertiary colour |
| Node title | Inter | 600 | 15 | 1.25 | Colour-coded by category via accent bar, not text colour |
| Control label | Inter | 500 | 13.5 | 1.25 | Secondary/tertiary colour |
| Parameter value | Inter, `tnum zero` | 600 | 16 | 1.15 | Larger than its label |
| Parameter unit | Inter | 500 | 13.5 | 1.15 | Quieter than value |
| Meter scale (tick labels) | Inter, `tnum` | 500 | 13 | 1.1 | Fall back to fewer ticks, never smaller |
| Meter readout (peak/RMS/LUFS) | Inter, `tnum zero` | 600 | 18 | 1.1 | Larger on analysis panels (§9) |
| Code | JetBrains Mono | 400 | 14 | 1.5 | 1.45–1.55 acceptable |
| Code line numbers | JetBrains Mono | 400 | 13 | 1.5 | Muted |
| Diagnostics / compile messages | JetBrains Mono | 400 (message), 700 (severity tag) | 13.5 | 1.45 | |
| Documentation / help body | Inter | 400 | 15 | 1.55 | Max line length 70–80 characters |
| Documentation headings | Inter | 600 | 20 / 16 / 14 | 1.3 | H1/H2/H3 |
| Documentation inline code | JetBrains Mono | 400 | 12.5 | inherit | Subtle background tint |
| Status bar | Inter, `tnum` | 500 | 13 | 1.2 | Fixed-width segments for CPU/latency/rate |
| Tooltips | Inter | 400 | 13 | 1.35 | Value in 600 |
| Menus / dialogs | Inter | 400 | 14 | 1.35 | Dialog titles 16/600 |

**Readability revision:** user review of the first implementation found its
original 10–12 px roles and 10 px canvas floor too small in the actual desktop
window. The shared roles now use at least 13 px logical size for essential UI
text, with a 14 px on-screen minimum for canvas labels; values/headings are
larger. At small patch zoom, detail is reduced instead of shrinking operating
labels. This is a practical legibility floor, not the deferred OS-level text
size/high-contrast preference or proof of cross-platform behavior.

## 6. Parameters and the values-over-labels pattern

### 6.1 Layout pattern

Label above, value beneath, stacked and left-aligned (or centred above a
knob). No colon.

```
 Preferred                      Less preferred

  THRESHOLD                      Threshold: −18.4 dB
  −18.4 dB
```

Typographic treatment:

- Label: 10.5 px / 500 / tertiary colour, optional uppercase.
- Value: 13 px / 600 / primary colour, unit in 10.5 px / 500 / secondary.
- Vertical gap label→value: 2 px. Control→label gap: 6 px.
- Horizontal (inline) layout is permitted only in narrow lists where the
  stacked layout cannot fit, and then **the value is right-aligned and
  heavier**, the label left-aligned and quiet.

### 6.2 Editing state

On click/double-click the value becomes an editable field: same font, size and
weight, a visible caret, selection highlight, and the unit stays visible in
secondary colour. The field never changes width during editing.

### 6.3 Drag feedback

While dragging, the value is temporarily enlarged to 15 px in a floating
read-out above the pointer so a hand does not cover it.

## 7. Node typography

### 7.1 Example: Compressor

```
 ┌─────────────────────────────────────────┐
 │▌ Compressor                      ⏻  ⋯   │   Title 15/600
 │                                         │
 │  THRESHOLD      RATIO         ATTACK    │   Labels 13.5/500
 │  −18.4 dB       4.0 : 1       12 ms     │   Values 16/600
 │                                         │
 │  GAIN REDUCTION                         │
 │  ▮▮▮▮▮▯▯▯▯▯▯▯▯▯▯▯          −6.2 dB     │   Readout 18/600 tnum
 └─────────────────────────────────────────┘
```

### 7.2 Semantic zoom (canvas zoom levels)

Text does not scale linearly with the canvas. Node content has four **levels
of detail**; each level defines what is shown, not just how big.

| Zoom | LOD | Visible text |
|------|-----|--------------|
| ≥ 110% | **Full** | Title, all labels, all values with units, meters |
| 80–109% | **Compact** | Title, role and port labels, values with units |
| 35–79% | **Overview** | Title only; operating labels are hidden rather than compressed below readable size |
| < 35% | **Glyph** | Title only; values replaced by meter colour/bar |

Rules:

- The shipped canvas layer keeps its operational titles/labels at a minimum of
  **14 px on screen**; the user review rejected the smaller original target
  floor. If a tier cannot fit, hide that tier.
- Text size **does not exceed 1.25×** its 100% size when the canvas zoom is
  above 100%; nodes grow, text grows slower, so the layout is controls-first.
- Key value order for Overview/Glyph LOD is defined per module
  (Compressor: Gain Reduction, Threshold; EQ: active band frequency/gain;
  Delay: time; etc.).

### 7.3 Truncation rules

| Text | Rule |
|------|------|
| Node title | Middle-ellipsis preserving first and last words (`Multiband Comp…Master`); full title in tooltip; user can rename |
| Control label | Never truncated; replaced with the abbreviation first, then hidden (value keeps unit) |
| **Parameter value** | **Never truncated.** Reduce decimals, then switch SI prefix (`1250 Hz` → `1.25 kHz`); widen the field if needed |
| Unit | Never hidden when value is shown |
| Endpoint / device names (rails) | End-ellipsis after 24 chars, tooltip shows full name and channel list |
| Source names, file names | Middle-ellipsis to keep extension |

### 7.4 Weight compensation

On dark surfaces at small sizes, light text looks thinner. Use variable-font
weight: +25 weight units for text under 11 px on dark themes. At canvas
zoom-out the weight rises another +50 (max 700) to keep strokes visible.

## 8. Analysis modules

Analysis panels are read at a distance (second monitor, 1–2 m). They use a
**one step larger** scale and a *glance* hierarchy: one dominant number, then
supporting numbers, then axes.

| Element | Font | Weight | Size (100%) | Notes |
|---------|------|--------|-------------|-------|
| Primary readout (e.g. Integrated LUFS) | Inter `tnum zero` | 700 | 32 (resizes to 48 in "Glance" mode) | Unit at 40% size |
| Secondary readout (Short-term, LRA, True Peak) | Inter `tnum` | 600 | 18 | |
| Tertiary readout (min/max, labels) | Inter | 500 | 12 | |
| Axis labels (dB, Hz, time) | Inter `tnum` | 500 | 11 | Cannot drop below 10 |
| Cursor/hover readout | Inter `tnum` | 600 | 14 | Floating tag with background plate |
| Peak/marker labels | Inter `tnum` | 600 | 12 | Plate for contrast, leader line |

### 8.1 Module specifics

- **Oscilloscope:** time axis in ms with SI switch (`0.5 ms/div`); level axis
  in linear or dBFS; trigger info (`Trig ↑ −12 dB`) in the top-left at 12/600.
  Channel labels (`L`, `R`) in channel colour **plus** letter plus line style.
- **Spectrum analyzer:** frequency axis uses log labels (`20`, `50`, `100`,
  `200`, `500`, `1k`, `2k`, `5k`, `10k`, `20k`) at 11/500; dB axis labelled
  at 12 dB steps. Peak labels show `440 Hz · −18.4 dB · A4 +2 ct`. FFT
  settings (size, window, overlap, averaging) appear as quiet chips at 11/500.
  Numeric FFT tables use JetBrains Mono for column alignment.
- **Correlation meter:** scale `−1  0  +1` at 12/600 with the numeric value
  at 24/700 `+0.84`; text word beneath: `Mono-compatible` / `Phase issues`
  so colour is not the only carrier.
- **Loudness meter (EBU R128/ITU-R BS.1770):** Momentary / Short-term /
  Integrated / LRA / True Peak as a stacked column; Integrated is the primary
  readout; target (e.g. `−14 LUFS`) shown at 12/500 with a delta
  (`+0.6 LU`) at 18/600.

### 8.2 Distance legibility rules

- Primary data ≥ 24 px at 100% scale, secondary ≥ 16 px.
- Contrast ≥ 7:1 (AAA) for primary readouts on analysis panels.
- A **Glance mode** (per panel) scales primary readouts to 48–72 px and
  drops axes and secondary text.
- Updates throttle numeric text to ≤ 10 Hz for readability (graphics
  continue at display rate); peak-hold values freeze for 1.5 s.

## 9. Accessibility and platform support

## 10. Accessibility and platform support

### 10.1 Windows and macOS

| Topic | Windows | macOS |
|-------|---------|-------|
| Rendering | DirectWrite, grayscale or ClearType per OS setting | Core Text, native smoothing |
| Fonts | Bundle Inter and JetBrains Mono; fall back to Segoe UI Variable / Cascadia Mono | Bundle same files; fall back to SF Pro / SF Mono, Menlo |
| Text size | Follows "Make text bigger" **and** an in-app text-size slider (80–200%) | Follows an in-app slider (macOS has no global text-size on every release) |
| Parity | Size table identical on both platforms. Do not compensate for the macOS 72-dpi vs Windows 96-dpi legacy; use logical px |

### 10.2 High DPI and mixed DPI

- Size in logical units, snapped to a half-pixel grid; font hinting is light
  on ≥ 144 DPI and full on 96 DPI.
- When a window moves between monitors with different DPI, text is re-laid out
  at the destination scale; layout widths use logical units so nothing
  re-wraps unexpectedly.
- Fractional scales (125%, 150%, 175%) get the same QA as 100% and 200%.
- Hairlines (axis ticks) align to device pixels; text never does subpixel
  positioning in meter readouts (prevents shimmering).

### 10.3 Ultrawide and multi-monitor

- Panel typography does not scale with window width. Extra width adds
  columns or space, not larger text.
- Analysis panels can enter Glance mode when floated on a second monitor
  or when their window is large.
- Max line length for documentation text is 80 characters, centred in wide
  panes.

### 10.4 Colour blindness

- Text roles, syntax, meters and severity use **shape, glyph, weight or
  position** as well as colour (✕ ▲ ⓘ, bold/italic/underline).
- The default theme is verified against protanopia, deuteranopia and
  tritanopia simulators; channel colours (L/R) also carry letter labels.
- Warning/danger text is never the only indicator of clipping: a `CLIP`
  badge accompanies it.

### 10.5 Low vision

- Contrast: body text ≥ 4.5:1 (AA), primary readouts ≥ 7:1 (AAA), disabled
  controls still ≥ 3:1.
- Text-size slider scales all text (not just editor) between 80% and 200%;
  layouts reflow rather than clip. Node LOD thresholds use effective
  on-screen size, so larger text keeps legible LODs.
- High-contrast theme uses pure foreground/background pairs and 1 px outlines;
  honours Windows High Contrast and macOS "Increase contrast".
- Reduced motion disables animated value tweening on readouts.
- Screen readers receive full strings (`Threshold, minus 18.4 decibels`), not
  the glyphs; meters announce on request, not continuously.
- Do not use light (< 400) weights below 14 px; do not use all-caps for
  strings longer than ~16 characters.

## 11. Styling rules summary

| Rule | Detail |
|------|--------|
| Case | Sentence case for titles and menus; UPPERCASE only for section titles and short control labels (≤ 16 characters) |
| Tracking | Uppercase text +0.06 em; ≥ 14 px headings −0.01 em; code 0 |
| Alignment | Labels left; numeric values right in lists, left in stacks; tables align decimals |
| Colour roles | Primary (values, titles), Secondary (units, body), Tertiary (labels, axes), Disabled |
| Weights in use | 400, 500, 600, 700 only (four per family) |
| Italic | Comments only (code font) and rare emphasis in docs |
| Underline | Links and diagnostics only |
| Emoji / icon fonts | Not used as text; icons are vector assets |
| OpenType | UI: `tnum`, `zero`, `case`; Code: ligatures off |
| Glyph coverage | Required: − ∞ ± × ÷ ° µ Δ ↑ ↓ → ✕ ▲ ✓ ⏻ ♯ ♭ and Latin Extended |
| Localisation | Fallback chains include Noto Sans for CJK, Arabic, Hebrew, Devanagari; layout allows +35% string growth |

## 12. Worked examples

### 12.1 Parameter stack (Delay)

```
  TIME            FEEDBACK        MIX
  250 ms          35 %            22 %
```

### 12.2 Value formatting

```
  Frequency          440 Hz          (A4 +0 ct)
  Gain               −18.4 dB
  Latency            3.2 ms
  Sample rate        48 kHz
  Buffer             64 samples
  Ceiling            −∞ dB
```

### 12.3 Status bar

```
 ● Running   48 kHz · 64 samples · 1.3 ms    CPU 12 %    Dropouts 0    Out: Main
```
Segments are fixed-width tabular so changing numbers never move neighbours.

## 13. Rationale summary

| Decision | Why |
|----------|-----|
| Inter as UI font | Best small-size legibility, tabular figures and slashed zero, OFL, identical on both OSes, not an "office" face |
| JetBrains Mono for code | Built for long sessions, clear ambiguous glyphs, OFL, familiar to modern developers |
| Numbers in the UI font, not mono | Compact, instrument-like, `tnum` removes jitter; mono is kept for code |
| Values heavier than labels | Matches how musicians and engineers work: eyes go to the number |
| Semantic zoom with minimum sizes | Keeps nodes readable at all canvas scales without unreadable microtype |
| Larger analysis scale + Glance mode | Second-monitor, at-a-distance use |
| Non-colour cues everywhere | Colour-blind and low-vision safety |

## 14. Licensing and packaging notes

- Inter and JetBrains Mono are licensed under the SIL Open Font License 1.1;
  they may be bundled and redistributed with the application, with the licence
  text included in the third-party notices.
- SF Pro and SF Mono may not be bundled; they are used only as macOS system
  fallbacks.
- Ship variable-font files plus static Regular/Semibold/Bold subsets for
  platforms with weak variable-font support.

## 15. Verification plan (non-binding)

1. Render the size table on Windows (100/125/150/200%) and macOS (1×/2×) and
   review screenshots side by side.
2. Verify no number shifts while a value sweeps through every format.
3. Check node LODs at each zoom threshold with long/localised strings.
4. Verify contrast and colour-blind simulations for text roles and syntax.
5. View analysis panels from ~1.5 m on a second monitor in Glance mode.
6. Run a four-hour reading/editing session test with feedback on fatigue.
