# Routing Model Redesign: Fixed Sources and Destinations

**Status:** target architecture and UX design. The first executable shared-bus
layer supports explicit Sum, multiple roots, fan-out and legal inert nodes. Its
canvas follow-up adds fixed-looking Sources/Destinations rails that group the
existing Input, generator and Main Output graph nodes and prevent their canvas
move/delete. This is a presentation layer over the version-1 graph, **not**
system-owned endpoint lifetime/readiness/bindings. Independent typed buses,
layout adapters and multiple destinations below remain unshipped. See the
[implementation boundary and dependency order](plans/07-TypographyAndRoutingImplementation.md).

## 1. Review of the current model

Today the patch is a single chain:

```
Input Node → Processing Nodes → Output Node
```

Facts from the engine (`AudioGraph.cpp`, `AudioProcessorNode.h`,
`docs/plans/03-OutputOnlyProcessingPlan.md`):

- `InputNode` copies corresponding hardware input channels; `OutputNode`
  copies one upstream bus to hardware outputs.
- A non-empty graph requires **exactly one Output**; each node accepts **at
  most one upstream source**; every node must reach Output; there is **no
  implicit summing**.
- Input width must equal output width (1–32 channels).
- Input is optional (output-only workflows exist) but is expressed through
  Audio Setup, not the patch.

### Problems

| # | Problem | Consequence |
|---|---------|-------------|
| 1 | Input/Output are ordinary nodes the user must place and wire | Every new patch starts with boilerplate; deleting them yields a broken graph |
| 2 | One Input and one Output only | Cannot monitor to headphones while recording, or mix a generator with a mic |
| 3 | Sources of different kinds (device, generator, file) are modelled differently (hardware Input vs. `GeneratorNode`) | Inconsistent mental model |
| 4 | Device configuration lives in Audio Setup, graph in canvas | Users cannot see which physical channel a wire represents |
| 5 | Equal in/out channel width | Mono mic → stereo speakers is impossible without hidden workarounds |
| 6 | No summing | Two sources cannot meet without a mixer module |

## 2. Target model

```
 SOURCES (fixed, left)        PROCESSORS (user-created)       DESTINATIONS (fixed, right)
┌──────────────────┐                                         ┌──────────────────┐
│ Hardware Inputs  │──┐    ┌─────┐    ┌────────────┐     ┌──▶│ Main Output      │
│ Signal Generators│  ├───▶│ EQ  │───▶│ Compressor │──┬──┤   │ Headphones       │
│ App Capture      │  │    └─────┘    └────────────┘  │  └──▶│ Recorder         │
│ File Players     │──┘                               └─────▶│ File Writer      │
│ Virtual Devices  │                                         │ Streaming Output │
└──────────────────┘                                         └──────────────────┘
```

The canvas has three zones. Sources and destinations are **system-owned,
always present, not deletable or movable**; processors are **user-owned**.
Cables run left → right only.

### 2.1 Node taxonomy

`InputNode` and `OutputNode` are retired and replaced by three roles that
share the existing node base (`AudioNode`) and a common port/bus contract.

| Node | Owner | Ports | Lifetime | Replaces |
|------|-------|-------|----------|----------|
| **SourceNode** | System, per available source | 0 inputs, ≥1 output *bus* | Exists while the source is configured/available; can be disabled but not deleted | `InputNode`, `GeneratorNode` as a product source |
| **ProcessorNode** | User | ≥1 input bus, ≥1 output bus | Created/deleted by the user | All existing DSP/script/plugin nodes |
| **DestinationNode** | System, per available endpoint | ≥1 input bus, 0 outputs | Exists while configured; can be disabled but not deleted | `OutputNode` |

Concrete kinds (data, not new classes — a `kind` discriminator plus
kind-specific settings):

- **SourceNode kinds:** Hardware Input, Signal Generator, Application
  Capture, File Player, Virtual Audio Device.
- **DestinationNode kinds:** Main Output, Headphones, Recorder, File Writer,
  Streaming Output.
- **ProcessorNode kinds:** EQ, Compressor, Delay, Reverb, VAL Script, VST3
  Plugin, plus existing modules (Gain, analyzers, …).

### 2.2 Bus, port and channel model

- Every port carries a **bus** with an explicit **channel layout**:
  Mono (1), Stereo (2), or Multichannel (N, 3–32, with a named layout such as
  5.1, 7.1, or "Custom N").
- A SourceNode exposes **one output bus per logical group** of channels. A
  hardware interface with 8 inputs exposes, by default, 8 mono buses plus
  user-defined stereo/multichannel *groupings* (e.g. "Ch 1-2 Stereo"). A
  grouping is just a named view over the same physical channels; a channel can
  belong to several groupings but only one may be *active in a patch* at a
  time (prevents accidental double-use of a converter).
- A DestinationNode mirrors this for output channels.
- Processors declare layout capability: *follows input*, *fixed*, or
  *adapting* (e.g. mono in → stereo out for Reverb).
- **Channel width is a property of a cable**, not of the whole graph. This
  removes the current global "input width == output width" rule.

### 2.3 Connection rules

1. Cables go Source → Processor → Processor → Destination, or Source →
   Destination directly (a valid patch with zero processors).
2. **Matching layout:** direct connection when layouts are equal.
3. **Auto-adaptation:** mismatches never fail silently. The canvas inserts a
   visible, removable **adapter chip** on the cable (see §4) with a named
   rule:
   - Mono → Stereo: *Dual-mono* (default) or *Pan*.
   - Stereo → Mono: *Sum −3 dB* (default), *L only*, *R only*.
   - N → M multichannel: *Channel map* (editable matrix).
   Adapters are engine-level, deterministic, and appear in the project file;
   they are not hidden conversions.
4. **Fan-out is free:** one output bus may feed many inputs.
5. **Fan-in is explicit:** an input port accepts one cable by default. A
   port flagged *summing* (all Destinations and a Mixer-style processor input)
   accepts several cables and shows a **sum badge** with per-cable gain/mute.
   This keeps the "no implicit summing" invariant: summing happens only where
   the user can see it.
6. **No cycles** (feedback requires an explicit Feedback/Delay-loop
   processor, a future feature).
7. A **processor with no path to any destination** is shown dimmed with a
   "not routed" hint; it is legal but inert (replaces the current hard
   "every node must feed Output" failure with a visible state).

### 2.4 Engine and validation changes (conceptual)

| Area | Current | Proposed |
|------|---------|----------|
| Root requirement | Exactly one Output | Zero or more enabled Destinations; empty graph still renders silence |
| Source requirement | Optional Input via Audio Setup | Any enabled Source; no source ≠ error |
| Fan-in | Single upstream, no summing | Single by default, explicit summing ports |
| Width | Global in==out | Per-bus, with adapters |
| Start validation | Device + graph conflated | Per-endpoint readiness (below) |
| Execution order | Topological from Input | Topological from all enabled Sources, evaluated only on subgraphs reaching an enabled Destination |

**Endpoint lifecycle** — each Source/Destination has a state shown on the
node: *Ready*, *Disabled*, *Missing* (device unplugged), *Error*. Missing or
errored endpoints keep their wires; the signal path is muted and flagged
rather than the project being rejected. This preserves the existing rule that
audio starts only on explicit Start and no endpoint is silently substituted.

**Real-time vs. non-real-time endpoints:** Hardware, App Capture, Virtual
Device, Main Output, Headphones and Streaming are clock-bound; Signal
Generator and File Player are free-running and sample-accurate; Recorder and
File Writer are *sinks with a lock-free ring buffer to a writer thread*, never
blocking the audio callback. Sample-rate differences between endpoints are
handled at the endpoint boundary (resampler inside the endpoint, not in the
graph).

### 2.5 Project format (conceptual)

- Replace `"type": "Input" | "Output"` nodes with an `endpoints` section
  keyed by stable endpoint IDs (`src:hw:0`, `dst:main`), each with `kind`,
  device binding, enabled flag and channel groupings.
- Processor nodes reference endpoints only via `cables`, each cable carrying
  `from {node, port}`, `to {node, port}`, and optional `adapter`.
- **Migration:** a legacy project's `Input` becomes the default Hardware
  Input source (or "none" if absent); `Output` becomes *Main Output*;
  its single wires become cables; width mismatches cannot exist in legacy
  files, so no adapters are inserted. Layout positions are discarded for
  endpoints (they are fixed) and kept for processors.
- Bump the project schema version; loading older files migrates in memory and
  prompts to save as the new version.

## 3. Canvas architecture

```
┌ Sources ─────┬────────── Patch area ──────────┬ Destinations ┐
│  (fixed)     │      (pan/zoom, user nodes)    │   (fixed)    │
└──────────────┴────────────────────────────────┴──────────────┘
```

- **Rails:** Source and Destination rails are pinned to the viewport edges
  and **do not pan or zoom with the patch area**; cables run from the rail
  port into the scrolling area and stay attached. Rail contents are vertically
  scrollable and groupable (collapsible by kind).
- **Patch area:** pans/zooms; hosts ProcessorNodes. Dropping a module from the
  palette places it at the cursor; dropping it *onto a cable* splices it in
  (re-routing both sides, with adapters if needed).
- **Rail cards** show: icon, name, state dot, channel groupings, and a live
  **level meter per bus**. Click → inspector (device, channel mapping, gain,
  mute, monitor).
- **Port color/shape:** one color per layout (mono, stereo, multichannel);
  multichannel ports are drawn as a fat double-line with a channel count.
  Consistent with `docs/VisualDesignLanguage.md`.
- **Cable behavior:** drag from any compatible port; incompatible targets
  dim, compatible ones glow; "adaptable" targets get a dashed ring.

## 4. UX wireframes

Legend: `●` port, `══` stereo cable, `──` mono cable, `≡≡` multichannel
cable, `[ ]` adapter chip, `▮` meter, `Σ` summing port.

### 4.1 Mono routing

Use case: one microphone through EQ and Compressor to the main output on both
speakers.

```
 SOURCES                      PATCH                          DESTINATIONS
┌───────────────────┐                                      ┌──────────────────┐
│ ▼ Hardware Inputs │                                      │ ▼ Main Output    │
│ ┌───────────────┐ │   ┌────────┐     ┌────────────┐      │ ┌──────────────┐ │
│ │ In 1 (Mic) M ▮●──────▶● EQ  ●──────▶● Compress  ●─────┐│ │ L+R  (2ch)   │ │
│ │ In 2        M ▮●│   └────────┘     └────────────┘     ││ │              │ │
│ └───────────────┘ │                                     │└─▶● Σ      ▮▮   │ │
│ ▼ Signal Gens     │                                [M→S │  │              │ │
│   Sine 440 Hz  M ●│                                 dual]  └──────────────┘ │
└───────────────────┘                                      │ ▶ Headphones     │
                                                           └──────────────────┘
```

Behavior:
- Mono cable (thin line) from `In 1`; the Main Output port is stereo, so a
  **`[M→S dual]` chip** appears on the cable automatically. Click to switch
  to *Pan*.
- Hovering a chip shows "Mono → Stereo, dual-mono, 0 dB".
- A mono source can also feed *Headphones* in parallel (fan-out) with no
  extra nodes.

### 4.2 Stereo routing

Use case: stereo file player through Reverb to Main Output and Recorder.

```
 SOURCES                      PATCH                          DESTINATIONS
┌───────────────────┐                                      ┌──────────────────┐
│ ▼ File Players    │      ┌──────────────┐           ┌───▶● Main Output  ▮▮ │
│ ┌───────────────┐ │      │   Reverb     │           │   │   L R           │
│ │ song.wav    S ▮▮●═════▶●  mix 25 %    ●═══════════╪═══┘                  │
│ │ ▶ ■  00:42    │ │      └──────────────┘           │   ┌──────────────────┤
│ └───────────────┘ │                                  └──▶● Recorder   ●●  │
│ ▼ Hardware Inputs │                                      │ rec_01.wav ●REC │
│   In 1-2 (Stereo) │                                      └──────────────────┘
└───────────────────┘
```

Behavior:
- Stereo is a single thick cable; ports show `L R` on hover.
- Fan-out from Reverb to two destinations; no mixer needed.
- Recorder and File Writer cards expose **Arm/Rec** and format (WAV/FLAC,
  bit depth) in the inspector; the audio callback is never blocked.
- Dropping a Compressor onto the cable between Reverb and Main Output
  inserts it; the Recorder branch stays pre-compressor (the user sees this
  immediately).

### 4.3 Multichannel routing

Use case: 8-channel interface, drums on channels 1–8, processed per group and
delivered to a stereo bus and a 5.1 stream output.

```
 SOURCES                     PATCH                             DESTINATIONS
┌──────────────────────┐                                      ┌─────────────────┐
│ ▼ Hardware Inputs    │                                      │ ▼ Main Output   │
│  ┌──────────────────┐│     ┌───────────────┐                │  L R            │
│  │ Drums 1-8  (8ch)≡≡●≡≡≡≡▶● Channel Map   ●══[8→2 map]═══╗ │  ┌────────────┐ │
│  │ ▮▮▮▮▮▮▮▮         ││     │  (VAL Script) │              ╚▶● Σ  stereo  │ │
│  └─ expand ▾        ││     └───────────────┘                │  └────────────┘ │
│   1 Kick  ●          ││                                      │ ▼ Streaming     │
│   2 Snare ●          ││     ┌───────────────┐                │  ┌────────────┐ │
│   3-4 OH  S ●        ││     │ Compressor    │                │  │ 5.1 (6ch)  │ │
│   …                  ││     │ (per-ch link) │≡≡≡[8→6 map]≡≡≡▶● ≡≡          │ │
└──────────────────────┘│     └───────────────┘                │  └────────────┘ │
                                                               └─────────────────┘
```

Behavior:
- The `Drums 1-8` card is one **multichannel bus** (double-line cable with
  `8` badge). **Expand ▾** reveals individual channels/sub-groups (mono and
  stereo pairs) for direct patching without changing the grouping.
- A mismatch chip such as `[8→2 map]` opens the **channel matrix editor**:

```
          Out L   Out R
  1 Kick    ●       ●      ← click cell to toggle, drag for gain
  2 Snare   ●       ●
  3 OH L    ●
  4 OH R            ●
  …
  [Presets: Stereo fold-down | 5.1 | Passthrough | Clear]
```

- Layout-aware presets (stereo fold-down, 5.1, 7.1) pre-fill the matrix.
- Processors that support multichannel show a **link toggle** (apply the same
  settings to all channels vs. per-channel).
- Rail cards collapse long channel lists to meter strips so 32-channel
  devices remain legible.

### 4.4 Shared interaction details

| Action | Result |
|--------|--------|
| Drag from source port | Compatible ports glow; adaptable ports show dashed ring |
| Drop on adaptable port | Cable + adapter chip, auto-configured |
| Click cable | Inspector: layout, adapter, per-cable gain (on Σ ports), delete |
| Disable endpoint | Card greys out; downstream path mutes and shows `muted: source disabled` |
| Missing device | Card shows ⚠ state; wires preserved; Start is not blocked for other endpoints |
| Empty patch | Rails visible and live-metering; helper text "Drag a cable from a source to a destination" |

## 5. How this simplifies the user experience

1. **No boilerplate.** A new project already contains every source and
   destination. The first useful action is a single cable, instead of "add
   Input, add Output, place, wire both".
2. **One consistent mental model.** Everything that makes sound is a
   source on the left; everything that receives sound is a destination on the
   right; everything in between is a processor. Generators, files, capture and
   hardware are the same kind of thing.
3. **Physical reality is visible.** Device channels, meters and states are on
   the canvas, so the question "which mic is this?" no longer needs a trip to
   Audio Setup.
4. **Mono/stereo/multichannel stops being an error.** The one-global-width rule
   is replaced by per-cable layouts with visible, editable adapters. Users
   see and control conversions instead of discovering failures.
5. **Parallel outputs are trivial.** Monitoring, recording and streaming from
   the same signal are three cables, not three separate projects.
6. **Safer.** Endpoints cannot be deleted, so patches cannot be broken by
   removing Input/Output; missing devices degrade the path, not the project;
   summing is only where it is visible.
7. **Faster iteration.** Drop-on-cable insertion and rail-pinned endpoints
   keep the patch navigable at any zoom level.
8. **Output-only and file workflows become first-class** (generator → main
   output; file → recorder), consistent with
   `docs/plans/03-OutputOnlyProcessingPlan.md`.

## 6. Risks and open questions

| Risk / question | Mitigation / proposal |
|-----------------|-----------------------|
| Rails crowded with many devices | Collapse by kind, hide disabled endpoints, search/filter |
| Auto adapters hide too much | Always visible chips; project file records them; tooltips state the rule |
| Summing and gain staging clipping | Per-cable gain on Σ ports, destination meters with clip indicators, optional soft limiter on Main Output |
| Destination clock domains differ | Resample at endpoint boundary; document latency per endpoint in inspector |
| Legacy projects | Schema bump + in-memory migration (§2.5) with a tested round trip |
| Feedback loops | Out of scope; reject cycles with a highlighted cable |
| Plugin/script (VAL, VST3) channel capability | Declare layouts in the module contract; fallback to adapter chips |

## 7. Suggested rollout (non-binding)

1. Introduce `SourceNode`/`DestinationNode`/`ProcessorNode` roles and bus
   layouts in the engine behind the existing graph API; keep legacy
   `InputNode`/`OutputNode` as migration shims.
2. Add per-cable layouts, adapters and summing ports plus validation.
3. Add project schema v-next and migration.
4. Build the pinned rails and cable adaptation UX; then multichannel matrix
   editor.
5. Add Recorder/File Writer/Streaming/App Capture endpoints incrementally;
   each is independent once the endpoint contract exists.
6. Remove shims after one release.
