# Desktop audio workflow

The desktop composes the existing backend, audio engine, built-in DSP and
visualization services. Device enumeration starts automatically; playback never
starts automatically. Project editing and offline preview do not require an audio device.

## Configure a native device

Open **Audio > Audio Setup** to show the dedicated resizable device dialog.
This is the only Audio Setup entry; it is not a permanent workspace panel, and
there is no toolbar button or View-menu item. Close the window or choose
**Done** to hide it without changing transport. The main shell retains compact,
truthful stopped/setup-required, scanning, ready or running status.

The main workspace uses fixed Module Browser, Patch Canvas and Property
Inspector regions. The canvas cannot be collapsed. The side panels collapse
in place using their header controls and reopen from the small edge controls;
they cannot be dragged, floated or docked elsewhere. Legacy saved dock
positions are ignored while panel visibility is retained.

The **Help > About Visual Audio Lab** dialog displays the bundled application
logo, the product version from the CMake project metadata and the source
revision captured when CMake configures the build. Reconfigure after changing
the source revision to refresh the displayed build identifier. The dialog is
single-instance, uses a responsive compact layout when needed, and is fitted
to the usable area of the display containing the main window. Dismiss it with
its Close button, Escape, or the native window close control.

The main window is fitted in JUCE's logical display coordinates against the
monitor's usable `userArea`, which excludes reserved work-area space such as
the taskbar. For a native-titlebar window, the fit includes JUCE's current
logical non-client frame extents as well as the app's client area, so the
caption and resize frame remain on-screen. The same calculation runs for
startup, restored project bounds and when the window moves/resizes across a
display or its reported frame metrics change; it is deferred while maximized
or full-screen and reapplied when normal bounds return.

Saved project and machine-local window sizes need only have positive
dimensions; they are fitted to the current monitor when opened. The shell's
minimum outer size is 640×380 logical units and adapts to a smaller usable
area. On a 1920×1080 display at 175% Windows scaling, the full display is
roughly 1097×617 logical units before taskbar insets; Windows scaling and app
fonts are unchanged. Tests cover simulated 125%, 150%, 175% and 200% work areas
with representative nonzero native-frame extents. They verify geometry math,
not actual Windows titlebar rendering or a particular monitor/taskbar; qualify
those on the target PC.

1. Windows offers **ASIO**, **WASAPI Shared**, **WASAPI Exclusive** and **DirectSound**; macOS
   offers **CoreAudio**. Offline preview is separate. Foreign APIs are not
   offered. Windows defaults to ASIO; macOS defaults to CoreAudio, without fallback.
   JUCE supports DirectSound, not a separate MME backend, so MME is not advertised.
2. Construction and backend selection automatically enumerate devices. Choose an output
   device and either an input device or **None (output only)**. ASIO selects
   one driver; input is either that same driver or None, not a second driver.
   WASAPI selects separate Windows capture/render endpoints.
3. Endpoint selection automatically loads channel names, rates and buffer choices.
   There is no Detect button. **Refresh / Retry** is an optional recovery action.
   Status above the fields distinguishes loading, failed and ready states; hover
   it for the full diagnostic. Inspection uses one temporary JUCE device for the
   actual endpoint pair. Initializing a driver can affect hardware: ASIO inspection
   may briefly start/stop its driver even though no application stream is opened.
   Native enumeration/inspection run in a separate helper process with its own
   JUCE message thread and event loop. The desktop remains responsive; a crashed
   helper fails visibly, and a hung inspection is terminated after 15 seconds.
   Changing endpoints discards obsolete results; changing backend cancels its helper.
4. Choose the reported **Sample rate** and **Buffer size**. Audio Setup does not
   select channel counts, mono/stereo formats or hardware channels: those belong
   to the fixed Inputs and Outputs lanes beside the canvas. The selected device
   inventory supplies all reported names to the lane editors. Basic assignments
   select a named mono channel or stereo pair; Advanced supports wider and sparse
   exact channel selections up to 32 channels. The displayed names are the
   driver's names (an empty name is marked **name not reported**), not a claim
   about physical sockets. JUCE does not classify physical, virtual or loopback
   channels; check the vendor mixer where that distinction matters. WASAPI pair candidates follow
   JUCE's combined-device rules (including shared-mode mixer conversion), not
   an intersection of independently queried endpoint periods. Shared-mode
   **Callback frames** configure the JUCE processing block; Windows manages
   the hardware period. A single reported candidate is shown as such.
   Empty lists mean unreported/unsupported, never invented defaults or proof
   that a setting is host-managed. Disabled controls show pending/error/retry
   guidance. Start stays unavailable until a valid explicit selection and
   routable output are available.
5. Build a valid patch, check output levels, then explicitly click **Start**
   or press **F5**. There is no master limiter. **Stop** / **Shift+F5** closes
   the stream before configuration changes. Open verifies rate, buffer and
   exact channel masks as well as counts; a driver substitution fails visibly rather than silently
   changing the requested settings.

Unavailable drivers, stale devices, unsupported configurations and stream
errors are shown in the panel/status bar. No silent fallback occurs. Device
inventory changes stop transport and invalidate capabilities. Exact unambiguous
device-name matches can be restored, but missing/ambiguous devices are never
replaced. Discovery and capability revalidation are automatic; Start remains explicit.
**Control Panel** is enabled only when the driver reports support. It runs in the
isolated helper, closes after a five-minute timeout if the vendor panel hangs,
and automatically refreshes capabilities afterwards without starting transport.
Explicit stream open/start/stop remain on the desktop JUCE control thread:
those user-requested vendor operations are not made crash-proof or time-bounded by
the discovery helper. Physical driver qualification remains necessary.

Validated backend/device names, rate and buffer persist in machine-local
`settings.json` using the existing preference lifecycle. The local endpoint
mapping associates stable project row IDs and saved channel names with current
driver indices. These are resolved against fresh inventory and never substitute
another channel silently. Launch may initialize hardware to revalidate choices
but never starts application playback. Tokens and
transport state are never persisted. Old window-only preferences remain valid;
malformed preferences preserve their original file and disable automatic writes.
Legacy count-only settings retain their historical width but have no exact
channel identity; they do not select first-N hardware channels. Assign rows
explicitly in the canvas lanes. CoreAudio keeps its existing numerical value 3; DirectSound is appended as 4, and the UI's
offline-preview item moves from ID 5 to 6 (offline is not a persisted backend).

## Try DSP without opening hardware

Choose **Offline preview (no hardware)**. It offers a 48 kHz, 512-frame preview
with selectable output bus width. Select one channel before inserting a
mono-configured generator.

Use the **Module Browser** category filter and search to find a shipped entry.
Categories are **Effects**, **Generators**, **Analysis**, and **Routing utilities**;
unavailable/future modules are not listed as empty categories. The
Routing utilities category contains the explicit **Sum** processor; hardware
Input and Output are fixed endpoints, not browser modules. Insert
the selected result with the button, double-click, or Enter, or drag it onto
the Patch Canvas to place it at the pointer. Each insertion is one undoable
edit with its runtime identity already attached. Dragging outside the canvas
area is rejected with a visible status message. Insertion never auto-wires:
connect ports explicitly.
A **module** is a reusable audio function; a placed **node** is its instance
with its own saved parameters and runtime state. The fixed left Inputs
lane contains configured hardware input rows and inserted built-in generator
source cards; the fixed right Outputs lane contains hardware output rows.
Endpoint rows have stable project identities and cannot be moved as processors.
Use **+ Add input/output** to add an unassigned row; no row addition binds a
device, wires a cable or starts playback. Assign exact driver-named channels in
the lane, enable/disable or remove rows there, and use the explicit connection
ports to route them.
Analyzer modules observe audio while passing it through unchanged. Reusable
patch packaging is not newly implemented.

Supported source kinds are configured hardware inputs and the shipped internal
generators; the supported destination kind is the configured main hardware
output. Input is optional, so generators can feed output without capture.
Unassigned/disabled rows remain inert. A disconnected route can render silence.
Application Capture, File Player,
Virtual Audio Device, Recorder/File Writer, Streaming and plugin-hosted
endpoints are not implemented.

For a device-free built-in DSP example:

1. Insert **Signal Generator**, **Gain** and **Oscilloscope**.
2. Connect `Signal Generator -> Gain -> Oscilloscope ->` the configured output row.
3. In the generator's Properties, choose the DC waveform, set its DC value to
   `0.25` under Advanced, and set output level to `0 dB`. Set Gain to `0.5`.
4. Start offline preview. Select the Oscilloscope node to see the live signal
   (constant amplitude `0.125` in this example). Stop explicitly when finished.

Offline processing uses silent input and sends output only to observation
buffers. A UI timer advances one block per tick, not a hardware-rate clock:
automation delays are **audio sample time**, not wall-clock preview time.
Offline callback timing is synthetic, not a real-device performance measurement.

Hardware input rows read their assigned buses; hardware output rows write to
their assigned channel groups. Each destination accepts one feed; use **Sum**
to combine routes explicitly. Independent cable buses preserve their declared
widths. Matching layouts connect directly; the implemented mono-to-stereo
adapter is visible and saved on the cable. Other incompatible widths are
rejected rather than silently truncated, duplicated or converted. The
Oscilloscope, Spectrum Analyzer, Peak Meter, RMS Meter and Guitar Tuner catalog
entries are pass-through Analyzer nodes with distinct initial display choices,
not independent audio processing pipelines. The Guitar Tuner uses the selected
node's measured signal and reports chromatic note/octave, cents sharp/flat,
in-tune state, confidence and silence/uncertain feedback against A4=440 Hz.
Connect it inline with one guitar signal and select the node to open its tuner
view. It is monophonic; chords, string identification and alternate tunings are
not implemented. Its analysis is synthetic-test qualified, not physically
qualified against every pickup, device or acoustic environment.

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

## Legacy VAL Script projects

VAL Script execution, source editing, compilation and catalog insertion have
been retired. The `.valab` schema and generic scalar-parameter codec remain
unchanged: legacy module type/source, scalar mapping fields, other parameters,
node identity/geometry and graph connections are retained on load and save.
They are opaque data, not executable features. Starting a graph that contains
a retired Script node or a `source: "script"` scalar mapping produces an
explicit unsupported-feature diagnostic; the application does not execute,
silently disable, delete or rewrite that content. Replace affected modules and
mappings with supported built-in modules before running the patch. Custom DSP
authoring is external to Visual Audio Lab; see
[ValScriptRetirementPlan.md](ValScriptRetirementPlan.md).

**File > Save Project / Save Project As / Load Project / Recent Files** retain
module identity, scalar parameters, bypass, analyzer display kind, graph
topology, patches and the existing window layout in human-readable `.valab`
JSON. Dirty-project consent remains in the existing file workflow. Load and
patch switches quiesce transport before replacement and never restart
automatically.

## Live graph and signal inspection

Canvas edits, undo/redo and parameter changes synchronize
the active patch with the engine. Position/name-only changes do not rebuild DSP.
Invalid structural/source edits remain editable and display **last accepted audio
continues**; Stop stays available. Rejected parameter delivery stops transport
to avoid partial/ambiguous targets. Correct the patch and restart if stopped.

**Routing Draft** / **N** and old generic saved nodes remain usable in the editor.
They are clearly labeled structural utilities with no DSP, not executable
pass-through modules, until they have a supported explicit runtime identity.
The runtime graph supports multiple independent sources and destinations,
fan-out and legal unrouted/inert processors. A destination accepts a single
feed, and explicit Sum is required for mixing. Bus widths are checked per port;
the only implemented layout adapter is mono-to-stereo. Cycles, incompatible
layouts and overlapping output channel assignments are rejected with diagnostics.
Each hardware output row maps to a disjoint channel group of the configured
audio-device session. The implementation does not synchronize arbitrary
independent devices or offer generalized resampling.

Selecting a captured node follows it in **Signal**. Selecting an analyzer also
chooses its initial display. The five tabs share calibrated scope, spectrum,
sample-peak, raw-RMS and pitch products. **Signal Inspector** opens a non-modal
five-plot monitor pinned to the current target; its source selector and
Pin/Freeze controls remain independent. Embedded canvas sparks use the same
sources.

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
and [ProjectFormat.md](ProjectFormat.md) for persistence and compatibility.

Native hardware timing, audible processing and macOS device operation require
platform/hardware qualification; offline and injected-device GUI tests do not
establish them. Plugin hosting/export, MIDI, persistent automation lanes and
Application Capture, File Player, Virtual Audio Device, Recorder/File Writer,
Streaming, plugin hosting, generalized resampling and persistent automation
lanes remain outside this integration. Panel docking and
re-docking are not supported.
