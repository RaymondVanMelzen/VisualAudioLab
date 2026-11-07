# Routing Model Redesign: Fixed Sources and Destinations

**Status:** the core fixed-endpoint and typed-bus architecture is implemented.
Hardware Input and Output are system-owned, stable project endpoints; exact
driver-named channel assignments and readiness states are shown in fixed canvas
lanes. The runtime supports multiple sources and destinations, fan-out,
independent bus widths, explicit Sum, and a visible mono-to-stereo cable
adapter. Each destination accepts one feed; mix through Sum rather than
implicit destination summing. Hardware outputs use disjoint channel groups
from one configured backend/device session. Legacy Input/Output nodes migrate
without changing processor or cable identities.

The shipped endpoint kinds are hardware input, the existing internal
generators and configured main hardware output. Application Capture, File
Player, Virtual Audio Device, Recorder/File Writer, Streaming, plugin hosting,
arbitrary unsynchronized devices and generalized resampling remain deferred.
The rest of this document describes the broader target product model; its
deferred kinds and capabilities are proposals, not shipped behavior. See the
[implemented plan and validation record](plans/08-HardwareEndpointLanesAndAudioSetupPlan.md).

## 1. Previous model (before fixed endpoint implementation)

The following is historical context, not a description of current runtime
behavior.

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
not deletable or movable as processor nodes**; processors are **user-owned**.
Cables run left → right only. The shipped hardware lanes may contain several
user-configured endpoint rows; a row's identity survives missing hardware and
its driver channel assignments are not serialized as machine-specific device
tokens in the project.

### 2.1 Node taxonomy

`InputNode` and `OutputNode` are retired and replaced by three roles that
share the existing node base (`AudioNode`) and a common port/bus contract.

| Node | Owner | Ports | Lifetime | Replaces |
|------|-------|-------|----------|----------|
| **SourceNode** | System, per available source | 0 inputs, ≥1 output *bus* | Exists while the source is configured/available; can be disabled but not deleted | `InputNode`, `GeneratorNode` as a product source |
| **ProcessorNode** | User | ≥1 input bus, ≥1 output bus | Created/deleted by the user | All existing DSP/script/plugin nodes |
| **DestinationNode** | System, per available endpoint | ≥1 input bus, 0 outputs | Exists while configured; can be disabled but not deleted | `OutputNode` |

Concrete kinds (the following taxonomy is the wider product target, not a
claim that every listed source or destination has shipped):

- **SourceNode kinds:** Hardware Input, Signal Generator, Application
  Capture, File Player, Virtual Audio Device.
- **DestinationNode kinds:** Main Output, Headphones, Recorder, File Writer,
  Streaming Output.
- **ProcessorNode kinds:** EQ, Compressor, Delay, Reverb, VST3
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
3. **Explicit adaptation:** mismatches never fail silently. The shipped
   adapter is a visible, removable **Mono → Stereo: Dual-mono** cable chip,
   persisted in the project. Pan, stereo-to-mono, multichannel matrices and
   other conversions in the wider target model remain unimplemented; those
   mismatches are rejected rather than guessed.
4. **Fan-out is free:** one output bus may feed many inputs.
5. **Fan-in is explicit:** every destination accepts a single cable. Multiple
   sources are combined only through the visible **Sum** processor. The
   proposed destination sum badges and per-cable gain/mute are not shipped.
6. **No cycles** (feedback requires an explicit Feedback/Delay-loop
   processor, a future feature).
7. A **processor with no path to any destination** is shown dimmed with a
   "not routed" hint; it is legal but inert (replaces the current hard
   "every node must feed Output" failure with a visible state).

### 2.4 Engine and validation changes

| Area | Shipped behavior | Wider target not yet shipped |
|------|-----------------|------------------------------|
| Endpoints | Stable hardware input/output rows; existing internal generators remain available | Application Capture, File Player, Virtual Audio Device, Recorder/File Writer, Streaming and plugins |
| Bus layouts | Independently declared 1-32 channel ports, compatibility checked per cable | Additional conversion adapters and editable channel matrices |
| Fan-in | Single feed per destination; explicit Sum processor for mixing | Per-cable controls and destination summing ports |
| Fan-out | One output can feed several compatible inputs | Additional routing conveniences |
| Hardware mapping | Exact saved channel names resolved to current device indices; sparse physical masks compacted for callbacks | Multiple unsynchronized devices and cross-device resampling |
| Output groups | Disjoint physical channel assignments within the configured session | Independent destination clock domains |
| Start | Explicit; no automatic device fallback or playback | Other real-time and asynchronous endpoint lifecycle policies |
| Persistence | Schema 1.1 endpoint feature; legacy Input/Output nodes migrate with stable IDs and cable references | Additional endpoint-specific portable state |

**Endpoint lifecycle** — the shipped hardware rows show readiness, disabled,
missing or error state in the fixed lanes. Rows and cables survive a missing
device; the user must resolve the binding, and no channel/device is silently
substituted. A configured audio session can still be unavailable as a whole
if its device is missing. Audio starts only on explicit Start.

**Real-time vs. non-real-time endpoints:** the shipped hardware input/output
and internal generators participate in the current audio graph. The additional
App Capture, Virtual Device, File Player, Recorder, File Writer and Streaming
examples above are target-design considerations only. No file writing,
external endpoint resampling or asynchronous sink behavior is claimed as
implemented.

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
│  │ ▮▮▮▮▮▮▮▮         ││     │  (Compressor) │              ╚▶● Σ  stereo  │ │
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

## 7. Implementation record

The fixed endpoint rows, per-port runtime graph, explicit Sum, visible
mono-to-stereo cable adapter, sparse device channel mapping, schema-1.1
persistence and legacy node migration have shipped together. Legacy `Input` and
`Output` type recognition remains in the reader as a compatibility path; new
projects cannot add these hardware nodes through the Module Browser.

The focused implementation plan, scope boundaries and validation evidence are
recorded in [Hardware Endpoint Lanes and Audio Setup Plan](plans/08-HardwareEndpointLanesAndAudioSetupPlan.md).
