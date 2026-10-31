# Audio backend discovery

## Implemented scope

`IAudioBackend` is an application-owned, platform-neutral C++20 discovery boundary. Infrastructure provides `AsioBackend`, `WasapiExclusiveBackend`, `WasapiSharedBackend` and `CoreAudioBackend` through the pinned JUCE 8.0.12 device module. Production discovery is real; only tests inject a fake device source.

The discovery abstraction includes native stream control for the [realtime engine](AudioEngine.md). The [desktop runtime UI](DesktopWorkflow.md) now exposes explicit discovery, capability inspection, configuration and transport. Backend/device choices remain session-local; tokens are not persisted as stable hardware identities. The desktop remains usable without audio and does not construct/probe these adapters until explicit Scan.

The preferred factory returns ASIO on Windows and CoreAudio on macOS. It does not choose a device, start audio, or fall back to another API. WASAPI Shared and Exclusive are separately required implementations and are never substituted. An available API with an empty inventory does not imply usable hardware.

## Contracts and identities

Application types contain no JUCE, UI, native handle or platform header. A snapshot contains a backend kind, generation, endpoint descriptors, actionable error and diagnostic notes. Descriptors expose direction, display name and an informational system-default flag where JUCE provides it; ASIO has no automatically chosen default.

Device tokens combine backend kind, backend-instance identity, inventory generation and an opaque locator. They are **session-scoped**, not persistent hardware identities. JUCE publicly exposes name-based device locators, not native endpoint UIDs. Separate input/output endpoints are listed independently even if their names match; an ASIO driver is one potentially duplex endpoint. Channel counts are queried, not inferred from that label. Ambiguous locator keys reject the inventory.

Explicit refresh, native invalidation and changed polling results advance the generation; an unchanged background poll preserves it. A token from another backend instance, API, earlier inventory or unplug/replug cycle is rejected before probing. Clients rediscover/reselect after invalidation. Do not serialize these tokens into projects or preferences.

Capability queries return driver-reported input/output channel names, candidate sample rates, candidate buffer sizes and an optional default buffer size. Empty rate/period lists mean unknown/unreported, not universal support. These are **not an exhaustive Cartesian product**: rate, channel format/layout and period may be interdependent. JUCE probes a finite set of formats and may synthesize period candidates from driver limits. Shared-mode candidates may reflect Windows mixer conversion, while exclusive candidates do not prove that exclusive access will be granted.

The capability result's `negotiated` field remains absent: a capability query is not a running stream. Current stream settings are returned separately by explicit successful `openStream`, never inferred from a default, first candidate, OS mix format or driver-reported nominal rate.

Creating a temporary JUCE device can initialize hardware. In particular, JUCE 8.0.12 ASIO initialization creates dummy buffers and may briefly start/stop the driver; it may also repair an invalid driver rate. **ASIO probing is explicit, never part of enumeration, hot-plug refresh or unattended native tests.** No application audio callback is installed. Third-party drivers can be busy, block, fail or crash in-process; this adapter is not a driver sandbox.

## Ownership, scheduling and cancellation

Construct, use, shut down and destroy the adapters on the JUCE message/control thread, with a live `MessageManager`/event loop. Wrong-thread control requests fail explicitly. Construction/destruction have the documented owner-thread precondition. `kind()` is immutable. No API is real-time safe.

1. Register a short, non-throwing change callback and call `refresh()`.
2. Receive a later snapshot; errors include unsupported platform, disabled ASIO, unavailable driver and ambiguous inventory.
3. Pass a current token to `queryCapabilities()` and handle its later result/error. A missing completion or full queue is rejected immediately without a callback.
4. On shutdown, unregister notifications, cancel scheduled work and destroy temporary driver resources on the owner thread. Pending requests are cancelled without callbacks. Recreate an adapter to restart it.

Refresh requests coalesce. At most 32 capability requests/results can be queued. Discovery, one capability query, and client notifications execute in separate message-loop phases. Reentrant callback requests enqueue later work rather than recursively calling a driver or another observer. A callback may shut down or destroy its backend; remaining deliveries are suppressed. Replacing/removing an observer cancels its pending notification. Observed device changes before result delivery convert a result to a stale-token error; OS notifications themselves are asynchronous, not instantaneous hardware-state guarantees.

Asynchronous dispatch does **not** make driver queries nonblocking. Native discovery/probes run on the JUCE message/control thread because the JUCE types and their notifications share that ownership. An explicit probe can stall that thread in vendor code; future UI integration should make that operation visible/cancellable at the workflow level. There is no unsafe forced interruption of an in-progress native driver call. Shutdown waits until a running owner-thread call returns, then cancels queued work.

No client callbacks execute inside native notification handlers, device queries or real-time processing. Native handlers only schedule invalidation; notifications do no DSP and allocate no application real-time processing state.

## Hot plug and failure behavior

- WASAPI uses JUCE endpoint-change listeners; CoreAudio uses JUCE hardware-property listeners.
- ASIO adds Windows Configuration Manager device-interface notifications. A PnP change invalidates tokens even when the registered driver name remains unchanged. This broad subscription can also invalidate on unrelated device-interface changes, favoring safety over token stability.
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
JUCE_DIRECTSOUND=0
```

Windows also links the SDK's `Cfgmgr32` for device-interface notifications. Non-native classes return unsupported-platform errors, rather than pretending to enumerate another API.

JUCE 8.0.12 already bundles the ASIO interface headers it uses. No third-party SDK source is copied into this repository and no external SDK download is introduced. Enabling ASIO makes the result subject to those bundled headers' terms as well as JUCE's license; review the pinned dependency notices and distribution requirements before shipping. `-DVAL_ENABLE_ASIO=OFF` excludes ASIO support and returns an explicit disabled error on Windows; it does not select WASAPI automatically. A compatible installed vendor driver is still required when enabled.

## Validation

`Audio.BackendContracts` uses the injectable `IAudioDeviceDiscovery` seam to exercise mapping, distinct APIs/instances, coalescing, asynchronous/non-reentrant callbacks, wrong-thread rejection, unplug/replug generation changes, disappearance during a probe, failed enumeration/probes, ambiguous IDs, observer replacement, queue saturation, shutdown cancellation and callback-driven destruction.

`Audio.NativeDiscovery` enumerates all four production backends, checks the platform/ASIO build guards and preferred API, queries the channels/rates/periods of available WASAPI/CoreAudio endpoints and unregisters native listeners. It reports unavailable endpoints rather than mistaking them for negotiated streams. ASIO driver enumeration is real but capability probing is skipped because it can touch live audio hardware. The test is labelled `native-audio`; exclude it on machines where native endpoint probing is inappropriate:

```powershell
ctest --test-dir build\windows-native -C Debug --output-on-failure
ctest --test-dir build\windows-native -C Debug -LE native-audio --output-on-failure
```

Physical unplug/replug, vendor ASIO capability probing, sleep/wake, driver crashes and macOS permission/hardware behavior require dedicated hardware qualification. Injected hot-plug tests are not evidence that these physical scenarios passed. No latency, exclusive stream opening, DSP, real-time workload or cross-platform runtime performance qualification is claimed.
