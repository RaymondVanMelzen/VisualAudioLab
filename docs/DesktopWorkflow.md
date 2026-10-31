# Desktop audio workflow

The desktop composes the existing backend, audio engine, DSP, VAL Script and
visualization services. It does not start audio or initialize a native backend
on launch. Project editing and offline preview do not require an audio device.

## Configure a native device

Open **Audio > Audio Settings** (also on the toolbar). The non-modal dock is
scrollable when its height is small.

1. Choose **ASIO**, **WASAPI Exclusive**, **WASAPI Shared** or **CoreAudio**.
   Windows defaults to ASIO; macOS defaults to CoreAudio. Unsupported APIs are
   reported explicitly, never replaced with another API.
2. Click **Scan devices** to enumerate the selected backend. Choose an output
   device and either an input device or **None (output only)**.
3. Click **Inspect selected devices** to query channels, sample rates and buffer
   sizes. This is deliberately separate from enumeration: initializing a
   driver can affect hardware, and ASIO inspection may briefly start/stop it.
4. Select the reported sample rate, buffer frames and output channel count.
   Input count must be zero or match output count. Counts select the first N
   reported channels; tooltips list their names. Shared input/output choices
   are the intersection of reported capabilities, not a guarantee that every
   rate/buffer combination will negotiate.
5. Build a valid patch, check output levels, then explicitly click **Start**
   or press **F5**. There is no master limiter. **Stop** / **Shift+F5** closes
   the stream before configuration changes.

Unavailable drivers, stale devices, unsupported configurations and stream
errors are shown in the panel/status bar. No silent fallback occurs. Device
inventory changes stop transport and clear the selections; inspect and restart
explicitly. Device configuration is session-local, not a portable saved device
token.

## Try DSP without opening hardware

Choose **Offline preview (no hardware)**. It offers a 48 kHz, 512-frame preview
with selectable output bus width. Select one channel before inserting a mono
script example.

Use the **Module Browser** search and **Insert selected module**, or
double-click/press Enter on a selected result. Available modules are Input,
Output, Gain, EQ, Delay, Compressor, Distortion, Chorus, Reverb, Oscilloscope,
Spectrum Analyzer, Peak Meter, RMS Meter and Script. Each insertion is one
undoable edit with its runtime identity already attached.

For a device-free example:

1. Insert **Script**, **Gain**, **Oscilloscope** and **Output**.
2. Drag their ports to connect `Script -> Gain -> Oscilloscope -> Output`.
3. Select Script and use **Properties / Script** to load an example into the
   draft, or enter the following mono audio-rate source:

   ```pascal
   NODE Preview AT AUDIO;
   OUTPUT Y: REAL;
   PARAM Level: REAL = 0.25 RANGE 0 .. 1;
   BEGIN
     Y := Level
   END.
   ```

4. Click **Compile / Apply**. Set Gain's saved gain target to `0.5`.
5. Start offline preview. Select the Oscilloscope node to see the live signal
   (constant amplitude `0.125` in this example). Stop explicitly when finished.

Offline processing uses silent input and sends output only to observation
buffers. A UI timer advances one block per tick, not a hardware-rate clock:
automation delays are **audio sample time**, not wall-clock preview time.
Offline callback timing is synthetic, not a real-device performance measurement.

Input reads the configured input bus; Output writes the output bus. Four
analyzer catalog entries are pass-through Analyzer nodes with distinct initial
display choices, not four independent audio analysis pipelines.

## Parameters, bypass and automation

Selecting an executable node opens controls derived from its actual processor
metadata, including units/ranges. Slider drag completion or a committed text
value creates one undoable saved base-target change. Bypass is saved and
undoable; effects crossfade to dry, while generators/sources crossfade to silence.
The processor's existing smoothing and sample-offset event machinery are used.

Live controls expose **override**, **release override**, **disable automation**
and a target scheduled **0.1-9 audio seconds ahead**. Override wins over
automation; releasing it reveals automation or the saved base. Disabling
automation restores base after override release. Scheduled events carry the
engine epoch, graph revision and sample position. Queue rejection and expired/
canceled events are visible; automation events and live layers are **transient**,
not an automation-lane editor or saved arrangement.

Ordinary parameter edits keep the current processor state. Undo that removes
an explicit named parameter restores its default through a prepared graph
revision. Structural/source revisions reset processor state and cancel
automation for the old revision.

## Script source and persistence

The Script editor provides multi-line source, asynchronous **Load Source**,
four built-in example drafts, compiler diagnostics with line/column, and
**Compile / Apply**. Input/output counts must match the selected runtime bus
(or a script can have zero inputs); mono examples require a mono bus.
Only the already implemented stateless audio-rate mode is executable; CONTROL
mode and prohibited capabilities produce diagnostics, not a substitute mode.

Source is visibly a **draft, not saved until Compile / Apply**. Refresh,
selection and undo preserve unapplied drafts in memory. **Load Source** replaces
the selected draft; **Revert to saved source** deliberately restores the current
saved source without editing project history. A loaded project with reused
identities can retain a conflicting draft: the conflict is labeled, and Revert
restores the loaded source. Drafts are not recovered after editor destruction/
application close; compile/apply source you intend to retain.

Successful compilation persists `audio.script` as one edit. Incompatible saved
parameter targets block apply with a diagnostic instead of being discarded.
Compiler work, graph validation, preparation and reclamation run off the audio
callback.

**File > Save Project / Save Project As / Load Project / Recent Files** retain
module identity (`audio.type`), script source, named parameter targets, bypass,
analyzer display kind, graph topology, patches and the existing window layout
in human-readable `.valab` JSON. The existing schema/unknown-field preservation
rules apply; no format migration is needed. Dirty-project consent remains in
the existing file workflow. Load and patch switches quiesce transport before
replacement and never restart automatically.

## Live graph and signal inspection

Canvas edits, undo/redo, parameter changes and successful source apply synchronize
the active patch with the engine. Position/name-only changes do not rebuild DSP.
Invalid structural/source edits remain editable and display **last accepted audio
continues**; Stop stays available. Rejected parameter delivery stops transport
to avoid partial/ambiguous targets. Correct the patch and restart if stopped.

Generic structural **Add Node** and old saved nodes remain usable in the editor.
They are not executable until they have a supported explicit runtime identity.
There is no implicit mixer/channel conversion: the current engine supports one
matching bus, one upstream connection per processing node and one Output;
all runtime nodes must contribute to that Output. Structural graphs can be much
larger than the runtime limit of 256 nodes/edges.

Selecting a captured node follows it in **Signal**. Selecting an analyzer also
chooses its initial display. The four tabs share the calibrated scope/spectrum/
sample-peak/raw-RMS products. **Signal Inspector** opens a non-modal four-plot
monitor pinned to the current target; its source selector and Pin/Freeze controls
remain independent. Embedded canvas sparks use the same sources.

Refresh controls are 1-30 Hz for inspector/monitor and 1-15 Hz for embedded views;
hidden/offscreen views release demand. Capture admission is bounded to at most
eight sources, first two channels each, sixteen channels overall and 768,000
captured samples/second. Analyzer nodes get admission priority on start, then
other non-Input nodes. Unadmitted targets show unsupported/not scheduled rather
than fake silence. At a saturated limit, deleting a source frees admission after
the replacement revision becomes active; adding a replacement may require
another edit or stop/start.

Status diagnostics distinguish backend/device, negotiated rate/buffer, reported
input/output latency, callback duration/deadline DSP load, backend xruns and
callback misses. Missing latency/xrun data and whole-process CPU are explicitly
**unavailable**, not zero. Reported latency is not a physical round-trip
measurement. Capture gaps and source/epoch/revision provenance are shown in the
signal views. See [SignalVisualization.md](SignalVisualization.md) for calibration
and [ValScript.md](ValScript.md) for language/resource limits.

Native hardware timing, audible processing and macOS device operation require
platform/hardware qualification; offline and injected-device GUI tests do not
establish them. Plugin hosting/export, MIDI, persistent automation lanes, an
expanded language and modern drag-docking remain outside this integration.
