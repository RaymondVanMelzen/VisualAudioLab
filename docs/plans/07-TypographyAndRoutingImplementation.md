# Typography and routing implementation

## Delivery boundary

This implements the font foundation and the first executable routing layer of
`TypographyAndFontSystem.md` and `RoutingModelRedesign.md`. It does **not** claim
that either target specification is complete. A single safe reviewable PR is
preferable to replacing persistence, device binding, DSP scheduling and canvas
interaction simultaneously.

The existing graph has one structural input/output per node, one global channel
width, and one main hardware output. The runtime previously rejected every
disconnected node and effectively limited execution to a single chain. The
first layer removes that restriction without inventing independent device buses.

## Decisions grounded in the code

| Area | Decision |
|---|---|
| Endpoint identity/lifetime | Keep document-local stable GraphIds and existing Input/Output instances in this layer. Do not silently replace/delete legacy instances or change their IDs. Fixed system-owned rails require the next schema/UI layer. |
| Supported endpoints | Selected hardware input, the seven existing internal generators, zero-input scripts and the main hardware output. Application Capture, File Player, Virtual Audio Device, Recorder, Streaming and plugins are deferred. |
| Machine binding | Device tokens are inventory-scoped; project files must not persist them. Existing machine-local settings retain explicit backend/device names and selected driver indices. Discovery/inspection errors remain visible; Start never falls back. |
| Channel identity | AudioBackends already opens explicit BigInteger masks and rejects changed active masks. RuntimePanel exposes named contiguous groups. Keep these exact indices; do not replace them with first-N selection. |
| Layouts | Existing DSP is prepared for one shared width (1-32 channels); input is disabled or matches output. Scripts enforce their declared dimensions. This layer keeps these restrictions explicit. Mono-to-stereo conversion, arbitrary channel maps and independent multi-bus processors need visible adapters and are deferred, not fabricated. |
| Execution | Validate/prepare the entire bounded DAG, but execute only nodes upstream of the main Output. Disconnected nodes are legal/inert; direct Input-to-Output and fan-out are supported. Add a visible Sum utility as the only port that accepts multiple upstream cables, adding corresponding channels with unity coefficients. No implicit summing at Output/effects, no automatic normalization/limiter. |
| Realtime | Keep AudioEngine's immutable publication/retirement protocol. Resolve upstream offsets and allocate all scratch off-thread. Callback work is bounded by the existing node/edge/storage budgets; no locks, allocation, ownership changes, logging or UI. |
| Persistence | Remain on version 1: Sum uses the existing explicit audio.type scalar and connection format, guarded by required feature audio.explicit-sum.v1 so older readers refuse unfamiliar semantics. Old graphs retain exact IDs, edges, parameters and unknown fields. Teach shared validation to accept fan-in only at Sum; no destructive migration or fake version-2 multi-bus format. |
| Canvas | Preserve palette/glow, selection and keyboard editing. Expose Sum in the catalog and its fan-in meaning on the node. Apply legible semantic text tiers to current nodes. Fixed endpoint zones, readiness badges and port-indexed cables are deferred together with their model. |
| Typography | Bundle official Inter variable and JetBrains Mono faces with OFL notices; embed resources, not machine font dependencies. Shared helpers own role sizes, fallbacks and numeric formatting. Theme owns default component/menu/dialog fonts; code editors explicitly use Mono. Numeric Inter must enable tnum/zero using verified JUCE APIs; if unsupported, report it rather than claim support. |
| Accessibility | Enforce readable text tiers at zoom and preserve logical DPI sizing and keyboard behavior. Do not claim OS text-size/high-contrast integration or a complete reflowing 200% preference until implemented. Existing future analysis/editor features in the spec are not invented. |

## Implementation order

1. Verify official downloadable font files/licenses, embed them, add shared font
   roles and number/unit formatting, theme defaults and code-editor application.
2. Update graph/editor/project validation together; implement explicit Sum,
   topological preparation of reachable branches and inert disconnected nodes.
   Keep one explicit main output and matching channel width.
3. Add focused graph, persistence, DSP allocation and typography/render tests.
   Exercise direct routes, multiple roots, fan-out, Sum, dead branches, cycles,
   rejected dimensions and old/new version-1 round trips.
4. Build Windows Release; run non-device suites, inspect representative
   offscreen-rendered UI at zoom/scales, save screenshots, create an implementation
   PR stacked on #21. Preserve the previous persistent Windows bundle, install
   exact-head app/helper/runtime/resources/notices and verify all checksums.

## Required follow-on dependency order

1. Version-2 typed port/bus/channel-layout schema, endpoint descriptors/readiness,
   machine-binding resolver and explicit lossless version-1 migration.
2. Per-port scratch/execution and truthful processor layout declarations; tested
   visible mono/stereo/channel-map adapters, multiple disjoint output buses and
   designated sum-port indices. No generalized resampling promise.
3. Fixed source/destination rails, system-owned lifetime, accessible readiness,
   insert/rewire gestures and endpoint inspector, integrated with explicit Start.
4. Full typography accessibility settings/reflow, analysis Glance mode and the
   richer syntax/diagnostic editor described by the target document.

Windows validation is not macOS qualification. Offline rendering/tests are not
physical audio, multi-monitor DPI transition, screen-reader or long-session
fatigue qualification. Record actual results and remaining gaps with delivery.

## Verification recorded for this layer

- Windows x64 Release: clean all-target build, followed by rebuilt final
  readout/evidence changes. All 21 non-device CTest suites pass, including audio,
  application graph edits, persistence, backend contracts and presentation.
  `Audio.NativeDiscovery` was deliberately excluded; no device was probed/opened.
- Typography tests load every embedded face, check actual tabular/monospace
  advances, feature settings, formatting thresholds, stable unit placement and
  semantic zoom limits. Windows software-rendered shell snapshots at 100%, 125%,
  150% and 200%, a dialog, script editor and canvas zoom tiers were captured.
  Representative shell/dialog/editor/zoom captures were inspected.
- Audio tests exercise multi-root Sum/fan-out, direct routes, inert branches,
  duplicate/cycle/dimension rejection, capture inactivity and callback allocation
  guards. Existing device-mask contract tests remain passing. There are no
  multi-destination/adapter/readiness tests because those features are not shipped.
- One project-UI test timed out during the first clean regression run; it passed
  three consecutive isolated reruns and both subsequent complete non-device runs.
  No timeout increase or speculative production workaround was introduced.
- Not qualified: physical audio, macOS, mixed-monitor DPI transitions, screen
  readers, OS text-size/high-contrast preferences or the deferred architecture.
