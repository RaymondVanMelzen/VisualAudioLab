# Visual Audio Lab: Visualization Architecture

## Status, scope and governing context

This is a **proposed architecture**, not an implementation, benchmark or delivery commitment. It defines future audio and control-signal observation without introducing audio processing, DSP modules, a visualization runtime or persistence into the present application. The structural node-editor work is independent of this design.

Use the C++20, JUCE and CMake direction in [TechnicalArchitecture.md](TechnicalArchitecture.md), host-neutral processing in [DspArchitecture.md](DspArchitecture.md), and Windows/ASIO and macOS/CoreAudio boundaries in [AudioPlatformStrategy.md](AudioPlatformStrategy.md). Preserve the offline-first, open-source, no-subscription, no-cloud and no-AI principles in [ProductVision.md](ProductVision.md). Visualization is never required to produce audio; future exported processors must work without an editor or observation subscribers.

This document refines the observation responsibilities in [FunctionalArchitecture.md](FunctionalArchitecture.md) and the inspector/workbench concepts in [UxBlueprint.md](UxBlueprint.md). It does not expand [UiPrototypePlan.md](UiPrototypePlan.md): that separate usability study remains UI-only, with unavailable measurements and explicitly labeled illustrations.

The read-only **UX Review and Workspace Redesign**, consulted on 2026-10-31 and reviewing shell commit `dd74079`, informed the docking, selection, workspace recovery and persistent Signal Inspector proposals. It is not assumed to be available in this checkout. Its Signal Info tab is metadata-only; the current request additionally requires live Property Inspector visualizations. The explicit refinement here is an optional **shared signal-preview region** within the Property Inspector, not a second analyzer/dashboard. The persistent Signal Inspector remains the canonical detailed measurement tool. This relationship needs UX validation, not a silent change to the review or earlier documents.

### Goals and invariants

- Make a node, port or connection easy to inspect without rewiring the patch or knowing DSP terminology first.
- Offer three scales: a glance on the canvas, selection-oriented inspection, and stable dedicated monitors for comparison or another display.
- Keep capture work bounded and admit it against the audio budget; move analysis and rendering off the callback.
- Scale by sharing work, limiting resources and reducing visual fidelity before affecting audio deadlines or editing responsiveness.
- Expose units, measurement windows, freshness, gaps and provenance. Unavailable, stopped and stale are not zero.
- Maintain accessible numeric/text alternatives and recoverable multi-monitor layouts. Nothing depends on network access.

Monitoring cannot have literally zero CPU, memory or cache impact. The aim is **bounded, qualified overhead that does not cause observed audio deadline regressions on declared reference workloads**. Lock-free communication alone does not establish realtime safety. Every numeric budget below is a proposed starting point to measure and revise, not existing performance.

## 1. Visualization levels

All levels subscribe to the same observation service. A view's dimensions and visibility request a quality profile, not another audio tap. Each surface shows its target, channel mapping, Live/Frozen/Stale state and a route to more detail.

| Level | Purpose and proposed size constraints | Refresh and rendering strategy | Interaction and cost controls |
|---|---|---|---|
| **1: Embedded Preview** | A miniature oscilloscope, spectrum or meter inside an analysis-node card. Candidate content area 96-240 by 48-120 logical pixels; below a readable threshold replace plots with a role/status glyph and numeric summary, not smaller text. | Start at 10 updates/s, hard candidate cap 15/s. Cache decimated min/max waveform columns, frequency-bin aggregates or meter values; repaint only dirty visible rectangles. No per-node timer or FFT. | Click/focus reveals the same target in the inspector; Open Monitor pins it. Hidden groups, offscreen nodes and inactive canvas tabs cease graphical refresh. Never allocate per sample or paint the full graph for a meter change. |
| **2: Inspector View** | A larger shared preview region beneath Property Inspector fields for a single selected target. Candidate minimum plot 280 by 160 logical pixels; in a narrow inspector show values and an Open Signal Inspector action. The detailed Signal Inspector can occupy a bottom or side dock. | Start at 20 updates/s, cap 30/s for the focused visible inspector. Bind immediately to target metadata; show Waiting while a new matching observation is acquired. Reuse prepared plot data and scale geometry with the panel. | Channel chooser, waveform/spectrum/statistics tabs, cursors, units, Pin, Locate and Freeze. No forced graph selection when the user changes focus. Zooming an axis changes presentation first; a new FFT/window request is explicit and admitted separately. |
| **3: Dedicated Monitor Window** | A pinned visualization panel usable in a dock split, tab group, floating native owned window or secondary application monitor frame. Candidate minimum 320 by 200 logical pixels; meter-only layouts may be narrower if labels remain readable. | Start at 30 updates/s for visible foreground monitors; 60/s is an opt-in smooth-display profile requiring qualification, not an analysis-rate or audio-rate requirement. Hidden tabs and minimized windows stop painting. | Independent target pin, channel/axis/analysis settings, Freeze, compare and Dock Back. Closing releases the view subscription; it does not delete the node, stop audio or close the project. Resource denial is visible. |

Logical pixels describe layout, not physical framebuffer size. Text scaling and translated labels can increase minimum sizes. A 60 Hz display does not require 60 FFTs/s. Temporal meter smoothing can animate from timestamped results without inventing new measurements; show actual observation age.

### Rendering model

Use JUCE components and separated C++ presentation state on the message/UI thread. Workers prepare immutable numeric summaries and plot geometry, never manipulate components. Rendering consumes a bounded snapshot lease, then releases it. Cache grid/axis text independently of dynamic traces; invalidate on size, theme, scale or data changes. A shared UI frame coordinator schedules eligible surfaces rather than starting one timer per widget.

Baseline rendering should work without requiring GPU-specific code. An optional qualified GPU path may upload bounded geometry/textures off the audio path; uploads, device loss, texture re-creation and driver stalls cannot reach the callback. A CPU renderer remains a design fallback for graphics-device failure, with a visible reduced-quality state. GPU acceleration is an unresolved optimization, not a selected dependency.

At canvas zoom-out, semantic simplification hides traces before node names, faults and connection identities. Never let a scope's animation obscure editable controls or turn canvas navigation into a measurement operation. Reduced Motion uses slower/static traces and numeric readings; Freeze captures a display snapshot, not DSP state.

## 2. Workspace concept

Built-in workspaces are immutable layout templates; user changes produce a Modified layout and may be saved under a local name. A workspace changes panel placement and visibility, not patch topology, audio state, parameter values or sampling configuration. Switching never starts/restarts audio. Analyzer budget admission still applies when a layout exposes many plots.

| Workspace | Visible panels and docking arrangement | Primary workflow |
|---|---|---|
| **Beginner Workspace** | Large central Patch Canvas/Structure List; left Module Browser with Analysis category and optional Explorer tab; right Property Inspector with explanatory units and shared preview when supported. Signal Inspector is initially collapsed/auto-hidden; compact audio-status strip remains visible. No floating windows by default. | Add a simple patch, select an endpoint, read the preview and open the detailed inspector through a labeled action. Explain unavailable data before introducing FFT controls. |
| **Patch Design Workspace** | Canvas dominates centre; browser/explorer tab group left; Property Inspector right; Signal Inspector auto-hidden bottom. Dedicated monitors reopen only when the user retained them, never from an automatic template surprise. | Build and reorganize a patch while glancing at embedded previews. Pin an endpoint for comparison without changing the editing selection. |
| **DSP Debugging Workspace** | Canvas/Structure List centre; Explorer/Problems left; Property Inspector right; persistent Signal Inspector bottom with waveform/statistics. Optional pinned A/B monitors in a right split or second display. | Locate a suspect connection, compare input/output timing and non-finite/clipping evidence, Freeze a labeled snapshot, then return to the relevant node. Observation never activates a pruned branch. |
| **Audio Analysis Workspace** | Monitor document/group occupies most of the centre or a secondary frame; waveform and spectrum in adjacent splits/tabs; persistent Signal Inspector at the bottom for numeric provenance. Canvas remains reachable as a document tab; browser auto-hidden. | Select explicit channels, pin targets, compare windows and cursor readings, then inspect metadata/latency. Opening this layout does not create unsolicited analyzer nodes. |
| **Performance Monitoring Workspace** | Canvas remains usable; a compact Performance/Observation Diagnostics panel and Problems dock bottom/right; meter summaries visible, heavy histories/spectrograms paused unless requested. Browser/explorer tabbed left; properties auto-hidden. | Inspect callback headroom separately from process CPU, analysis load, capture drops and UI frame time. Identify expensive subscriptions and reduce their quality, without silently changing device buffers. |

Audio Settings stays an on-demand non-modal document, not a permanent bottom visualization. No workspace opens script consoles, video tools or plugin editors before those capabilities exist. A preset can show empty placeholders with actionable explanations, not fabricated measurements.

```text
------------------- Patch Design / main display ---------------------+
| Workspace selector | Add / Search | History | Audio lifecycle       |
| Browser/Explorer | Patch Canvas / Structure List | Property Inspector|
|                  | embedded previews            | fields + preview |
| Auto-hide: Signal Inspector / Problems                              |
| Effective audio state | observation freshness | budget status       |
+--------------------------------------------------------------------+
             Open Monitor / Move to Monitor (explicit)
                              |
+------------------ Audio Analysis / second display ------------------+
| Pinned scope: Node A.Out / L | Pinned spectrum: Node B.Out / L       |
| window/trigger/time context | FFT/window/units/context               |
| Shared numeric Signal Inspector / observation diagnostics           |
+--------------------------------------------------------------------+
```

### Docking, navigation and recovery

Use one workspace docking model for inspectors and monitors: tab stacks, readable-minimum splits, floating windows and explicit auto-hide. Tab/header drags show transient named drop previews; Escape cancels. Context menus and Window > Arrange Panel provide Dock, Float, Move to Group, Move to Monitor, Resize and Dock Back without dragging. Do not copy permanent Dock destination selectors into every analyzer.

Keyboard region navigation includes owned floating windows; moving a monitor preserves focus, target, drafts and subscriptions. Focus and selection are distinct. Auto-hide overlays remain open while they contain focus or an active interaction. Hidden, auto-hidden and docked-inactive are distinguishable states. Window > Monitors lists target, placement and freshness; revealing a monitor reuses it rather than silently spawning duplicates.

**Proposed persistence:** store layout schema/version, workspace name, panel instance identities, dock tree, split proportions, active tabs, floating logical bounds, display association, quality preferences and pin descriptors as machine-local application preferences. This is future full-layout support, not a claim about the existing settings framework. Keep project identity plus stable endpoint references separate from layout geometry; do not save samples, FFT history, live buffer pointers, plugin handles or device indices in layouts. Missing projects/targets restore as unresolved pins, never another matching name. Layout operations do not dirty a patch or enter patch undo history; provide layout-only Undo/Reset.

On mixed-DPI displays, recalculate logical bounds, readable minimums, line widths and cached pixel geometry at the destination scale. Cap physical render-target dimensions before allocation; 4K scaling must not multiply every plot buffer without admission. Remember a display identifier only as a hint, with normalized relative placement and a fallback work area. Respect taskbars/menu bars and native macOS window spaces.

On display removal, sleep/wake or remote-session reconnect, move windows to a reachable active work area with title bars visible; dock windows that no longer fit. Preserve target/freeze state and inform the user. Window > Bring All Panels Here and Reset Current Layout provide explicit recovery. Invalid/corrupt or incompatible saved layouts fall back to a safe template with an explanation, retaining rejected data for recovery. No placement recovery reconfigures audio.

## 3. Signal Inspector and shared inspection model

### One selection, explicit targets, shared data

Canvas, Explorer and Structure List share a document selection identity. Selection is not keyboard focus. The observation coordinator maps selection to a **target descriptor** containing project/session identity, stable node/port/connection identity, signal rate/type, channel mapping and requested measurement profile. A node with several eligible ports shows a named endpoint chooser; an unambiguous remembered choice is disclosed. A connection resolves to its source at that exact routing boundary; downstream transformations are not silently included. A group or multi-selection requires an explicit endpoint choice, not implicit summation.

The Property Inspector follows selection by default and retains editable fields and metadata. Its live region appears by default for a supported single target, offering a concise larger visualization of that endpoint; users may collapse it. Ambiguous selections show the endpoint chooser, and unavailable processing shows an explanation rather than a simulated trace. Inspect Live Signal opens/focuses the persistent Signal Inspector with the same target and compatible display settings. These are **two hosts of a shared inspection presentation model**, not two analysis owners. Keep advanced controls and the canonical numeric/statistics dashboard in Signal Inspector; the property preview exposes only essentials and a route to that dashboard.

An unpinned Signal Inspector follows the same selection; pinning it or a dedicated monitor freezes its target reference, not the global selection. Headers disclose differences such as `Properties: Filter 3` and `Pinned signal: Oscillator 1.Out / L`. Embedded previews, the Property Inspector, Signal Inspector and pinned monitors share one capture subscription per source/channel set and compatible analysis products. Multiple visible hosts may paint the same immutable result; they do not each run an FFT. When Property Inspector and Signal Inspector have the same target, show synchronized preview versus detailed controls, not two competing full dashboards.

Changing the Property Inspector selection must not retarget a pinned monitor. A deleted target becomes Target removed; reconnection, undo or graph replacement resolves only the same stable identity and active epoch after validation. Frozen results remain labeled historical and cannot be mistaken for live replacement data.

```text
Select node / port / connection
             |
Document Selection ----> Property Inspector (fields + shared preview)
             |
       Target Resolver ------> unpinned Signal Inspector
             |                        |
     Subscription Registry <---- pinned monitor descriptors
             |
  Admission / prepared capture plan
             |
 Active signal -> bounded capture -> shared analysis products
                                        |
                              immutable result cache
                        /               |               \
                 embedded         inspector hosts     monitors
```

### Measurements and presentation semantics

| Information | Required meaning and provenance |
|---|---|
| Peak | Per-channel maximum absolute sample over the named contiguous window, in linear full-scale units and sample-peak dBFS referenced to amplitude 1.0. Silence may display negative infinity explicitly. State hold/decay interval and Reset scope; do not call this true peak. |
| RMS | Square-root mean-square over a declared contiguous duration/sample count, per channel. Reference amplitude 1.0 for 0 dBFS RMS; a full-scale sine is approximately -3.01 dBFS under this convention. Label the convention; RMS is not LUFS. |
| Frequency data | FFT size, window, sample rate, bin spacing, channel, amplitude/power normalization, averaging and capture time accompany the plot. A dominant-bin/band summary is not a guaranteed fundamental-frequency estimate. Unsupported/insufficient samples remain unavailable. |
| Clipping | Count samples at or beyond the declared full-scale threshold and maintain a labeled hold indicator over observed coverage. Internal floating-point values may exceed full scale without proving analog clipping. Endpoint context and gaps matter. Separate non-finite values from clipping; true-peak/codec risk requires a qualified detector. |
| Sample statistics | Valid contiguous sample count, signed minimum/maximum, mean/DC, absolute peak and non-finite count for each channel/window. Show duration and coverage. Non-finite samples invalidate affected numeric products rather than contaminating plots or silently becoming zero. |
| Latency | Report known node/graph algorithmic delay in frames and milliseconds, separately from any declared compensation and backend/host-reported output latency. The current DSP design does not assume automatic compensation. Unknown is not zero; buffer duration is not measured round-trip latency. |
| Observation delay | Capture-to-analysis age, analysis-to-present age and total age in the stream timeline where available. This visual lag is not audible latency. Freeze, backlog and missing timestamps remain explicit. |

Use states No target, Unsupported, Not processing/disconnected, Audio stopped, Preparing, Waiting for first complete window, Live, Degraded/gaps, Stale, Frozen and Target removed. Report the active graph revision/stream epoch, channels, measurement window and age alongside values. A newly selected target never briefly displays the previous target's numbers under its name. Mixed-channel views require explicit overlay, side-by-side or aggregate semantics.

Pin, Freeze, axis zoom and Reset Hold are observation actions, not DSP commands or patch-history edits. Freeze retains a bounded snapshot and stops that view's demand for fresh data; other subscribers may continue. Reset Hold establishes a new display/measurement accumulation boundary and acknowledges it in the UI. It never resets an oscillator, filter, graph clock or audio device. Numeric copy or accessible reading requests use the same snapshot/version as the graph.

## 4. Visualization pipeline and ownership

```text
CONTROL / PREPARATION (off callback)
Selection + visibility + profiles -> shared registry -> budget admission
       -> preallocated capture plans / worker states -> activation request

REALTIME
Audio Thread -> prepared read-only tap -> Capture Buffer
              bounded copies only         | one producer per queue
                                          | release/acquire publication
WORKERS                                   v
bounded fair dispatcher -> Analysis Engine -> fixed result-slot pools
                           shared products            |
UI / RENDERING                                        v
UI frame coordinator -> Rendering Engine -> JUCE components / monitor windows
                         latest ready data

Retirement: detach at safe boundary -> callback acknowledgement/quiescence
            -> drain/release consumers -> reclaim off callback
```

The logical rendering engine includes worker-prepared numeric geometry and message-thread painting; it is not rendering in the audio callback. Device/host buffers remain borrowed only during processing. Captures contain owned samples plus value metadata, never pointers into reusable graph buffers.

### Capture admission and transport

1. The control/preparation owner validates observable endpoints, channel limits, requested profiles and resource budgets. Only outputs already scheduled by the active processing plan are eligible. Inspection does not turn a disconnected/pruned branch into a processing root. An explicit analysis sink, if later admitted, has declared cost and cannot change upstream state-advancement implicitly.
2. Allocate, initialize and touch capture pages, queues, analysis scratch and output pools before activation. Add/remove taps through the prepared-plan handoff; UI selection never edits callback-owned registries. A new demand shows Preparing until activated. Sharing overlapping channel sets must obey the channel budget, not force every channel to be copied.
3. Use a fixed-slot single-producer/single-consumer ring for each admitted capture stream. The callback is the sole producer; an assigned ingestion worker is the sole consumer. Future parallel render producers need separate queues or an explicitly qualified merge boundary, not accidental multi-producer use of SPSC.
4. Producer fills an exclusively owned free slot, then publishes its sequence with release semantics. Consumer acquires before reading and returns the slot only after bounded ingestion/copy into its worker-owned analysis window. Neither side writes the other's owned slot. Cache-line separation and verified lock-free atomic index operations on supported architectures are prerequisites; no spinning/CAS retry loop, mutex or notification syscall in the callback.
5. Slot metadata includes source identity, graph revision, stream epoch, first sample index, valid frames, channel layout/rate, sample rate, sequence and drop/discontinuity flags. Invalid slots are never advertised as complete. Variable blocks split only into a precomputed bounded maximum slot count. Oversized blocks beyond the admitted bound skip observation and flag it; never truncate audio or allocate a larger buffer.
6. If full, **drop the incoming observation**, increment a producer-owned saturating counter and carry the counter snapshot/discontinuity marker into the next delivered slot. Other contexts read only published snapshots or separately qualified lock-free atomic counters, never race on producer-local fields. Never overwrite an unread/being-read slot or advance a consumer cursor from the producer. Consumer may skip owned queued slots to catch up, marking that gap before restarting analysis. It cannot turn separated data into a contiguous window.
7. Audio processing proceeds regardless of observation availability. Fault/stop acknowledgements use the independent engine lifecycle path, not this lossy queue. Visualization errors disable/report the affected observation off-callback; they do not masquerade as DSP faults or block Stop.

The normal callback capture path performs only admitted metadata publication and bounded sample copies. It performs no FFT, rendering, min/max scan, RMS accumulation, string formatting, dynamic allocation/deallocation, refcount-driven destruction, logging, UI event posting or worker waiting. Even inexpensive statistics belong off-callback. Account for extra copies/cache traffic and any capture representation conversion in the audio budget; default to the engine's prepared representation.

### Shared analysis and result publication

Key analysis products by source/epoch, channel mapping, algorithm/version, sample rate, window/FFT size, hop, calibration/normalization and smoothing policy. View size, color and placement are not analysis keys. A waveform pyramid/min-max envelope can supply multiple resolutions; a single calibrated spectrum can feed preview and detailed plots. Different FFT sizes or channels need distinct bounded products unless a proven compatible derivation exists. A slow preview never forces high-resolution analysis, and one expensive monitor cannot silently raise every subscriber's cost.

Ingestion copies into preallocated worker-owned circular windows and releases capture slots promptly. A bounded worker job reads an immutable window snapshot; it never retains an audio slot while waiting for UI or another worker. Batch peak/RMS/statistics scans where compatible. A fixed-size worker pool with a bounded fair queue owns analysis, below audio scheduling priority; do not launch a worker per node/window or use all logical processors by default. Initial analysis concurrency is one worker, optionally two after qualification and headroom assessment. No worker takes locks shared with processing.

Publish results through fixed pools with explicit Free, Writing, Ready and Reading ownership. A worker writes only its reserved slot; the UI or dispatcher claims a complete immutable slot before access and returns it after use. Fan-out occurs off the callback via bounded leases/copies; the last lease returns memory off-callback. If the pool is exhausted, skip publication and report lost refreshes; never overwrite a UI-read snapshot or let UI retention back-pressure audio. A Frozen view consumes its own admitted bounded copy, not a capture/result slot forever.

At each UI tick consume the newest matching ready result, retiring superseded ready results safely. Tag every result with source/epoch/window/quality/coverage. Reject stale generations on both analysis completion and UI consumption. UI invalidation is coalesced per frame, not one message per audio block. Large raster/texture work is chunked to avoid a single monitor monopolizing a message-thread frame.

Use stream sample indices for signal-window alignment and the engine's already available qualified clock correlation for wall-time freshness. Keep acquisition time, analysis completion and presentation time distinct. Do not introduce a clock syscall into every tap, assume clocks on different devices/hosts are identical, or claim phase alignment between unsynchronized streams. If clock correlation is unavailable, show sample-domain age and unknown wall-time latency instead of subtracting unrelated timestamps.

### Discontinuities, lifecycle and resource reclamation

Dropped capture, audio stop, device/sample-rate/channel change, transport discontinuity or incompatible graph activation starts a new analysis continuity segment. Clear partial FFT/RMS/correlation windows and mark gaps in time displays. Preserve a historical snapshot only with its original epoch and Stale/Frozen label. A drop in **rendered frames** need not invalidate an otherwise complete worker measurement; distinguish capture loss, analysis loss and presentation loss.

Graph swaps prepare a new capture generation off-callback. The old plan remains alive until the callback has acknowledged replacement and all old worker/result leases are retired. Cancel stale queued jobs; already running jobs may finish but cannot publish under the new epoch. No callback deletes a tap, decrements an owning shared pointer or waits for retirement. Device stop obtains callback quiescence through the control owner before draining pools. Project close cancels demands, detaches capture, joins/drains worker ownership and then destroys services. UI window close releases only that subscriber; analysis ends after the final compatible demand disappears.

Default behavior does not carry peak holds, trigger acquisition or smoothing across an incompatible epoch. If later supporting continuous multi-segment history, retain explicit segment boundaries. Future no-editor plugin instances create no observation capture by default; host offline rendering has its own sample clock and may outrun wall time, so throttle/drop presentation without slowing the host.

## 5. Analyzer module library

These are **analyzer-library Version 1, Version 1.5 and Version 2 groups**, not product Release 0.1, 0.5, 1.0 or 2.0 from [PRD.md](PRD.md). They specify a requested visualization roadmap; product release mapping, delivery dates and supported limits remain undecided. Advanced displays do not become MVP commitments through this document.

Analysis modules are observational sinks with stable signal inputs and optional preview surfaces. Hiding a surface removes observation demand, not saved graph topology. If a future module has a transparent audio-through port, its pass-through semantics and processing cost must be explicit; analysis is not silently inserted in a signal path. General inspection taps and analyzer nodes use the same service and admission rules.

| Library group / module | Analysis semantics and controls | Result, limitations and gap behavior |
|---|---|---|
| **Version 1: Oscilloscope** | Time-domain samples per selected channel; free-run or declared rising/falling threshold trigger, pre/post-trigger duration and time/amplitude axes. Initial candidate acquisition up to 4096 samples; detailed longer windows require admission. Min/max envelopes preserve visible extrema, not just every nth sample. | Timestamped waveform and trigger state; sample cursors and numeric range. No interpolated detail presented as additional measured samples. On loss discard incomplete trigger acquisition and show Waiting/gap. Freeze preserves a labeled complete capture. |
| **Version 1: Spectrum Analyzer** | Real FFT over contiguous samples; initial candidate 2048 points with Hann window, detailed profiles 1024-8192 after admission. Label bin spacing, window-dependent resolution, amplitude normalization/coherent gain and linear/log frequency axis. Named averaging in time; candidate 50% overlap, not an implicit universal rule. | Per-channel calibrated spectrum and dominant-bin summary; distinguish amplitude dBFS from power spectral density per Hz. Zero-padding changes drawing/bin interpolation, not physical resolving power. Gaps reset FFT/averaging state. No unsupported pitch certainty. |
| **Version 1: Peak Meter** | Per-channel sample peak, declared acquisition interval, hold interval and display release ballistics; candidate 50 ms result windows. Sample threshold events and peak occur in workers, independent of how fast the bar is painted. | Linear and dBFS values, hold marker, over-range count with observed coverage. Reset affects hold only. Does not claim inter-sample true peak or continuous clipping coverage across lost captures. |
| **Version 1: RMS Meter** | Per-channel RMS using declared duration; initial candidate 300 ms sliding window, with explicit window/hop and amplitude-1 reference. Compute from contiguous observed samples, not averages of dB values. | RMS dBFS and linear value, window duration and age; silence distinct from missing data. Gap invalidates the rolling window until refilled. Not a loudness meter or an ear-safety statement. |
| **Version 1.5: Correlation Meter** | Explicit synchronized channel pair, candidate 300 ms window; normalized zero-lag cross-correlation with named DC-removal policy. Initial proposal removes each channel's window mean. | Value from -1 to +1 plus valid/low-energy status and window. Zero-energy denominator is undefined, not zero correlation. Not a frequency-dependent phase plot. Gaps/reset invalidate both sides until a complete aligned window exists. |
| **Version 1.5: Spectrogram** | Time sequence of calibrated FFT power/intensity columns; state FFT/window/hop, frequency scale, color scale and finite history duration. Reuse compatible spectrum products. | Time/frequency raster, numeric cursor and accessible band summary. Missing columns are visibly marked, not interpolated into apparently continuous coverage; oldest columns evict at cap. Color is not the sole cue. |
| **Version 1.5: Loudness Meter** | Proposed qualified BS.1770-family K-weighting/channel weighting and declared gating/profile; distinguish momentary, short-term, integrated LUFS and any separately qualified true-peak measurement. Exact standard revision, channel layouts and conformance vectors must be selected before delivery. | Loudness requires continuous admitted ingestion and a bounded-state integrator, not retained whole-session samples. Pause/reset/session boundaries explicit. After a capture/analysis gap mark integrated result incomplete/invalid and require restart or a clearly labeled new segment; never call sparse capture a compliant integrated reading. No conformance claim before tests. |
| **Version 2: Signal History** | Explicitly armed bounded in-memory rolling history of samples or declared summaries, with duration/channel/resolution profile and trigger/bookmark metadata. No automatic disk recording. | Timeline with epoch/gap markers, finite retention/eviction and memory indicator. Raw history and summary history are not interchangeable. Longer retention needs a separately approved recording/storage design. |
| **Version 2: Waterfall Display** | Perspective/stacked presentation of a finite series of spectrum frames; share FFT/spectrogram products, cap slice count/geometry and decimate old slices for viewing. | Frequency/time/amplitude axes and a 2D accessible alternative; reduced-motion mode. No new sample resolution or missing-frame reconstruction; GPU use is optional and qualified. |
| **Version 2: Debug Graphs** | Bounded trends for control parameters, graph revisions, known node latency, queue depth, capture drops, worker utilization and engine timing where instrumentation is separately qualified. Domain/rate and clock mapping explicit. | Separate render callback timing/headroom from process CPU and visual load. No normal logging/timer syscalls added to the callback solely for a graph; unknown instrumentation coverage remains unknown. Typed labels and event discontinuities, not invented cross-clock alignment. |

Continuous metrics such as loudness can be denied when guaranteed capture coverage cannot be admitted; degrading them into sparse sampling is not a valid quality tier. Analysis failure leaves the audio path intact and produces an unavailable/invalid measurement. Observation of non-finite data does not replace the engine's own independent fault handling.

## 6. Performance management and proposed budgets

### Initial qualification profile

Use the audio strategy's 48 kHz stereo, 128/256-frame reference configurations where supported, plus higher rates, variable blocks and other admitted channel layouts. The following limits are intentionally conservative **design hypotheses**, not throughput guarantees or user-facing permanent limits.

| Resource / metric | Initial candidate budget and interpretation |
|---|---|
| Callback capture cost | Aim for added 99th-percentile capture cost at or below 1% of the effective callback period, with bounded worst-case copies and no observed monitoring-induced misses in qualification. Include monitoring in the audio strategy's overall headroom target; this is not extra permission to exceed it. Reject/reduce capture if unsupported. |
| Capture admission | Start with at most 16 mono-equivalent captured channels across all active unique taps and an explicit aggregate bytes-per-second ceiling. At 48 kHz float32, 16 channels are about 3.1 MB/s of payload before metadata/cache effects; this arithmetic is not a speed claim. Higher rates, precision and channels consume proportionately more budget. |
| Ring capacity | Candidate four slots of at most 2048 frames for each admitted channel set, with metadata and fixed segmentation bounds. Variable/oversized block profiles must be qualified; a ring is a bounded jitter cushion, not permission to display old backlog indefinitely. |
| Total observation memory | Candidate 64 MiB per application process, including all project capture rings, worker windows/scratch, output/geometry pools, history, frozen copies and CPU/GPU render buffers. GPU allocations are accounted explicitly. Refuse an allocation before activation when the total cap cannot be met. |
| Worker scheduling | Start with one analysis worker; cap a dispatched job slice at a candidate 2 ms and instrument actual tails. Limit each scheduling round's jobs and CPU allowance, reserve graph preparation/control responsiveness, and prevent queues growing with node count. A library FFT must itself fit the selected profile; a time limit cannot preempt arbitrary FFT code safely. |
| UI frame work | Candidate 4 ms total visualization work per 30 Hz UI frame across eligible surfaces, subordinate to input/layout. Budget measured physical-pixel geometry/raster work, not only component count. Defer lower-priority views before overrunning the frame budget. |
| Visual freshness | Under a qualified normal load, aim for a newly completed 50 ms meter window to appear within a further 100 ms. Longer FFT/RMS windows add their own acquisition time. Display measured age; candidate Stale threshold is the greater of 500 ms or three expected publication intervals after a result is due. |
| Stress fixture | Exercise 100 visible embedded previews and 8 detailed monitor surfaces, including shared versus unique sources. This is a demand/overload test, not a promise that every unique high-resolution analyzer runs simultaneously. Budget refusals and reduced-quality states are expected to remain usable. |

Pool sizes are computed off-callback from channel count, sample representation, maximum frames, window/hop and result count. For example, four 2048-frame float32 slots for 16 total channels use 512 KiB of sample payload; metadata, padding and worker/UI copies are additional. Duration-based RMS/loudness state and future histories require separate accounting. No pool grows on capture overflow. A settings change that cannot fit leaves the last accepted profile intact with an actionable explanation.

### Demand, priority and adaptive quality

The observation coordinator aggregates subscribers by compatible product. Priority order is audio deadlines and lifecycle first, editing/input next, then focused inspector/explicit continuous measurements, visible pinned monitors, visible previews, and background history. Priority never grants unbounded work. Age/fairness prevents permanent starvation; denied work reports its reason rather than looking Live.

Apply degradation in a declared order: reduce paints, simplify geometry/bin aggregation, lower presentation rate, lower analysis publication rate for eligible products, then offer a lower-cost admitted capture/profile or suspend the least-important demand. Do not silently change a measurement's FFT/window, averaging, channel mapping, calibration or continuous-coverage promise. Mark any quality tier visibly; users explicitly accept semantic changes. Use hysteresis and a recovery cooldown to avoid oscillation; settle exact thresholds through profiling.

Offscreen embedded previews, collapsed groups, inactive tabs, hidden/occluded/minimized monitors and closed inspectors stop graphical work where reliable visibility is available. If no visible or explicitly armed demand remains, detach the unique capture/product at a safe boundary. Pinned alone means a stable target, not background measurement. Background accumulation requires an explicit Keep Measuring/Arm History choice, budget cost and visible status. Shared products continue only for remaining subscribers. Reopening acquires fresh data; it does not pretend the hidden interval was observed.

Frozen surfaces have no fresh-data demand. Background windows may run at a lower repaint tier, but continuous admitted analysis stays at its declared coverage or becomes explicitly invalid. No automatic frame limiter raises audio buffer size, changes driver, resumes audio, mutes modules or rewires processing.

### Diagnostics and overload behavior

Expose active unique taps/products versus view count, admitted channels/bytes, memory pool use, callback capture overhead where qualified, queue occupancy/high-water marks, capture/analysis/presentation drop counts, result age, worker time and visualization frame time. Separate process CPU from render callback headroom. Collect counters in bounded ways, read/aggregate off-callback and rate-limit diagnostic presentation; no per-drop log flood.

The Performance Monitoring workspace provides Locate Consumer, Reduce Visual Quality, Pause Background Observations and Reset Display Counters. Show which targets lost coverage. An engine fault or Stop state bypasses ordinary lossy visual results and remains visible even if the UI skips every trace. Under memory pressure reject new demand and release offscreen caches off-callback; never wait for the UI to free a buffer from audio.

## 7. Future expansion without a universal realtime widget API

| Extension | Architectural preparation and boundary |
|---|---|
| Control-signal visualization | Typed control-rate packets carry logical frame indices, control interval, units and interpolation/hold semantics. Draw steps or the declared reconstruction; do not imply audio-rate measurements from sparse control samples. Share selection and result publication. |
| MIDI visualization | Separate bounded timestamped event capture with defined overflow counters and note/state resynchronization after gaps. Piano roll/event list/velocity display are render consumers, not permission to send MIDI or alter clocking. MIDI support remains a future product decision. |
| Video modules | Separate frame transport, clock-domain mapping, bandwidth/memory budgets and qualified texture ownership. Do not copy video frames through audio capture rings. AV synchronization and video processing require their own architecture before delivery. |
| Plugin visualizers | Host-owned output taps can use the normal pipeline, but third-party native editor UI remains a separate lifecycle/security/performance boundary. Do not call arbitrary plugin visualizer callbacks from audio or claim that a native editor obeys these budgets. Future plugin export remains editor-independent. |
| Custom visual widgets | A versioned descriptor names typed inputs, units, result schema, required products, minimum readable size, accessibility summary and declared resource demand. Approved widgets consume immutable results on allowed contexts, never own device buffers or get arbitrary realtime callbacks. |

Start with built-in trusted analyzers. Third-party native extensions can violate resource and process safety; a descriptor is not a sandbox. Admission, trust/isolation, cancellation and extension packaging/licensing need separate decisions before third-party execution. VAL Script's existing prohibition on networking, files, threads and dynamic allocation is not relaxed; it is not implicitly a visualization scripting engine. No extension requires a cloud renderer or AI service.

## 8. Architecture decision records

### ADR-VIS-01: Keep all three visualization levels

**Decision:** Embedded previews, selection-oriented inspector views and pinned monitor panels are complementary hosts of shared analysis products.

**Why:** Previews preserve spatial patch context and allow fast scanning; inspectors supply readable controls and explanations without window management; monitors keep a comparison stable while selection changes and support a second display. One inspector alone loses comparisons; monitors alone overwhelm beginners; previews alone are too small for quantitative analysis.

**Tradeoffs:** Three hosts increase rendering/accessibility and lifecycle complexity. Share target models, result schemas and rendering primitives; bound the number/quality of visible views. Reject the alternative of an independent analyzer per surface because it duplicates capture/FFT work and permits inconsistent readings.

### ADR-VIS-02: One inspection service, not competing dashboards

**Decision:** A shared model drives the Property Inspector's concise live preview and the persistent Signal Inspector's detailed measurements; monitors add explicit pins. Signal Info remains metadata plus observation context and the route to detailed inspection.

**Why:** Satisfies the requested live Property Inspector without discarding the UX review's clear distinction between editing and measurement. Selection, target pinning, focus and historical Freeze have separate semantics.

**Tradeoffs:** Two visible hosts can look redundant; synchronized preview/detail hierarchy, clear headers and usability tests are required. If redundancy persists, make the property preview collapsible rather than removing the required inspector level or duplicating analysis. This is an explicit proposal beyond the review's metadata-only tab.

### ADR-VIS-03: Lossy bounded capture protects processing

**Decision:** Prepared copies into owned SPSC slots feed bounded off-callback analysis; full queues drop new observations. Audio never waits, paints, performs FFTs or destroys capture resources.

**Why:** A stalled worker, UI or second monitor cannot retain borrowed audio buffers or back-pressure processing. Independent stop/fault handling stays reliable.

**Tradeoffs:** Capture has nonzero copy/cache cost and observations can lose continuity. Coverage-sensitive metrics must invalidate/refuse demand rather than claim complete readings. A shared ring overwritten in place or a mutex-protected UI access path is rejected because readers could race or block rendering.

### ADR-VIS-04: Floating and docked monitors share workspace ownership

**Decision:** One panel identity survives dock/float/tab/move-to-monitor actions. Persist machine-local layout descriptors with DPI/disconnect recovery, not live samples or audio activation.

**Why:** Enables comparison and multi-monitor analysis without reopening analyzers or tying project meaning to a physical display.

**Tradeoffs:** Native-window focus, accessibility, DPI, resource and restoration behavior require platform qualification. Ship reliable tab/split/recovery before promising every docking state; no current full-layout persistence is asserted.

### ADR-VIS-05: Task-oriented presets, not task-specific engines

**Decision:** Provide Beginner, Patch Design, DSP Debugging, Audio Analysis and Performance Monitoring templates over the same document, tools and observation service.

**Why:** Different users need different information density, not separate project models. Beginner simplicity and advanced analysis can coexist.

**Tradeoffs:** Templates can trigger demand spikes or hide a useful panel. Admission is incremental, layout changes are reversible, recovery is discoverable, and audio/settings/drafts remain untouched. Auto-creating analyzer nodes or changing device setup with a preset is rejected.

### ADR-VIS-06: Semantic correctness precedes apparent smoothness

**Decision:** Share compatible analysis, decouple acquisition/analysis/paint rates and expose quality, gaps and provenance.

**Why:** Smooth bars are not evidence of fresh or continuous measurements. Larger FFTs, hidden histories and loudness integration have distinct resource and validity requirements.

**Tradeoffs:** Users see Waiting/Degraded states and may need to choose a lower-cost profile. This is preferable to a fast-looking fabricated zero, interpolated missing history or silently inaccurate measurement.

## 9. Qualification plan and open decisions

No measured acceptance results are supplied. Before claiming a capability, qualify:

- **Numerical behavior:** known impulse/sine/DC/silence/noise signals, over-range/non-finite values, explicit peak/RMS conventions, FFT normalization/leakage/bin spacing, trigger boundaries and channel mapping. Correlation covers identical, inverted, unrelated and zero-energy inputs. Loudness needs selected standard/reference conformance vectors and gap/reset tests before a compliance claim.
- **Ownership and boundedness:** stalled workers/UI, full rings/result pools, consumer skips, variable/oversized blocks, allocation/destruction instrumentation, verified lock-free atomics, no overwritten Reading slots, and teardown with in-flight jobs. Test graph swaps, removed/undo-restored endpoints, project close, epoch mismatch and repeated stop/start.
- **Performance:** compare monitoring disabled with increasing unique taps and shared subscribers on reference ASIO/CoreAudio hardware. Record callback tail distributions/deadline events, process/analysis CPU, memory, capture bytes, UI frame times and freshness. Test at least the audio strategy's 30-minute steady run plus demand/visibility churn, high-rate/channel workloads and worst allowed capture profiles; zero observed failures is not proof of hard realtime behavior.
- **Scale and degradation:** large graphs, the proposed 100-preview/8-monitor demand fixture, many incompatible FFT profiles, frozen snapshots, offscreen groups, hidden tabs and explicitly armed histories. Verify responsive selection/editing/Stop and truthful resource refusals; rendering frame drops must not masquerade as capture gaps or vice versa.
- **Cross-platform UX:** mixed-DPI multi-display docking, keyboard-only move/resize/recover, display removal, sleep/wake, remote sessions, high contrast, large text, reduced motion and screen readers. Numeric summaries must match plot snapshot identity; do not announce every frame. Verify reachable windows and retained drafts/pins on Windows and macOS.
- **Usability:** a beginner finds an observation without rewiring; an experienced user pins A, selects B and explains the difference; a student reads FFT/window/latency provenance; an analyst notices lost coverage; a multi-monitor user recovers every panel after disconnect. Observe errors, task completion, understanding of stale/zero/frozen states and demand-denial recovery; set thresholds after baseline studies rather than inventing product success percentages.

Unsettled decisions include supported sample precision/rate/channel/block bounds, exact tap count and memory/CPU caps per hardware tier, worker scheduling and FFT implementation, calibrated spectral modes, trigger/window choices, loudness standard/profile and true-peak algorithm, continuous-history admission, rendering backend, complete layout schema/migration and third-party extension isolation. Analyzer-library groups still need mapping to product delivery scope. Settle these through profiling, numerical/reference tests and usability work, not by treating the proposed numbers as measured guarantees.

The end state is an observation system that can lose visual fidelity explicitly while preserving audio independence, measurement honesty and navigable project context.
