# Visual Audio Lab: VAL Script Architecture

## Status and purpose

VAL Script is a proposed Pascal/Delphi-inspired language for defining DSP graph nodes. This document specifies language and compilation architecture only. Grammar and examples are specification material, not a compiler implementation, generated code, or files to execute.

The requested capability refines the previously unresolved scripting direction in [FunctionalArchitecture.md](FunctionalArchitecture.md) and [TechnicalArchitecture.md](TechnicalArchitecture.md). Unlike general project-automation scripting, VAL Script describes bounded node computation. It does not expose application commands or arbitrary host APIs.

Use the graph, parameter, rate, revision, and realtime rules in [DspArchitecture.md](DspArchitecture.md), the platform boundaries in [AudioPlatformStrategy.md](AudioPlatformStrategy.md), and the independent visual workflow in [ProductVision.md](ProductVision.md) and [PRD.md](PRD.md). VAL Script is now a requested language design, not an automatic addition to the MVP or a named release. Visual patching must remain usable without it.

The host application follows the revised C++20/JUCE/CMake direction, with primary standalone Windows/ASIO and macOS/CoreAudio paths. The language itself remains independent of UI, audio-device APIs and native plugin formats. Future VST3 hosting and plugin export are separate architectural capabilities, not permission for scripts to load native plugins or a commitment to export script-containing patches.

## Design principles and proposed scope

- Familiar named declarations, typed variables/constants, procedures/functions, and readable structured statements.
- Support `IF THEN ELSE` and a deliberately hybrid `FOR ... TO ... DO ... NEXT` construct. This is inspired by Pascal/Delphi, not source compatibility with either language; `NEXT` is an explicit VAL Script choice.
- Compile locally, outside audio processing, into validated DSP node definitions and prepared execution plans.
- Make processing cost and storage statically bounded; reject unsupported programs rather than executing an unrestricted interpreter on the callback.
- No file access, networking, threads, or dynamic allocation in the language. Also exclude recursion, pointers, reflection, imports, foreign calls, classes, exceptions, and unbounded loops.
- Start with stateless scalar kernels. Persistent state, arrays, user-defined types, delay memory, and access to sample clocks are unresolved extensions, not silently available features.

## Language execution semantics

### Node interface and rates

A source unit declares one named node at `AUDIO` or `CONTROL` rate, followed by typed inputs, outputs, parameters, and its body.

An audio-rate kernel evaluates once for each sample frame, reading that frame's scalar inputs and effective parameter values and assigning every output. A control-rate kernel evaluates once at each prepared control tick and holds its outputs until the next tick.

For the initial design, audio ports contain scalar `REAL` samples; control ports contain scalar `REAL` values. Booleans and integers are internal computation types. Multichannel audio is expressed as separately named scalar ports; implicit channel expansion and rate conversion are forbidden. Vector/array ports are future decisions.

An audio kernel receives audio inputs; a control kernel receives control inputs. Cross-rate connections use the DSP graph's explicit supported adapters. A script cannot choose its own backend period, create threads, or bypass the graph scheduler.

`PARAM` declarations create node parameters, not input ports. Their base values, automation/override precedence, and smoothing follow the DSP parameter system. Audio kernels see the effective value for the current sample; control kernels see its value at the tick, with control-rate quantization disclosed.

### Types, values, and arithmetic

| Type | Proposed meaning |
|---|---|
| `REAL` | Finite floating-point scalar aligned with the selected DSP sample precision; initially recommend 32-bit. Exact rounding/precision must be pinned in a language version. |
| `INTEGER` | Signed 32-bit integer with checked arithmetic; overflow is an error, never implicit wraparound. |
| `BOOLEAN` | `TRUE` or `FALSE`; no implicit numeric conversion. |

Identifiers and keywords are case-insensitive, using ASCII letters, digits, and underscore; identifiers start with a letter. Names retain authored spelling for diagnostics. String values and Unicode identifiers are outside the initial grammar.

Numeric literals are decimal integers or decimals with a dot; a leading sign is an operator, not part of the token. No exponent notation is proposed initially. Integers can widen to `REAL` when required, with precision loss disclosed; conversion from `REAL` to `INTEGER` is not implicit.

`+`, `-`, and `*` operate on numeric operands; `/` yields `REAL`. `DIV` and `MOD` require integers, with quotient truncated toward zero and remainder having the dividend's sign. Division by zero, integer overflow, and non-finite numeric results are runtime faults unless diagnosed at compile time. No silent clipping or saturation is implied.

Relational operators yield booleans. Equality supports compatible numeric operands or two booleans; ordering comparisons require numeric operands. `NOT`, `AND`, and `OR` require booleans. `AND`/`OR` short-circuit left-to-right, and ordinary operand and argument evaluation is left-to-right.

### Variables, constants, and assignment

`CONST` expressions use literals, earlier constants, and permitted operators only. They are evaluated and checked at compilation; they cannot read inputs, parameters, variables, or call routines.

`VAR` declarations allocate fixed scratch slots during node preparation. Variables do not persist between samples/ticks. Reading a variable before definite assignment is a compile error; declaration does not silently initialize it.

Inputs, parameters, constants, and value arguments are read-only. Outputs and variables are writable. Every output must be assigned on every reachable path before the kernel ends, and reading an output before assignment is invalid. Output values are not previous-sample memory.

Assignments evaluate the right-hand expression before changing the target. For a `REAL` parameter, `RANGE` defines an inclusive finite domain containing its default; user/automation targets outside that domain are rejected through the existing parameter rules.

### Procedures and functions

Routines are top-level declarations after node variables; nested routines, overloads, forward declarations, and recursion are not supported. A routine may call only earlier declared routines, making the call graph acyclic.

Arguments are passed by value. `VAR`/reference arguments are not part of this proposal.

- A **procedure** has no return value. It may read node inputs/parameters/constants and definitely assigned node-level scratch/output values, and write node-level scratch variables or outputs. Its read requirements and write effects are explicit in semantic analysis.
- A **function** returns one declared scalar type by assigning to its own name, following familiar Pascal-style notation. Its result must be definitely assigned on every path. It may read constants, inputs, parameters, and its value arguments, but may write only its own locals and result. It cannot call a procedure or read mutable node-level scratch/output state.

Node constants/input/parameter names are visible to routines. Caller locals are not. Routine-local names may not shadow visible names; duplicate or ambiguous declarations are compile errors. The function's own name is a write-only result target, not a recursive call mechanism or an uninitialized accumulator.

Procedure effects participate in caller definite-assignment analysis. Every node-level value read before being assigned by a procedure must already be assigned at the call site. A call assigning a node-level scratch value must establish that assignment on all of the procedure's paths before the caller can rely on it.

### IF THEN ELSE

The condition must be boolean. Execute only the selected branch. `ELSE` associates with the nearest unmatched `IF`; use `BEGIN ... END` to make grouping explicit.

A semicolon separates statements; it must not appear between a `THEN` branch and its `ELSE`. Both branches are considered for definite assignment and worst-case cost. An omitted `ELSE` is legal but does not establish an assignment on the skipped path.

### FOR NEXT

Use inclusive ascending bounds: `FOR i := lower TO upper DO statement NEXT i`.

Both bounds must be compile-time `INTEGER` constant expressions. The counter must be a declared integer variable; the `NEXT` name must match it. Step size is one. If lower exceeds upper, the body executes zero times.

The counter is read-only within the loop body, including through called procedures. Nested loops must use different counters; aliases cannot circumvent the rule. Outside the loop the counter has no defined value until explicitly assigned again.

Compute the trip count in a sufficiently wide compile-time domain and reject it if it exceeds the qualified loop limit or expanded operation/storage budget. Loop completion must not overflow merely because the inclusive upper bound is the largest integer; lowering does not depend on overflowing an increment.

No `WHILE`, `REPEAT`, downward/dynamic bounds, `BREAK`, `CONTINUE`, or `GOTO` are proposed. A constant bound makes termination provable but does not by itself make a program cheap enough for audio.

## Illustrative grammar

This EBNF is a proposed normative syntax sketch, not parser source. Quoted words/symbols are terminals; braces mean repetition, brackets mean optional content, and `|` means alternatives. Whitespace separates tokens. `//` comments run to line end; other comment forms are not included.

```text
unit          = "NODE" identifier "AT" rate ";",
                { inputDecl }, { outputDecl }, { paramDecl },
                [ constSection ], [ varSection ], { routineDecl },
                compound, "." ;
rate          = "AUDIO" | "CONTROL" ;
inputDecl     = "INPUT" identifier ":" "REAL" ";" ;
outputDecl    = "OUTPUT" identifier ":" "REAL" ";" ;
paramDecl     = "PARAM" identifier ":" "REAL" "=" constantExpr
                "RANGE" constantExpr ".." constantExpr ";" ;
constSection  = "CONST" constDecl { constDecl } ;
constDecl     = identifier "=" constantExpr ";" ;
varSection    = "VAR" varDecl { varDecl } ;
varDecl       = nameList ":" scalarType ";" ;
nameList      = identifier { "," identifier } ;
scalarType    = "REAL" | "INTEGER" | "BOOLEAN" ;
routineDecl   = procedureDecl | functionDecl ;
procedureDecl = "PROCEDURE" identifier "(" [ formals ] ")" ";",
                [ varSection ], compound, ";" ;
functionDecl  = "FUNCTION" identifier "(" [ formals ] ")" ":"
                scalarType ";", [ varSection ], compound, ";" ;
formals       = formalGroup { ";" formalGroup } ;
formalGroup   = nameList ":" scalarType ;
compound      = "BEGIN" [ statement { ";" statement } [ ";" ] ] "END" ;
statement     = assignment | procedureCall | compound | ifStmt | forStmt ;
assignment    = identifier ":=" expression ;
procedureCall = identifier "(" [ arguments ] ")" ;
ifStmt        = "IF" expression "THEN" statement [ "ELSE" statement ] ;
forStmt       = "FOR" identifier ":=" constantExpr "TO" constantExpr
                "DO" statement "NEXT" identifier ;
arguments     = expression { "," expression } ;
expression    = orExpr ;
orExpr        = andExpr { "OR" andExpr } ;
andExpr       = relation { "AND" relation } ;
relation      = sum [ relOp sum ] ;
relOp         = "=" | "<>" | "<" | "<=" | ">" | ">=" ;
sum           = product { ( "+" | "-" ) product } ;
product       = unary { ( "*" | "/" | "DIV" | "MOD" ) unary } ;
unary         = ( "+" | "-" | "NOT" ) unary | primary ;
primary       = literal | identifier
              | identifier "(" [ arguments ] ")"
              | "(" expression ")" ;
constantExpr  = expression ;
literal       = integerLiteral | realLiteral | "TRUE" | "FALSE" ;
```

`constantExpr` uses the expression syntax but has the semantic restrictions defined above. Declarations are processed in source order: parameter defaults/ranges cannot refer forward to constants declared later in the unit. Formal types, writable targets, routine visibility, output assignment, rate compatibility, and resource limits are semantic checks, not properties of EBNF alone.

The lexer recognizes `:=`, `..`, `<=`, `>=`, and `<>` before their single-character prefixes. A decimal literal requires digits on both sides of its dot, so the range delimiter cannot be swallowed into a number. The parser distinguishes a call from an identifier by the following parentheses. A signed minimum integer literal must be checked as a complete signed constant expression rather than rejected due to an intermediate positive-token range check.

At least one output is required semantically; a node may have no inputs. Source length, nesting depth, declaration count, and literal limits must be set before implementation.

## Parser architecture

Use a staged, local compiler front end:

1. **Lexing:** Produce tokens with source spans; recognize reserved words, names, literals, comments, and operators. Invalid characters and unsupported syntax produce diagnostics.
2. **Parsing:** A hand-written recursive-descent statement/declaration parser with precedence-based expression parsing is recommended for the small grammar. No parser library is selected.
3. **Recovery:** Recover at appropriate declaration/statement boundaries to report useful additional errors, but never compile a recovered malformed unit as valid.
4. **Semantic analysis:** Build scoped symbol tables, bind identifiers/calls, type-check, evaluate constants, compute routine effects, prove assignment and boundedness, and produce a typed bound representation.

All stages run outside the callback with cancellation and finite compilation budgets. Source-aware diagnostics include the location, reason, and affected declaration; a parser error must not be reduced to a generic audio-start failure.

## AST architecture

The AST preserves authored structure and source spans; it is not the runtime DSP graph.

| AST family | Representative elements | Responsibility |
|---|---|---|
| Unit/interface | Node name/rate, input/output/parameter declarations. | Describe the proposed node's public boundary. |
| Declarations | Constant, variable, formal argument, procedure, function. | Preserve names, types, initial constant expressions, and bodies. |
| Statements | Assignment, compound, conditional, bounded loop, procedure call. | Preserve evaluation/control structure without assuming both branches execute. |
| Expressions | Literal, name, unary, binary, function call. | Preserve operations and source evaluation order. |
| Bound annotations | Symbol/type bindings, constant values, routine effects, assignment facts, cost findings. | Provide verified meaning after semantic analysis; may reside in a separate bound tree rather than mutating the source AST. |

Do not embed JUCE components, native device handles, filesystem objects, or arbitrary C++ host objects in language nodes. Keep source AST, typed intermediate representation, and DSP execution plan separate.

## Compiler and lowering into DSP graph nodes

### Pipeline and artifact

Conceptually: source text -> tokens -> AST -> typed/bound representation -> bounded DSP operation graph -> validated node definition -> prepared graph plan.

The output is a versioned **script-defined DSP node artifact**, containing its interface, verified operation/control plan, constant/fixed scratch requirements, source mapping, language/compiler identity, rate, and cost findings. The exact persisted representation remains undecided.

This is not generated C++, dynamic native-library loading, a system compiler invocation, or permission to execute general machine code. The local compiler can be implemented in C++20 without exposing C++ to scripts. A prepared operation-plan executor is the initial recommendation; its dispatch/memory layout must be qualified. Alternative compiled execution approaches require a separate design and must preserve these restrictions. Future native plugin packaging does not imply that VAL Script compilation generates native source.

### Lowering rules

| Language construct | DSP lowering meaning |
|---|---|
| Input / parameter read | Typed access to the current sample/tick input or effective parameter supplied by the existing runtime. |
| Constant | Prepared immutable scalar, with checked constant folding where valid. |
| Variable / assignment | Fixed scratch slots or single-assignment values; no runtime heap allocation. |
| Procedure call | Bounded expansion/inlining of the verified body, with explicit scratch/output effects and value arguments evaluated first. |
| Function call/result | Bounded pure computation and one verified result; inline or use a preplanned acyclic internal call sequence. |
| IF THEN ELSE | Structured conditional subplans and merged assigned values. Only the selected branch executes. |
| FOR NEXT | Compile-time bounded repetition expanded within the approved plan budget; reject programs too large to expand. |
| Output assignment | Final scalar output value with definite-assignment proof for every reachable path. |

Inlining and loop expansion can increase code/plan size dramatically; apply limits to the expanded plan, not only the source. Recursion and dynamic iteration cannot be hidden behind routine calls.

Mutable source variables do not create DSP feedback edges. Assignment lowering tracks the current value; reads before a definition fail analysis. At a conditional merge, only values valid on all possible incoming paths are readable. The resulting operation structure is acyclic.

A conditional may not be replaced by eager execution of both branches if doing so changes faults, state, or cost bounds. In particular, a branch guarding division by zero remains lazy.

### Integration with the visual graph

Expose the artifact as one named node with stable interface identities, local descriptions, source diagnostics, and explicit rate. Internally it contains validated primitive operations; the patch editor may explain/source-map them without expanding the user's patch into hundreds of nodes by default.

The normal graph validator checks its ports, channels, parameter metadata, and cycle/resource rules. Scripts do not connect directly to devices, edit other nodes, install modules, or create dynamic ports.

Compile and prepare a revised artifact off-callback; activate only as a complete graph revision. A failed edit leaves the previous valid artifact active if safe, while displaying the new source as uncompiled/inactive. Do not label old audible behavior as the successful result of the changed source.

In the proposed CMake target structure, Application coordinates compilation and validation through a compiler boundary; a dedicated C++ compiler implementation may be separated when warranted. Domain receives node metadata/rules; Audio consumes only verified plans, not parser services. JUCE Presentation edits source and shows diagnostics without calling device code. Host allocations for tokens/AST/scratch preparation stay off-callback; C++ destructors must not retire artifacts during rendering.

For future plugin export, keep verified artifacts and their executor independent of a device manager, compiler UI or project window. Whether such nodes are export-eligible, how language/runtime versions and source/resources are packaged, and how host parameters map to script parameters require an explicit design. No compiler invocation or artifact loading may occur in host processing. Third-party VST3 processors belong to a separate native adapter and are not subject to this language's sandbox guarantees.

## Optimizer

Apply only semantics-preserving transformations, with cost/equivalence evidence:

- Fold checked constant expressions; report errors rather than folding overflow, division by zero, or non-finite results into an arbitrary value.
- Remove branches with compile-time constant conditions and unreachable operations while retaining diagnostics for invalid declarations and forbidden capabilities.
- Propagate values and remove unused calculations only when doing so does not remove a reachable fault or required effect.
- Reuse common pure expressions only when evaluation, floating-point, and fault behavior are preserved.
- Inline routines and expand bounded loops subject to explicit storage/operation limits.
- Reuse scratch/buffers only after last-use analysis; do not overwrite values needed by another consumer.
- Consider batching/SIMD across samples after verifying branch behavior, event boundaries, and numerical tolerances on both platforms.

Do not reassociate floating-point sums, enable unchecked fast-math, speculate through guarded divisions, or turn a required fault into a healthy result by default. Algebraic identities such as multiplication by zero are not universally safe if they suppress a non-finite intermediate or integer-overflow error.

Optimization produces another verified DSP plan, not source code. Maintain source maps and recompute worst-case cost after each relevant transformation. Every optimized artifact must pass the same validator as an unoptimized one.

## Explicit capability constraints and realtime safety

| Unsupported capability | Enforcement boundary |
|---|---|
| File access | No file/path/string/stream APIs, imports, or foreign calls. The host may save source as project content; scripts cannot choose or read/write storage. |
| Networking | No sockets, URLs, HTTP APIs, service access, or host-object escape routes. Compilation and execution are local. |
| Threads | No creation, tasks, async/await, synchronization primitives, or scheduler access. The graph runtime chooses the execution context. |
| Dynamic allocation | No heap constructors, dynamic arrays, collections, closures, recursion, or dynamically sized data. Fixed scratch and node storage are allocated by the host during preparation. |

A blacklist of names is insufficient. Bind only the closed language vocabulary and verified operations; reject undeclared names and never expose reflection, raw pointers, native functions, dynamic libraries, or an unrestricted native evaluator.

No parsing, compilation, allocation, file access, logging, UI events, or arbitrary callbacks occur in sample/tick execution. Define resource limits for source analysis and expanded operations, fixed storage, nesting, and callback workload before accepting a node. Safe syntax alone does not prove a graph fits its deadline.

Runtime arithmetic faults initialize output safely, signal a bounded fault, and stop through the audio-control owner, following the DSP/platform documents. Do not throw exceptions across native callbacks, continue with hidden saturation, or call the logger from the kernel.

## Specification examples

These are illustrative VAL Script source examples for the proposed grammar. They are not implementation files or claims of an available compiler.

### Variables, constants, procedure, function, IF, and FOR NEXT

This audio-rate scalar node averages four progressively shaped values, then applies a bounded gain. The loop bound is compile-time constant; all scratch/output values are assigned on every path.

```text
NODE ShapedAverage AT AUDIO;
INPUT InSample: REAL;
OUTPUT OutSample: REAL;
PARAM Gain: REAL = 0.5 RANGE 0.0 .. 1.0;
CONST
  Stages = 4;
  Limit = 0.8;
VAR
  i: INTEGER;
  Working, Total: REAL;

FUNCTION Clip(Value: REAL): REAL;
BEGIN
  IF Value > Limit THEN
    Clip := Limit
  ELSE IF Value < -Limit THEN
    Clip := -Limit
  ELSE
    Clip := Value
END;

PROCEDURE ApplyGain(Factor: REAL);
BEGIN
  Working := Working * Factor
END;

BEGIN
  Working := InSample;
  Total := 0.0;
  FOR i := 1 TO Stages DO
  BEGIN
    Working := Clip(Working);
    Total := Total + Working;
    Working := Working * 0.5
  END NEXT i;
  Working := Total / Stages;
  ApplyGain(Gain);
  OutSample := Working
END.
```

`Clip` is pure and only writes its result. `ApplyGain` deliberately writes node-level scratch and is checked as an effectful procedure. Neither allocates memory or retains state between samples.

### Control-rate node and conditional output

```text
NODE ControlGate AT CONTROL;
INPUT Level: REAL;
OUTPUT GatedLevel: REAL;
PARAM Threshold: REAL = 0.25 RANGE 0.0 .. 1.0;
VAR
  Enabled: BOOLEAN;
BEGIN
  Enabled := Level >= Threshold;
  IF Enabled THEN
    GatedLevel := Level
  ELSE
    GatedLevel := 0.0
END.
```

The input is a control value, not an implicit measurement of audio. The result changes on control ticks. Connecting it to an audio path requires the explicit conversion described by DspArchitecture.md.

### Use as a visual DSP graph node

Conceptually connect a built-in audio source's scalar output to `ShapedAverage.InSample`, then route `ShapedAverage.OutSample` through the patch's level/output path. The exact built-in source inventory remains a product decision.

The compiled node exposes `Gain` through the same parameter inspector and qualified automation system as a built-in node. Audio-rate execution receives effective per-sample values, including declared host smoothing; the script does not read a UI control directly or implement a second automation timeline.

The control node can be placed in a control branch and connected only to compatible control consumers or explicit adapters. It is not automatically an audio-rate modulation input.

### Rejected examples and diagnostics

| Attempt | Expected result |
|---|---|
| A loop whose upper bound is an input or parameter. | Compile error: loop bounds must be compile-time integer constants. |
| A procedure calling itself or a later undeclared routine. | Compile error: recursive/forward call is unsupported. |
| Reading scratch or returning a function without assigning every path. | Compile error: value/result is not definitely assigned. |
| Using a file/network/thread/allocation API name. | Compile error: no such language capability or bound host API exists. |
| Writing to an input, parameter, constant, value argument, or active loop counter. | Compile error: target is read-only. |
| A parameter default outside its declared range or a non-finite constant. | Compile error with the declaration's source location. |
| Division by a runtime zero input on the executed branch. | Bounded runtime fault; safe output and visible stopped/failed state, not silent success. |

## Validation and unresolved decisions

Validate token/range ambiguity, precedence, dangling `ELSE`, declaration visibility, purity/effects, definite assignment, zero-trip loops, maximum integer bounds, loop expansion limits, short-circuit guards, and arithmetic faults.

Compare optimized/unoptimized execution for supported numerical semantics. Test sample/tick behavior across callback subdivisions, parameter updates, stale artifact activation, invalid source, interrupted compilation, and queue saturation. Verify no per-sample allocations or unsafe host access; qualify combined graph workloads, not just individual scripts.

Before implementation, settle the language version and exact numeric contract, compiler/resource limits, diagnostics format, parameter units/smoothing metadata, source/artifact persistence and trust policy, supported primitive operations, and cross-platform numerical tolerances. Persistent state, arrays, mathematical intrinsics, richer loops, and transport access require separate designs and are not implied by this document.
