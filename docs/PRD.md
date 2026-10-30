# Visual Audio Lab: Product Requirements Document

## Purpose and source of truth

This PRD translates [ProductVision.md](ProductVision.md) into proposed, testable product requirements for a visual audio programming desktop application. It serves former programmers, hobby musicians, sound designers, and DSP students.

The vision establishes the product principles: visual patching, offline first, Windows and macOS, open source, no subscription, no cloud dependency, and no AI dependency. These principles apply to every release.

Detailed capabilities and release sequencing below are product proposals derived from that intent, not statements about existing functionality or an approved implementation roadmap. No delivery dates, staffing commitments, or numerical performance targets are implied. The later user-directed architecture revision fixes C++20, JUCE and CMake, with primary standalone targets Windows/ASIO and macOS/CoreAudio, and requires support for future VST3 hosting and plugin export. These constraints supersede earlier stack/backend choices without expanding a release's feature scope.

## Assumptions and open decisions

- **A1 - Interactive audio:** The initial experience is assumed to let users hear a patch while adjusting it, rather than only producing audio through offline rendering.
- **A2 - Local projects:** Saving and reopening patches as local projects is assumed to be essential to meaningful offline use.
- **A3 - Initial component set:** A small set of sound-generation, sound-shaping, level-control, and output components is proposed. Exact components and connection types require product and DSP design.
- **A4 - MVP boundary:** Release 0.5 is proposed as the MVP; Release 0.1 is an exploratory prototype. Neither is presented as production-ready.
- **A5 - Compatibility:** Primary standalone audio paths are Windows/ASIO and macOS/CoreAudio. Supported OS versions, CPU architectures, hardware, drivers and distribution requirements remain to be defined before a release claims support. Windows audible use requires a compatible installed ASIO driver; universal built-in-output support is not promised. WASAPI is only a possible future compatibility option.
- **A6 - Quality budgets:** Latency, resource use, patch-size limits, and reliability acceptance thresholds must be established through representative workloads. This PRD does not invent numerical targets.
- **A7 - Open-source details:** The project license and contribution process remain open decisions; open-source availability itself is a fixed principle. Resolve compatibility with the pinned JUCE/SDK terms and future exported artifacts before distribution; do not assume open source permits bundling third-party plugins.

## Personas

These are needs-based personas, not validated research findings or separate product tiers. A person may fit more than one.

| Persona | Motivation and context | Primary need | Potential barrier |
|---|---|---|---|
| Returning builder | A former programmer wants the direct, exploratory feel of classic development tools applied to audio. | Understand components and connections, experiment, and preserve working ideas without a code-first setup. | Unfamiliar DSP terminology or a complex modern toolchain. |
| Musical experimenter | A hobby musician wants to explore sounds as part of a creative practice. | Get from a blank patch or example to audible results and adjust them directly. | Too much technical setup before hearing a result. |
| Sound explorer | A sound designer wants to inspect and shape audio behavior through visual structures. | Follow signal flow, compare adjustments, and reuse useful work. | Unclear routing or a component set too limited for exploration. |
| DSP learner | A DSP student wants to connect concepts to observable and audible behavior. | Inspect signal flow, change one parameter at a time, and relate results to explanations. | Hidden behavior, unclear units, or examples that assume prior knowledge. |

## User stories

| ID | Story | Evidence of success |
|---|---|---|
| US-01 | As a returning builder, I want to add and connect components so I can construct an audio idea visually. | I can build a valid path to audio output without writing code. |
| US-02 | As a hobby musician, I want to open a simple example so I can hear and modify something without starting from scratch. | A bundled example works offline and its controls produce understandable changes. |
| US-03 | As a sound designer, I want to follow connections and edit parameters so I can understand what shapes a sound. | Component roles, routing, values, and units are visible where relevant. |
| US-04 | As a DSP student, I want concise component explanations so I can relate patch behavior to signal-processing concepts. | I can access relevant explanations without an internet connection. |
| US-05 | As any user, I want to save and reopen my work locally so I can continue later without a service account. | The supported patch structure and parameter values are restored. |
| US-06 | As any user, I want clear feedback about an invalid connection or audio problem so I can recover. | The application explains the issue and offers an actionable next step without silently discarding work. |
| US-07 | As any user, I want an obvious way to stop sound so I remain in control while experimenting. | I can stop output immediately through a clearly available control. |
| US-08 | As a Windows or macOS user, I want the core patching workflow on my platform so I am not a secondary audience. | I can create, hear, save, and reopen a supported patch on either supported platform. |

## Functional requirements

The baseline column identifies the first proposed release where the requirement must be met. Later releases retain earlier requirements. Prototype limitations must be disclosed.

| ID | Requirement and acceptance condition | Baseline |
|---|---|---|
| FR-01 | Provide a visual patch workspace. Users can add, select, move, connect, disconnect, and remove available components; visible connections reflect actual routing. | 0.1 |
| FR-02 | Provide a minimal component set sufficient to generate, shape, control the level of, and output a sound. At least one complete example demonstrates these roles; the exact inventory is an open design decision. | 0.1 |
| FR-03 | Make supported parameters directly editable with meaningful names, current values, units where relevant, and defined valid ranges. Supported edits affect the audible result during use, subject to A1. | 0.1 |
| FR-04 | Provide explicit audio start and stop controls and a visible output state. Users can stop sound without deleting the patch. | 0.1 |
| FR-05 | Save and reopen local projects, preserving supported components, connections, and values. Report load/save failures; do not silently overwrite an unreadable or unsupported project. | 0.1 |
| FR-06 | Expose the available audio output choices and report unavailable output or audio-start failures with recovery guidance. The user can distinguish a stopped patch from a device problem. | 0.5 |
| FR-07 | Identify incompatible or otherwise unsupported connections and explain why they cannot be used. Invalid routing must not appear to be a working connection. | 0.5 |
| FR-08 | Provide bundled examples and local getting-started guidance covering creation, connection, parameter changes, audio control, and saving. No online documentation is required for this introductory path. | 0.5 |
| FR-09 | Provide local component descriptions that explain purpose, inputs, outputs, and parameters sufficiently to follow the bundled examples. | 0.5 |
| FR-10 | Support undo/redo for ordinary patch edits and warn before discarding unsaved changes. Errors must leave the user able to preserve unaffected work. | 0.5 |
| FR-11 | Permit a project created with the same supported release and component set to move between Windows and macOS without losing its patch structure or parameter values. Report device-specific settings that need reassignment. | 0.5 |
| FR-12 | Detect unsupported project versions and document migration or compatibility limits. Before changing an existing project format, provide a way to retain the original rather than silently making it unusable. | 1.0 |
| FR-13 | Provide an understandable way to organize and reuse related patch structures as projects become more complex. The specific interaction is a proposed 2.0 discovery outcome, distinct from future VST3 hosting, plugin export, or VAL Script; none of those is assigned to 2.0 by this requirement. | 2.0, proposed |

## Non-functional requirements

| ID | Requirement | Acceptance approach |
|---|---|---|
| NFR-01 | **Offline independence:** Core creation, editing, audio use, saving, reopening, examples, and introductory help work without network access, service authentication, or AI. | Exercise these workflows with networking disabled on both supported platforms. |
| NFR-02 | **Cross-platform quality:** Windows and macOS support the same core workflow. Platform-specific differences and supported configurations are explicit. | Run the same workflow checklist on the published support matrix; investigate functional differences rather than assuming parity. |
| NFR-03 | **Responsive audio and editing:** Supported patches remain usable while playing and editing. Define and disclose workload limits rather than implying unlimited capacity. | Before an external release, establish a representative patch/device matrix and measurable responsiveness, latency, and resource budgets; record results against those budgets. |
| NFR-04 | **Reliability and data integrity:** Supported saves round-trip correctly; failures are visible and do not silently destroy existing work. | Test save/load round trips, unavailable destinations, invalid files, and device interruption; document recovery behavior. |
| NFR-05 | **Approachability:** The introductory path does not require code or unexplained DSP knowledge. | Observe representative users attempting the bundled introductory tasks and address blocking comprehension issues. |
| NFR-06 | **Accessible interaction:** Essential controls have clear labels, status is not conveyed by color alone, and core workflows should be operable by keyboard. | Evaluate the core workflow with keyboard navigation and platform accessibility facilities; record remaining barriers before claiming accessibility support. |
| NFR-07 | **Controlled audio experimentation:** Starting or changing a patch must not unexpectedly remove the user's ability to stop output. Initial levels and documented parameter ranges should reduce avoidable exposure to loud output, without claiming guaranteed hearing protection. | Review initial output behavior and exercise stop control under invalid and demanding patch conditions; provide concise listening-level guidance. |
| NFR-08 | **User independence and transparency:** Releases are available as open-source software without subscription gating or mandatory cloud/AI services. | Review distribution, license availability, and runtime prerequisites for each release. |

Release 0.1 evaluates these quality requirements in a disclosed prototype context. Release 0.5 must establish evidence for the MVP workflow; Release 1.0 must satisfy documented quality gates across its declared support matrix. Unresolved budgets or blocking quality failures prevent an unqualified stable-release claim.

## MVP scope

**Release 0.5 is the proposed MVP:** a usable end-to-end loop of opening or creating a patch, connecting a small component set, hearing and adjusting it, stopping output, saving locally, and reopening it on Windows and macOS.

Included: FR-01 through FR-11, bundled examples and local explanations, and evaluation against the non-functional requirements above. The MVP validates whether the same visual workflow is useful across the four audiences; it does not attempt exhaustive musical production or DSP coverage.

Excluded from the MVP: a full digital audio workstation, multitrack arrangement, notation, comprehensive synthesis/effects libraries, VST3 hosting, plugin export, a custom-code environment, live collaboration, and a marketplace. These exclusions are feature-scope boundaries. Supporting future hosting/export architecturally is required now; delivering their user-facing workflows is not part of the MVP. VAL Script remains a requested design with no named release placement.

## Release definitions

Release numbers below describe proposed product maturity and outcomes, not dates. Scope must be reviewed against user evidence and technical feasibility before implementation is committed. Every release follows the revised stack and primary paths; VST3 hosting and plugin export remain future capabilities with no assignment to 0.1, 0.5, 1.0 or 2.0. Architecture validation should prevent device/UI coupling from blocking them without requiring their implementation at an early release gate.

### Release 0.1 - Exploratory audio patch prototype

**Purpose:** Test the foundational interaction: can users connect visible components and understand an audible result?

**Scope:** FR-01 through FR-05, a minimal demonstrable component set, and one bundled end-to-end example. Provide a locally usable prototype for both Windows and macOS; disclose its supported configurations and known limitations.

**Exit evidence:** On each declared prototype platform, a user can build or modify a simple patch, hear it, stop it, save it, and reopen it offline. Capture comprehension and usability findings for the MVP. Do not describe this release as production-ready.

### Release 0.5 - MVP beta

**Purpose:** Validate a complete independent creative and learning workflow with the target audiences.

**Scope:** All MVP requirements, FR-01 through FR-11, including actionable errors, local learning material, reversible editing, and cross-platform project portability.

**Exit evidence:** Representative users can complete the MVP loop; the team has documented support configurations, workload budgets, quality results, and remaining beta limitations. Blocking data-loss, audio-control, or core-workflow issues are resolved before broad beta use.

### Release 1.0 - Stable foundational product

**Purpose:** Make the proven MVP dependable and understandable for regular use.

**Scope:** Retain the MVP and add FR-12. Concentrate on reliability, usability, local documentation, compatibility handling, and consistent behavior on the declared Windows and macOS support matrix, rather than adding unrelated production features.

**Exit evidence:** The core workflow passes documented quality gates on both platforms. Project compatibility and recovery limits are explicit, open-source distribution details are settled, and known issues do not block the promised core workflow. Stable does not mean unlimited patch complexity or universal device support.

### Release 2.0 - Deeper visual exploration

**Purpose:** Extend creative and educational depth without sacrificing approachability or independence.

**Proposed scope:** Retain the stable foundation; improve organization and reuse of larger patches under FR-13, and selectively deepen the component set and learning examples based on demonstrated audience needs. Component expansion and reuse design require discovery before becoming committed scope.

**Exit evidence:** Representative users can build and understand richer patches without losing the introductory workflow. New capabilities meet the established offline, cross-platform, compatibility, and quality requirements. The major version number alone does not authorize breaking project compatibility.

## Future scope

Beyond the baseline, discovery may consider richer DSP components, signal-inspection aids for learning, reusable patch structures, and locally sharing projects and examples. Organization and reuse are candidates for the proposed 2.0 direction; other candidates are not assigned a release.

Future VST3 hosting and plugin export are required architectural capabilities under the revision. Hosting runs third-party processors in Visual Audio Lab; export packages eligible Visual Audio Lab processing for an external host. Their user workflows, compatibility/support matrices, export formats (VST3 is the initial candidate), eligibility rules, licensing, toolchain/distribution experience and release placement remain open. No arbitrary-patch export, third-party plugin redistribution, nested hosting, or universal host compatibility is promised.

Audio-file import/export, recording and MIDI remain discovery questions, not established product requirements. Audio-file export is distinct from plugin export. Each delivered expansion needs an explicit scope/acceptance decision; the new architecture direction does not authorize DAW scope.

No future scope may require a subscription, cloud service, or AI service for the core experience. Optional expansion must not undermine the offline visual workflow.

## Constraints

- Visual patching remains the primary experience; text-based programming cannot become a prerequisite for the core workflow.
- Windows and macOS are the named target platforms. Other operating systems are outside the current support commitment.
- C++20, JUCE and CMake are the selected architecture direction. Primary standalone audio targets are Windows using ASIO and macOS using CoreAudio; no drivers are implemented. WASAPI is not a required primary target or a promised fallback.
- Keep processing reusable without a physical device or editor for future VST3 hosting and plugin export. The standalone app owns device access; an exported plugin uses the external host's buffers/lifecycle. See [TechnicalArchitecture.md](TechnicalArchitecture.md) and [AudioPlatformStrategy.md](AudioPlatformStrategy.md) for boundaries and qualification.
- Core use must be offline-first and independent of cloud services and AI services.
- The product is open source and has no subscription requirement.
- Turbo Pascal, Visual Basic, LabVIEW, Pure Data, and modular audio systems are inspirations, not commitments to emulate their interfaces, APIs, file formats, or compatibility.
- Exact JUCE/CMake/compiler/SDK versions, detailed DSP implementation, project licensing choice, hardware/OS minimums, staffing, release dates, and numerical acceptance budgets remain separate decisions. The fixed stack and primary targets above are no longer open choices.

## Risks

| Risk | Product impact | Mitigation or validation |
|---|---|---|
| Four audiences have conflicting expectations. | A broad feature set may serve none well. | Validate the shared patching loop with each audience before broadening scope. |
| Visual programming hides rather than explains behavior. | Users cannot predict sounds or learn DSP concepts. | Validate labels, routing visibility, component explanations, and examples with representative users. |
| Audio performance varies by device and platform. | Glitches or setup friction undermine trust. | Define the support matrix and workload budgets, then exercise real configurations on both platforms. |
| ASIO is unavailable on a Windows user's machine. | The user cannot hear a patch despite being able to edit/save it. | State the installed-driver prerequisite, expose actionable stopped/unavailable state, and evaluate compatibility paths separately rather than silently promising WASAPI fallback. |
| Future native plugins introduce crashes, unsafe behavior or incompatible state/licensing. | Hosted code can compromise reliability; exports may fail in hosts or be undistributable. | Preserve device-independent adapters now; decide scanning/processing isolation, parameter/state/latency contracts, export eligibility and redistribution policy before shipping plugin features. |
| Experimental routing or levels create unexpectedly loud output. | Users may experience discomfort or hearing risk. | Evaluate level behavior, retain an obvious stop control, and document listening precautions; do not present software safeguards as guaranteed protection. |
| Save errors or format changes lose work. | Users cannot trust local project ownership. | Validate round trips and failure paths, preserve originals during migration, and document compatibility limits. |
| Dependency or distribution choices conflict with open-source independence. | The product cannot meet its foundational promises. | Review licenses and runtime prerequisites before adopting dependencies or distributing releases. |
| Scope expands toward a DAW or broad interoperability platform. | The approachable MVP is delayed or obscured. | Protect the MVP exclusions; require evidence and explicit decisions for additions. |
| Release labels are mistaken for delivery commitments. | Expectations exceed validated scope and capacity. | Keep proposed scope, open decisions, quality gates, and actual delivery planning distinct. |
