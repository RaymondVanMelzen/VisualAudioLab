# Audio backend discovery

## Implemented scope

`IAudioBackend` is an application-owned, platform-neutral C++20 discovery boundary. Infrastructure provides `AsioBackend`, `WasapiExclusiveBackend`, `WasapiSharedBackend`, `CoreAudioBackend` and `DirectSoundBackend` through the pinned JUCE 8.0.12 device module. Production discovery is real; tests inject device sources or subprocess fixtures.

The discovery abstraction includes native stream control for the [realtime engine](AudioEngine.md). The [desktop runtime UI](DesktopWorkflow.md) automatically enumerates on construction/backend selection and inspects selected endpoints, but opens/starts streams only on explicit Start. Backend/device names and selected settings are machine-local preference hints; tokens are never persisted as stable hardware identities. Windows offers ASIO, WASAPI Shared/Exclusive and genuine JUCE DirectSound; macOS offers CoreAudio. JUCE exposes no separate MME implementation.

The preferred factory returns ASIO on Windows and CoreAudio on macOS. It does not choose a device, start audio, or fall back to another API. WASAPI Shared and Exclusive are separately required implementations and are never substituted. An available API with an empty inventory does not imply usable hardware.

## Contracts and identities

Application types contain no JUCE, UI, native handle or platform header. A snapshot contains a backend kind, generation, endpoint descriptors, actionable error and diagnostic notes. Descriptors expose direction, display name and an informational system-default flag where JUCE provides it; ASIO has no automatically chosen default.

Device tokens combine backend kind, backend-instance identity, inventory generation and an opaque locator. They are **session-scoped**, not persistent hardware identities. JUCE publicly exposes name-based device locators, not native endpoint UIDs. Separate input/output endpoints are listed independently even if their names match; an ASIO driver is one potentially duplex endpoint. Channel counts are queried, not inferred from that label. Ambiguous locator keys reject the inventory.

Explicit refresh, native invalidation and changed polling results advance the generation; an unchanged background poll preserves it. A token from another backend instance, API, earlier inventory or unplug/replug cycle is rejected before probing. Clients rediscover/reselect after invalidation. Do not serialize these tokens into projects or preferences.

Capability queries return driver-reported input/output channel names, candidate sample rates, candidate buffer sizes and an optional default buffer size. Empty rate/period lists mean unknown/unreported, not universal support. These are **not an exhaustive Cartesian product**: rate, channel format/layout and period may be interdependent. JUCE probes a finite set of formats and may synthesize period candidates from driver limits. Shared-mode candidates may reflect Windows mixer conversion, while exclusive candidates do not prove that exclusive access will be granted.

The setup UI uses `queryConfigurationCapabilities` to inspect the selected input/output pair with one temporary JUCE device. ASIO uses one duplex driver, avoiding two consecutive vendor initializations. WASAPI delegates pair rate/period composition to JUCE: shared mode permits mixer conversion and exclusive mode uses common formats. Independent endpoint buffer-list intersection is not valid pair discovery. In pinned JUCE, shared-mode buffer candidates represent application callback blocks; the underlying Windows period is host-managed, not an exposed hardware-size choice. Empty lists do not establish host-managed or fixed behavior.

The capability result's `negotiated` field remains absent: a capability query is not a running stream. Current stream settings are returned separately by explicit successful `openStream`, never inferred from a default, first candidate, OS mix format or driver-reported nominal rate. Native open rejects a substituted rate, buffer, channel count or channel identity mask with an actionable setup error. Requests carry ascending, unique input/output driver-index vectors, matching their widths; JUCE `BigInteger` bits are set individually, never using `setRange(0, count)`. Basic UI offers mono channels and contiguous stereo pairs; Advanced adds wider contiguous groups up to 32. Noncontiguous masks can be represented by the contracts but are not editable in this UI.

Creating a temporary JUCE device can initialize hardware. In particular, JUCE 8.0.12 ASIO initialization creates dummy buffers and may briefly start/stop the driver; it may also repair an invalid driver rate. Automatic endpoint inspection therefore touches hardware, even though no application stream or render callback is installed. It runs in an isolated discovery helper, not in the desktop process. Unattended tests use injected devices/process fixtures and never probe physical ASIO hardware.

## Ownership, scheduling and cancellation

Construct, use, shut down and destroy the adapters on the JUCE message/control thread, with a live `MessageManager`/event loop. Wrong-thread control requests fail explicitly. Construction/destruction have the documented owner-thread precondition. `kind()` is immutable. No API is real-time safe.

1. Register a short, non-throwing change callback and call `refresh()`.
2. Receive a later snapshot; errors include unsupported platform, disabled ASIO, unavailable driver and ambiguous inventory.
3. Pass a current token to `queryCapabilities()` and handle its later result/error. A missing completion or full queue is rejected immediately without a callback.
4. On shutdown, unregister notifications, cancel scheduled work and destroy temporary driver resources on the owner thread. Pending requests are cancelled without callbacks. Recreate an adapter to restart it.

Refresh requests coalesce. At most 32 capability requests/results can be queued. Discovery, one capability query, and client notifications execute in separate message-loop phases. Reentrant callback requests enqueue later work rather than recursively calling a driver or another observer. A callback may shut down or destroy its backend; remaining deliveries are suppressed. Replacing/removing an observer cancels its pending notification. Observed device changes before result delivery convert a result to a stale-token error; OS notifications themselves are asynchronous, not instantaneous hardware-state guarantees.

Native discovery/probes run on the **helper's** JUCE message thread and dispatch loop. Moving the existing JUCE objects onto a plain worker would not be safe: ASIO uses process-global callback slots, `CoInitialize`, JUCE's Windows message window and timers; CoreAudio/WASAPI also register owner-bound notifications. Instead, each operation creates/uses/destroys its native type in a separate process. A desktop worker only launches/polls IPC; it never calls JUCE device methods. The desktop stream source defers even native type construction until explicit open, so automatic setup does not register driver types/property listeners on the UI thread.

`VisualAudioLab.AudioDiscovery.Helper` is built and copied beside the desktop executable (inside `Contents/MacOS` on macOS). Deployment must include it. Unique request/response JSON files live under the machine-local `VisualAudioLab/Discovery` directory and are deleted by their owning worker after completion, cancellation or timeout. Responses are limited to 1 MiB. A helper crash/missing binary is an explicit error, not an empty success. Enumeration/probing have a 15-second deadline; vendor Control Panel has a five-minute deadline. Shutdown clears owner-thread callback captures, signals cancellation and joins the IPC worker, never waiting in vendor code. The worker checks cancellation every 10 ms, terminates only its own child process (with at most a one-second process-exit wait), and cleans its IPC files. Normal shutdown cannot leave a detached worker/driver panel behind. OS process launch and filesystem operations are not hard-real-time APIs. Abrupt termination of the entire desktop/OS can leave orphaned files; no broad cleanup of unrelated files is performed.

Discovery isolation does not isolate streaming. Explicit native open/start/stop/close and render callbacks still use the desktop process and existing JUCE control ownership. User-requested vendor stream operations may block or crash there; no hard bound or physical hardware qualification is claimed. The helper never performs a native application-stream open or installs a renderer.

No client callbacks execute inside native notification handlers, device queries or real-time processing. Native handlers only schedule invalidation; notifications do no DSP and allocate no application real-time processing state.

## Hot plug and failure behavior

- Once the desktop stream source is initialized by explicit open, WASAPI uses JUCE endpoint-change listeners and CoreAudio uses JUCE hardware-property listeners. Before first open, isolated two-second polling detects inventory changes without constructing those native types on the UI thread.
- The initialized ASIO stream source adds Windows Configuration Manager device-interface notifications. A PnP change invalidates tokens even when the registered driver name remains unchanged. This broad subscription can also invalidate on unrelated device-interface changes, favoring safety over token stability. Before first open, polling can detect driver registration changes but cannot prove physical connectivity.
- Two-second inventory polling supplements notifications, including ASIO driver installation/removal. Polling does not automatically initialize/probe devices.
- A failed ASIO PnP subscription is an explicit snapshot diagnostic; polling/explicit probes remain available. Native subscriptions unregister and quiesce before destroying their callback context.
- ASIO registry enumeration describes installed drivers, not authoritative physical connection state. A disconnected interface may still have an installed driver; explicit capability probing checks usability. Neither JUCE nor this adapter can force a vendor driver to expose hot-plug state it does not report.
- A disappearing or busy device returns an unavailable/stale error, never a shared-mode fallback. No native device handles escape a probe or survive for later dereference.
- JUCE may report an empty inventory for no active devices or some native enumeration failures; the adapter cannot recover native error detail JUCE does not expose. Empty inventories are not evidence of hardware availability.

The explicit stream extension now quiesces/closes on invalidation and requires reselection/restart; see [AudioEngine.md](AudioEngine.md). Capability probing while a stream is open is rejected. Native realtime error signaling does not invoke ordinary change callbacks or allocate error text.

## Build and licensing

The existing dependency policy pins JUCE commit `29396c22c93392d6738e021b83196283d6e4d850` (8.0.12), with offline provisioning supported. Infrastructure privately links `juce_audio_devices`. Module definitions are consistent across JUCE consumers:

```text
JUCE_ASIO=<VAL_ENABLE_ASIO, default ON>
JUCE_ASIO_USE_EXTERNAL_SDK=0
JUCE_WASAPI=1
JUCE_DIRECTSOUND=1
```

Windows also links the SDK's `Cfgmgr32` for device-interface notifications. Non-native classes return unsupported-platform errors, rather than pretending to enumerate another API.

JUCE 8.0.12 already bundles the ASIO interface headers it uses. No third-party SDK source is copied into this repository and no external SDK download is introduced. Enabling ASIO makes the result subject to those bundled headers' terms as well as JUCE's license; review the pinned dependency notices and distribution requirements before shipping. `-DVAL_ENABLE_ASIO=OFF` excludes ASIO support and returns an explicit disabled error on Windows; it does not select WASAPI automatically. A compatible installed vendor driver is still required when enabled.

## Validation

`Audio.BackendContracts` uses the injectable `IAudioDeviceDiscovery` seam to exercise mapping, distinct APIs/instances, coalescing, asynchronous/non-reentrant callbacks, wrong-thread rejection, unplug/replug generation changes, disappearance during a probe, failed enumeration/probes, ambiguous IDs, observer replacement, queue saturation, shutdown cancellation and callback-driven destruction. `Audio.StreamLifecycle` also verifies that configuration inspection resolves both endpoints and preserves native pair candidates rather than intersecting independent periods. `Presentation.Runtime` covers platform filtering, one-probe ASIO duplex selection, setup hint restoration, pending/failed/empty capability controls, refresh callback ordering and explicit transport with an injected backend.

`Audio.BackendContracts` additionally launches real subprocess fixtures (no native driver calls) to verify responsive message-loop heartbeats, labeled IPC results, timeout/crash failures, Control Panel completion, cancellation and exact-file cleanup. `Audio.StreamLifecycle` verifies explicit masks reach the native seam and malformed masks reject before opening. `Presentation.Runtime` exercises automatic scan/probe, labeled later-port selection, exact Start requests, mismatched widths, failures/retry, stale selection completions, platform filtering and no automatic transport. Local settings tests cover every backend value and count-only migration.

`Audio.NativeDiscovery` enumerates all five production backends, checks platform/ASIO build guards and queries available non-ASIO endpoints through the helper. ASIO capability probing is skipped because it can touch live hardware. The test is labelled `native-audio`; exclude it on machines where native endpoint probing is inappropriate. These tests require the helper beside the test executable:

```powershell
ctest --test-dir build\windows-native -C Debug --output-on-failure
ctest --test-dir build\windows-native -C Debug -LE native-audio --output-on-failure
```

Physical unplug/replug, vendor ASIO capability probing, sleep/wake, driver crashes and macOS permission/hardware behavior require dedicated hardware qualification. Injected hot-plug tests are not evidence that these physical scenarios passed. No latency, exclusive stream opening, DSP, real-time workload or cross-platform runtime performance qualification is claimed.
