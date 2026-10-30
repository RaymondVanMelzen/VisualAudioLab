# Visual Audio Lab: Audio Platform Strategy

**Status:** Revised architecture direction with proposed integration details; no implementation or performance results.

**Research as of:** 2026-10-30. All external sources listed below were accessed on this date.

## Recommendation

Use **C++20 with JUCE and CMake**, with a JUCE device adapter behind application-owned contracts:

- **Windows primary path:** ASIO with a user-selected, compatible installed driver.
- **macOS primary path:** CoreAudio through JUCE.
- **Future capabilities:** Preserve device-independent DSP boundaries for VST3 hosting and plugin export. These are architectural constraints, not implemented features or MVP/release promises.

This user-directed revision supersedes the earlier PortAudio selection, managed interop design, and required WASAPI modes. JUCE provides device lifecycle/callback facilities and ASIO/CoreAudio support [S10, S13], plus processor/hosting and CMake plugin-target facilities [S15, S16, S17]. Selection is not a claim of superior measured latency.

WASAPI Shared/Exclusive may be evaluated later as optional Windows compatibility paths. They are not required primary targets, fallback guarantees, or current release gates. Pin JUCE, toolchain/CMake and SDK revisions and qualify native packaging and licenses before distribution; no second device library or driver implementation is selected.

## Product and architecture alignment

This strategy applies the revised stack and primary-target decision in [TechnicalArchitecture.md](TechnicalArchitecture.md), using the responsibilities in [FunctionalArchitecture.md](FunctionalArchitecture.md) and the scope in [PRD.md](PRD.md).

Core audio use remains local and offline-first, without subscription, account authentication, cloud, or AI dependencies. The initial proposed workflow in the PRD is standalone audio output; input, recording, MIDI, VST3 hosting, plugin export, multi-device aggregation, and advanced signal inspection are not added to the MVP by this strategy. VST3 hosting and plugin export now have an explicit architectural requirement, but no release assignment.

**No drivers will be implemented.** Use existing OS audio facilities and installed hardware-vendor drivers. An audio library/adapter invokes those facilities; it does not supply a missing vendor ASIO driver or make unsupported hardware capabilities appear.

## Evidence and assumptions

- **Sourced facts** below describe documented capabilities. **Assessments** explain fit for this product and are not benchmark results.
- Platform API availability does not prove that a packaged application enables it correctly. Verify the pinned JUCE build and its platform/module configuration; ASIO is not enabled by default in the reviewed module header. [S13]
- C++20, JUCE and CMake are fixed direction; exact framework/compiler/build-tool versions, OS minimums and deployment policy remain open.
- Recommended initial qualification candidates are Windows x64 and macOS Apple Silicon; macOS Intel and Windows ARM64 require explicit support decisions. This is a proposed test-matrix starting point, not a declared support matrix.
- The product's open-source license remains undecided. Library licenses, bundled dependencies, ASIO SDK terms, and vendor-driver distribution rights require review for the versions actually shipped.
- Keep the initial standalone device model to one selected output stream. Concurrent projects, simultaneous streams, input/output synchronization, and automatic default-device following remain separate decisions. A future exported plugin receives buffers from its host rather than opening a stream.

## Native APIs versus portable libraries

| Layer | Meaning in this strategy |
|---|---|
| ASIO | Primary Windows driver-facing API used with an installed compatible driver, generally associated with professional audio interfaces. It is not available on every computer. |
| WASAPI Shared / Exclusive | Possible future Windows compatibility paths, not primary requirements. Shared mode coexists with other audio; exclusive access may block other applications and requires explicit policy/consent. Neither mode guarantees a latency result. [S5] |
| CoreAudio | Apple's native audio framework/HAL for device access on macOS, reached through the chosen library rather than reimplemented. [S6] |
| JUCE | Selected C++ framework for UI, device integration and future plugin adapters. Its abstractions do not eliminate build, packaging, lifecycle or licensing obligations. [S10, S13-S17] |
| RtAudio / PortAudio / NAudio | Previously evaluated alternatives, not selected dependencies. See the revised comparison below. |
| Visual Audio Lab backend | Application-owned contracts and adapters, hiding library types while preserving meaningful host-specific choices and effective configuration. |

## Revised option evaluation

The earlier evaluation answered a different stack/target brief. Retain the factual comparison, but assess fit against C++20/JUCE/CMake and future plugin capabilities rather than a managed binding.

| Option | Sourced capabilities and strengths | Tradeoffs and revised assessment |
|---|---|---|
| RtAudio | Focused C++ device I/O, ASIO/CoreAudio and other APIs, probing/callbacks, MIT-like licensing. [S7, S8] | Credible lightweight device library, but does not replace JUCE's UI/plugin framework. Adding it would duplicate device lifecycle/packaging responsibilities without an established need. Not selected. |
| PortAudio | C API, callback/blocking I/O, ASIO/CoreAudio and WASAPI host implementations, permissive project license and host-specific extensions. [S1-S4] | Useful focused I/O library; its C API no longer provides the earlier stack-specific integration advantage. It is not a UI or VST3 hosting/export framework. Retaining it alongside JUCE adds a second device backend. Superseded, not selected. |
| NAudio | Official repository describes .NET 9+ processing packages and Windows ASIO/WASAPI packages; portable core is not all Windows-only, but the reviewed matrix does not establish a macOS CoreAudio output package. [S9] | Strong managed Windows tooling; introducing a managed runtime and separate macOS strategy does not fit the revised native stack. Not selected. |
| JUCE | C++ device lifecycle/callbacks and ASIO/CoreAudio [S10, S13], processor abstraction and configurable VST3 hosting [S15, S16], CMake app/plugin targets [S17]. | Selected direction unifies UI/audio/plugin integration without a managed bridge. It is broader than an I/O library, so keep module dependencies narrow and qualify realtime behavior. AGPLv3/commercial licensing [S14], SDK obligations and artifact distribution need review; selection does not imply a subscription requirement or validated performance. |

## Backend abstraction

Define contracts in the locations proposed by the technical architecture: Application owns user-facing audio-control operations; Audio owns host-neutral render/observation contracts; Infrastructure owns the JUCE standalone device adapter. Desktop constructs implementations using explicit C++ ownership/injection. Domain and presentation models must not reference JUCE device handles or host-specific structs.

| Capability | Inputs | Outputs and required behavior |
|---|---|---|
| Enumerate capabilities | Platform/host selection and refresh intent. | Available host APIs, devices, channels, supported/testable configuration choices, and availability/errors. Distinguish an unavailable API from a device with no matching format. |
| Negotiate configuration | Explicit device/host/mode, channels, rate, requested period or latency preference. | Supported/rejected result and effective configuration; report conversions or buffer changes where known. Do not substitute silently. |
| Open and prepare | Validated output configuration and prepared render endpoint. | Owned stream/session, effective format, negotiated callback behavior, latency estimates with provenance, or actionable failure. |
| Start / stop / close | Explicit lifecycle intent and operation identity. | Acknowledged running/stopped/failed/closed state; callback quiescence before callback objects or owned buffers are destroyed. |
| Render boundary | Output buffer, actual frame count, timing/status information where supported. | Fully initialized output for that callback and bounded health/activation signals. No UI work or ordinary application events. |
| Observe health | Stream faults, underrun flags, state changes, and capability refresh outcomes. | Typed off-callback status and diagnostics; unavailable measurements remain unknown rather than fabricated zeros. |

Device identifiers exposed by a library may be transient. Do not persist a process-local index as a durable identity; retain a qualified selection description and resolve it again on open, reporting ambiguity or absence. Hardware preferences are machine-specific; patch meaning remains portable.

Recommended lifecycle: uninitialized, ready, opening, stopped, starting, running, stopping, failed, and closed, with explicit legal transitions. A failed operation does not imply a runnable stream. Start and configuration changes are serialized by the control owner; exact state-machine design remains implementation work.

**Future host boundary:** Keep the render contract independent of device open/start/stop. A future JUCE AudioProcessor adapter receives rate, buses, actual frame count, parameter changes and playback context from an external host. It must work without the desktop window, device manager or editor. Host state/automation, bypass, tails and latency reporting need separate mappings; JUCE capabilities alone do not guarantee sample-offset fidelity. Future VST3 hosting uses its own scan/instance/editor adapter and trust/isolation policy. Plugin export eligibility, format set (VST3 initially considered), packaging, toolchain experience and third-party redistribution remain unsettled; no generated source or exporter is introduced here.

## Platform selection and fallback policy

**Windows primary path:** Present available ASIO drivers and require explicit output selection. Never select an arbitrary first driver. If no compatible driver is available, explain the prerequisite and leave audio stopped; editing, saving, examples and help remain usable. Do not silently substitute WASAPI, install a driver, or claim every built-in Windows output works. Broad consumer-device coverage is an open product decision.

If WASAPI compatibility is separately approved, qualify Shared and/or Exclusive explicitly and disclose availability; do not expose unimplemented modes. Exclusive use would require visible consent and no silent mode/device fallback. JUCE defaults or auto-selection behavior must not override this product policy. [S10, S13]

**macOS:** Use CoreAudio on the selected available output. Negotiate the actual supported rate and period; do not imply that CoreAudio uses Windows-style WASAPI modes. Avoid forced device exclusivity in the baseline.

**Interruption:** On unplug, format change, sleep/wake, or driver failure, stop safely and preserve the editable project. Report what changed. Do not automatically resume through a different physical output or a newly available speaker; require explicit restart/reselection unless a future validated policy says otherwise. Release exclusive/device resources when stopped or closing; foreground/background behavior needs an explicit policy suitable for an audio application, not an accidental UI-focus side effect.

## Threading model

These are execution roles, not a guarantee of one dedicated thread per role.

| Role | Allowed work | Boundaries |
|---|---|---|
| JUCE message/UI context | Input, presentation-model updates, rendering, device/settings choices, visible errors and any platform-required UI-only actions. | No DSP or graph preparation. Keep slow work off this context where APIs permit; platform/driver-affine lifecycle or editor/control-panel calls must be marshalled appropriately without blocking the audio callback. |
| Serialized audio-control context | Stream lifecycle, configuration negotiation, device refresh, fault recovery, ownership of device objects. | Respect JUCE/platform thread-affinity requirements and marshal UI-affine calls explicitly; no assumption that arbitrary worker execution is safe. |
| Preparation/worker context | Validate and prepare graph revisions, allocate/warm resources, load local assets when approved. | Publish only complete, ready state; cancel/reject stale work without changing a newer active revision. |
| Backend audio callback context | Render the requested frames using prepared state; bounded parameter/control consumption and health signaling. | No waiting for UI/workers; do not treat the callback as a general task scheduler. |
| Diagnostics/observation consumer | Drain bounded counters/status, aggregate local logs, throttle visualization updates, dispatch status to UI. | Never make callback progress depend on logging or visualization consumption. |

Use a bounded handoff between control/preparation and processing. Consume only bounded work per callback; coalesce continuous parameter changes where semantics permit. Preserve revision acknowledgements and faults, and provide a stop/fault mechanism that cannot be starved by an ordinary full observation queue.

Stop/close establishes callback completion from the control side before freeing resources. Graph retirement and device destruction occur outside processing after ownership is safely transferred. Do not invoke device lifecycle APIs from the render callback. JUCE device/render callbacks and processor callbacks are distinct adapter boundaries, not ordinary event subscribers.

## Latency goals

These are **proposed engineering targets for qualification**, introduced because this strategy requests measurable goals. They are not existing PRD commitments, library benchmarks, guarantees for all hardware, or grounds to hide unsupported configurations.

### Definitions and measurement boundaries

- **Period duration:** Frame count divided by sample rate. It is a scheduling/buffer interval, not total audible latency.
- **Backend-reported output latency:** The device/library's reported output latency where available. Record its source and units; requested latency is not the negotiated value, and an estimate is not a physical measurement. Inspect the active JUCE device/setup rather than a requested preference. [S10]
- **Measured render-to-output latency:** Delay from a known sample/timestamp in the rendering timeline to the corresponding electrical output event, using a documented clock-alignment/measurement method. State measurement uncertainty and any unaccounted path.
- **Application control delay:** Time from acceptance of a parameter operation to the first render block reflecting it. It excludes pointer input, UI rendering delay, device buffering, and acoustic propagation.
- **Round-trip latency:** Input-through-processing-to-output delay. It is relevant only if input/monitoring is later approved; do not call a one-way output estimate or two nominal periods a measured round trip.

### Proposed qualification targets

| Metric | Proposed target | Conditions and interpretation |
|---|---|---|
| Interactive low-latency profile | Start qualification at 48 kHz, stereo output, 128 frames where supported; also qualify 256 frames. | Nominal periods are approximately 2.67 ms and 5.33 ms respectively. ASIO/CoreAudio may impose different supported sizes. |
| One-way output path, low-latency profile | Aim for measured render-to-output latency at or below 10 ms on designated wired reference configurations. | Primary ASIO/CoreAudio paths; exclude patch-intrinsic delay and state method/uncertainty. A failure is not remedied by relabeling buffer duration as total latency. |
| Control-to-render delay | 99th percentile at or below two effective render periods for lightweight parameter changes after application acceptance. | Excludes graph rebuilds and intentional smoothing, which must be disclosed separately; audible response adds downstream buffering. |
| Render processing headroom | 99th-percentile callback work at or below 50% of its available period, with no observed deadline misses in the qualification run. | A headroom target, not a hard real-time proof. Include actual callback sizes and any buffering adaptation. |
| Sustained stability | No reported output underruns or detected discontinuities during a 30-minute reference-patch run after warm-up. | Record warm-up, workload, test duration, CPU/load, driver version, and health-detection coverage. Zero observed events does not prove faults are impossible. |

Use a simple generation/level/output patch and a representative multi-node patch whose inventory is documented before testing. Test 44.1 kHz as an additional supported-use candidate rather than forcing every device to 48 kHz.

The former shared-mode target is not a gate under the revised primary paths. If a compatibility backend is added, set its own measured profile before claiming support. A future exported plugin must report graph latency separately from the external host's device buffering; standalone physical-output targets do not certify plugin performance.

If a target fails, diagnose application overhead, backend/driver limits, conversion, and workload separately. Increase buffers only with an explicit effective-configuration update; classify that setup as a higher-latency compatibility profile if necessary. Bluetooth, network audio, and unqualified adapters are outside these wired reference goals, not silently counted as passing low-latency configurations.

## Realtime safety rules

Apply bounded-work rules to every JUCE device/processor callback and all DSP code reached from it. The general callback hazards also documented by PortAudio remain useful independent guidance [S11], not a reason to retain that library.

1. Preallocate buffers and graph execution state; prepare and warm processing paths before activation. Do not allocate/free heap objects, resize containers, destroy resources, format strings, or trigger synchronous cleanup in normal callbacks.
2. No filesystem/network I/O, console/file logging, blocking locks, waits, sleeps, task scheduling, UI dispatch, DI resolution, reflection, or arbitrary subscriber/script invocation on the audio path.
3. Keep callback work bounded by actual frame count and the validated graph/workload. A lock-free structure is not enough if draining it takes unbounded time.
4. Define buffer ownership, layout, sample format, valid frame counts, channel mapping, and permitted conversions at the JUCE adapter boundary. Do not retain borrowed device/host buffers beyond their valid lifetime.
5. Keep callback objects and referenced state alive until quiescence. No C++ exception may escape a device or host processing callback; throwing is not the normal fault-reporting path.
6. Prevent failures through validation first. At the final callback boundary, an unexpected failure must produce defined initialized output (normally silence), signal a bounded fault, and trigger visible off-callback failure handling. Silence here is an emergency action, not a success-shaped fallback.
7. Publish only prepared graph revisions; retire old state outside the callback after it is no longer in use. Do not free memory still reachable by native or processing code.
8. Handle underrun and overload explicitly. Do not create a feedback loop of logging, retries, or reconstruction from inside processing.
9. Keep an accessible stop path and deliberate initial levels. Any fades/limiting must have a defined bounded cost and behavior; no safeguard is a guarantee of hearing protection.
10. Treat destructor/refcount timing, page faults, hidden library allocation/locks, OS scheduling, and third-party driver/plugin behavior as residual risks. Native compilation and RAII alone do not establish hard realtime behavior; retirement/destruction must occur off-callback.

If qualification fails, measure graph, adapter, driver and system costs separately before changing scheduling or buffering. Future third-party hosting needs its own admission/isolation/failure design; Visual Audio Lab cannot promise that arbitrary external plugin code obeys these rules.

## Testing strategy

### Contract and deterministic tests

Use a device-free test adapter for tests only; it must never ship as a silent substitute for failed hardware. Cover configuration negotiation, unsupported modes, lifecycle transitions, stale graph revisions, callback frame/layout handling, full handoff queues, non-droppable faults, stop handling, and safe retirement.

Verify that requested/effective values and unknown/estimated/measured latency remain distinct. Test save/reopen of device preferences without persisting transient device indices, and ensure invalid preferences never destroy portable patch content.

### Native build and packaging tests

- Test C++ object/callback ownership, repeated start/stop, failed-open cleanup, shutdown races and runtime-library deployment for each supported OS/CPU/toolchain combination. Use sanitizers or equivalent memory/thread checks separately from timing measurements.
- Inspect the CMake-produced Windows build for explicitly enabled ASIO and the macOS build for CoreAudio. Do not infer availability from source coverage or framework defaults.
- Pin JUCE/CMake/compiler/SDK revisions, retain license notices, review ASIO SDK obligations and vendor-driver prerequisites, and audit transitive dependencies. JUCE licensing and SDK terms must be compatible with the project's open-source/no-subscription policy and future exported artifacts. [S13, S14, S17]
- Install and launch from clean, offline machines with the packaged dependencies; validate macOS signing/notarization and Windows distribution behavior under the eventual packaging policy.

### Hardware qualification matrix

| Platform/path | Required coverage |
|---|---|
| Windows ASIO | A declared reference interface with a vendor-supplied driver; supported rates/buffers, driver control-panel interaction, absent/busy driver, failure and restart. |
| macOS CoreAudio | Built-in and wired USB output candidates on each declared CPU/OS target; supported rates/buffers, device changes, sleep/wake, and interrupted output. |
| All supported paths | Repeated lifecycle operations, unplug while running, unsupported preferences, parameter edits under load, window resizing, local save/load activity, and explicit stop. |

Use the same documented patches and workload profiles across paths. Report differences rather than assuming a common library guarantees parity. Keep output levels controlled during hardware tests.

WASAPI rows become necessary only if that optional path is approved; then include coexistence/exclusive consent and failure tests. Before future plugin releases, separately qualify VST3 discovery/failed scan, state/parameter restore, missing dependencies and editor lifecycle for hosting; for export qualify eligible/rejected patches, packaging/validation, multiple instances, editor-free processing, host-driven rate/layout/block changes, automation, bypass, latency/tails and offline host rendering. Select reference external hosts and isolation policy explicitly rather than claiming universal compatibility.

### Performance and fault evidence

Measure callback execution distributions, frame sizes, actual rates, underrun/fault counts, CPU use, allocation/deallocation/lock behavior, and control-to-render delay under idle and declared UI/I/O stress. Run instrumentation that could perturb timing separately or quantify its cost.

Record backend-reported latency alongside physical measurements. A loopback cable test without clock alignment typically measures a combined output/capture path; label it as such rather than presenting it as isolated one-way output latency. An external measurement rig may be used without adding input/recording as a product feature.

Automated CI can check contracts, packaging, and some integration behavior, but shared/virtual CI machines do not establish hardware latency. Maintain a repeatable physical-machine qualification record for every advertised low-latency profile.

### Adoption and release gates

Before accepting the backend implementation, demonstrate both primary paths through the JUCE/C++ boundary, correct teardown/failure behavior, acceptable measured workload performance, and compatible distribution terms. If a gate fails, report the failing capability and resolve or explicitly revise support rather than claiming generic "JUCE support." No WASAPI gate or plugin release commitment is inferred.

For the PRD's prototype/MVP stages, disclose measured configurations and limits. A stable release must publish its support matrix and qualified profiles; neither a successful callback demo nor the goals in this document constitute evidence that a release is ready.

## Sources

All sources accessed **2026-10-30**. Repository default branches and generated documentation are mutable; recheck the selected release and dependencies before implementation. This strategy does not claim that a default-branch feature is already in a published package.

| ID | Authoritative source | Material evidence |
|---|---|---|
| S1 | [PortAudio official repository](https://github.com/PortAudio/portaudio) | Alternative-library comparison only: portable callback/blocking I/O and host implementations. |
| S2 | [PortAudio WASAPI extension documentation](https://portaudio.com/docs/v19-doxydocs/pa__win__wasapi_8h.html) | Exclusive and host-specific configuration, format and state facilities. |
| S3 | [PortAudio stream information](https://portaudio.com/docs/v19-doxydocs/structPaStreamInfo.html) | Actual rate and best-available latency estimates may differ from requests. |
| S4 | [PortAudio license](https://raw.githubusercontent.com/PortAudio/portaudio/master/LICENSE.txt) | Permissive license and notice conditions. |
| S5 | [Microsoft: Exclusive-Mode Streams](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams) | Shared/exclusive behavior, access restrictions, scheduling, and modern low-period shared capability. |
| S6 | [Apple: Core Audio](https://developer.apple.com/documentation/coreaudio) | Native audio hardware/HAL responsibilities; [accessible documentation text](https://developer.apple.com/documentation/coreaudio.md). |
| S7 | [RtAudio official repository](https://github.com/thestk/rtaudio) | C++ API, host coverage, streams, capability probing, and licensing overview. |
| S8 | [RtAudio official header](https://raw.githubusercontent.com/thestk/rtaudio/master/RtAudio.h) | Stream flags, callback definitions, format/ownership information, and exclusive-use limitations. |
| S9 | [NAudio official repository](https://github.com/naudio/NAudio) and [WASAPI player documentation](https://raw.githubusercontent.com/naudio/NAudio/main/Docs/WasapiPlayer.md) | .NET 9+ repository direction, portable core versus device packages, shared/exclusive/low-latency options. |
| S10 | [JUCE AudioDeviceManager](https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html) | Device lifecycle, callback registration, configuration, and device types. |
| S11 | [PortAudio: Writing a Callback](https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html) | Callback restrictions, unbounded operations, and API-call limitations. |
| S12 | [PortAudio: Windows build documentation](https://portaudio.com/docs/v19-doxydocs/compile_windows.html) | Host inclusion is a build choice; historical tutorial, not current compiler/SDK policy. |
| S13 | [JUCE audio-device module](https://raw.githubusercontent.com/juce-framework/JUCE/master/modules/juce_audio_devices/juce_audio_devices.h) | ASIO/WASAPI configuration, explicit WASAPI modes, CoreAudio dependencies. |
| S14 | [JUCE current repository license](https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md) | AGPLv3/commercial dual-license direction for the reviewed branch; verify pinned-version terms. |
| S15 | [JUCE AudioProcessor](https://docs.juce.com/master/classjuce_1_1AudioProcessor.html) | Hostable/wrappable processor lifecycle, buses and processing contract. |
| S16 | [JUCE processor module](https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_processors_headless/juce_audio_processors_headless.h) | VST3 hosting configuration and processor facilities; verify layout at the pinned revision. |
| S17 | [JUCE CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md) | Native app/plugin build targets and toolchain/configuration requirements. |
