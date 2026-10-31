# VAL Script version 1: implemented audio kernels

This is the implemented subset of [ValScriptArchitecture.md](ValScriptArchitecture.md),
not a claim that every proposed feature exists. The local C++20 compiler emits verified
typed register IR, **not native machine code**. `ScriptNode` executes that finite plan
once per sample frame inside the existing `AudioGraph`. This is actual buffer processing,
not a parse-only prototype. There is no script editor, automatic file loading, graph
playback on edit/load, plugin integration or external compiler dependency.

## Small example and runnable sources

```pascal
NODE ScriptGain AT AUDIO;
INPUT InSample: REAL;
OUTPUT OutSample: REAL;
PARAM Gain: REAL = 0.5 RANGE 0.0 .. 2.0;
BEGIN
  OutSample := InSample * Gain
END.
```

The host sets `Gain` through ordinary node parameters; initial values apply immediately,
then target changes follow a 5 ms sample-time linear ramp. Both timestamped automation
and live override/base precedence reuse the existing AudioNode machinery.

`examples/scripts` contains four executable source units:

| File | Demonstrates |
|---|---|
| `Gain.vals` | Mono input, output and smoothed parameter. |
| `ShapedAverage.vals` | Constants, scratch variables, pure functions, procedures, conditional shaping and `FOR ... NEXT`. |
| `StereoGate.vals` | Two explicitly named input/output channels, pure function and short-circuit logic. |
| `Constant.vals` | Zero-input source; generator bypass fades to silence, not a hidden dry route. |

These are stateless kernels; they do not have oscillator clocks, delay memory or arrays.
Run all four through the real node lifecycle without opening a device:

```powershell
cmake --build build\windows-native --config Release --target VisualAudioLab.Script.Tests
.\build\windows-native\tests\Release\VisualAudioLab.Script.Tests.exe --examples examples\scripts
ctest --test-dir build\windows-native -C Release -R "^Script\." --output-on-failure
```

## Grammar and semantics

Identifiers and syntax use case-insensitive ASCII; identifiers start with a letter and continue with
letters, digits or underscores. Metadata names are normalized to uppercase. `//` comments
run to end of line. Decimal literals require digits on both sides of their decimal point;
exponents, strings, Unicode identifiers and hexadecimal literals are not supported.

The grammar below omits whitespace/comments and uses braces for repetition:

```text
unit       = NODE name AT AUDIO ";"
             { INPUT name ":" REAL ";" }
             { OUTPUT name ":" REAL ";" }
             { PARAM name ":" REAL "=" constant RANGE constant ".." constant ";" }
             [ CONST { name "=" constant ";" } ]
             [ VAR { names ":" type ";" } ]
             { routine } compound "." EOF
routine    = (FUNCTION name "(" formals ")" ":" type
             | PROCEDURE name "(" formals ")") ";"
             [ VAR { names ":" type ";" } ] compound ";"
formals    = [ names ":" type { ";" names ":" type } ]
type       = REAL | INTEGER | BOOLEAN
names      = name { "," name }
compound   = BEGIN [ statement { ";" statement } [ ";" ] ] END
statement  = name ":=" expression | name "(" arguments ")" | compound
             | IF expression THEN statement [ ELSE statement ]
             | FOR name ":=" constant TO constant DO statement NEXT name
arguments  = [ expression { "," expression } ]
```

Declarations occur in that order. There is no global executable initialization block.
Parameters precede constants, so their defaults/bounds cannot refer to later constants.
Other constants may read only earlier constants, not runtime values or function results.
`RANGE` is inclusive; default and both bounds must be finite numeric constants.

Precedence, tightest first: unary `+ - NOT`, `* / DIV MOD`, `+ -`, comparisons
`= <> < <= > >=`, `AND`, `OR`. Binary operators associate left; chained comparisons
require explicit boolean expressions. `/` is REAL division; `DIV` and `MOD` require
INTEGER operands. `AND`/`OR` short circuit left-to-right at runtime, including guards
around faulty arithmetic. Constant expressions are checked in full, including a
constant right-hand side of `AND`/`OR`; invalid constant arithmetic is always a compile
error. `ELSE` binds to the nearest unmatched `IF`; do not put a semicolon before it.

- REAL is finite IEEE binary32; INTEGER is checked signed 32-bit; BOOLEAN is distinct.
  INTEGER widens to REAL explicitly in IR before mixed arithmetic/comparisons. There is
  no narrowing, numeric truthiness or REAL-to-INTEGER cast. `DIV` truncates toward zero;
  `MOD` has the dividend's sign (`-7 MOD 3 = -1`).
- Locals and node variables are scratch, not retained signal state. Every read must be
  definitely assigned in that sample's execution; every output and function result
  must be assigned on every path. IF facts intersect both branches. A zero-trip loop
  establishes no assignments. Even unused routine bodies and zero-trip bodies are
  checked. No previous frame's scratch value is semantically observable.
- Functions have by-value, read-only arguments, their own locals and a write-only
  Pascal-style result (`FunctionName := value`). They may read constants, inputs and
  parameters, not mutable node variables/outputs. They cannot call procedures or
  write globals. Returning early and reading the implicit result are unsupported.
- Procedures have by-value, read-only arguments and local scratch; they may read/write
  node variables/outputs. Bodies are rebound at actual call sites so their global
  read requirements and definite writes participate in caller analysis. A procedure
  cannot mutate a caller's local variables or arguments by reference.
- Routine calls are only to earlier declarations: no forward declarations, overloads,
  recursion or mutual recursion. Names cannot shadow node/global/routine/local names.
  A function value cannot be discarded as a procedure; a procedure cannot be an expression.
- `FOR i := lower TO upper DO statement NEXT i` is an inclusive ascending loop with
  compile-time INTEGER bounds. At most 64 iterations, further limited by expanded
  instruction/storage budgets. The counter must be a writable declared INTEGER variable,
  is read-only throughout the body and all transitive procedure effects, and becomes
  unassigned after `NEXT`, including zero iterations. A mismatched NEXT name fails.
  The compiler unrolls loops; reaching INT_MAX never requires an overflowing increment.

The design's `CONTROL` syntax is parsed but **explicitly rejected** by semantic analysis.
The engine has no control tick scheduler/rate adapters; executing it per sample would
silently change its semantics. Other intentional limits are first-error diagnostics
rather than multi-error recovery and a bounded IR executor rather than native codegen.

## Compiler stages and host integration

`Lexer` returns normalized tokens with original one-based line/column and zero-based
byte spans. `Parser` returns an owned AST. `SemanticAnalyzer` performs binding, checked
constant folding, typing, definite-assignment/effect/purity analysis, bounded inlining
and IR verification. `Compiler::compile(source, stop_token)` composes these off-callback
stages and converts compiler memory exhaustion into an explicit diagnostic. A failure
or cancellation returns **no** executable. Public staged AST/token APIs are for the
trusted local compiler pipeline, not a serialized/untrusted executable format.

`Program` has no public construction/mutation/deserialization API. It owns immutable
instructions, scalar slots, port/parameter metadata and source maps. Opcodes contain no
host calls, pointers or backward jumps; the executor has a hard instruction guard.
`Program::execute` takes caller-preallocated scratch with valid finite inputs/parameters
already loaded. Scripts never allocate; compiler/AST/metadata allocations are control-only.

```cpp
const auto compiled = val::script::Compiler::compile(source);
if (!compiled.value)
{
    // Present compiled.diagnostics (line, column, message); retain the active graph.
    return;
}
val::audio::AudioNodeDefinition node{42, val::audio::AudioNodeType::script};
node.script = *compiled.value;
// Add node to an explicit matching-channel graph, prepare/publish off the callback.
```

`ScriptNode` derives from the existing `AudioNode` hierarchy, not a separate processor
engine. It loads frame inputs and smoothed parameters, evaluates all scalar outputs once
in `beginFrame`, then supplies channel samples to the common numerical guard and
latency-aware bypass. It adds no buffering latency. INPUT and OUTPUT declaration order
maps to bus channel order. Output count must equal the prepared bus width (1-32);
input count is either the same or zero. There is no implicit channel conversion.
A zero-input source cannot accept an upstream connection. A declared-input script with
no upstream source reads the existing prepared silent bus, like an unconnected effect.

`AudioGraph::fromStructural` also supports:

| Persisted field | Meaning |
|---|---|
| `audio.type = "Script"` | Explicit processor type; display names do not select runtime behavior. |
| `audio.script` | Source string, compiled by this control-only mapping operation. |
| `audio.param.<name>` | Finite initial value within its declared RANGE; script names match case-insensitively. |
| `audio.bypass` | Existing effect/generator bypass state. |

The current `.valab` scalar schema already preserves source/parameter fields; no compiled
artifact is serialized. Load/edit does not compile or play automatically. Mapping reports
node ID and source location on failure; complete graph preparation still validates
topology, channels, memory and aggregate script cost. Hosts can prepare a new accepted
revision only after compile succeeds. Existing publication/retirement queues own plan
activation and off-thread reclamation. No shared_ptr copies/destruction, compilation,
locks, I/O, logging or UI dispatch occur in `ScriptNode::process`.

## Failure behavior and limits

Arithmetic checks reject constant overflow/division by zero/nonfinite values. At runtime,
the first executed INTEGER overflow, division by zero or nonfinite REAL result stops
the kernel and latches silence on every channel, including the dry bypass path.
INT_MIN `DIV -1` overflows; INT_MIN `MOD -1` is mathematically zero. The common audio
guard additionally rejects output magnitude above 1,000,000; this fault points to that
OUTPUT declaration, with code `outputRange` and `RuntimeFault::noInstruction`, not a
fictitious arithmetic instruction or a mislabeled nonfinite result. Input sanitization and
denormal handling follow AudioNode's existing documented behavior.

`AudioEngine::scriptFault(revision, nodeId)` returns an allocation-free code/source/
instruction record for a Script node, or an explicit wrong-thread/stale/unknown-node
error. `none` is healthy. It describes the latest **accepted** graph, which may still
be pending activation; a new generation does not carry an old fault. The audio writer
stores the immutable first-fault payload before a release publication; control reads
with acquire. Repeated queries do not clear it. Reset/reprepare requires quiescence;
stop/replacement reclaims failed nodes normally. There is no automatic retry/recovery
or UI diagnostic panel in this feature.

| Resource | Bound |
|---|---|
| Source / token count | 65,536 bytes / 16,384 non-EOF tokens. |
| Identifier / numeric literal | 64 / 32 ASCII characters. |
| AST expression/statement nodes | 4,096; syntactic traversal and constructed expression height bounded to 64. |
| Compile work | 200,000 budgeted visits per stage; cancellation checked during traversal. |
| Node declarations / routines / routine locals | 128 / 16 / 128. |
| Routine arguments | 16, all by value. |
| Parameters / input/output channels | 8 / 32 each, subject to bus rules. |
| Loop trips | 64 each; nested expansion must fit all other bounds. |
| Prepared artifact | 1,024 expanded instructions; 256 scalar slots. |
| Graph aggregate | 4,096 expanded script instructions per frame, plus existing graph/channel/memory bounds. |

The budgets are admission bounds, **not deadline guarantees**, and are engineering choices,
not persistent language ABI commitments. ScriptNode preallocates fixed 256-slot scratch,
32 output floats and eight parameter definitions; Program metadata/storage is retained
off-callback. No stateful memory, files, networking, OS APIs, imports, strings, arrays,
pointers, objects, native plugins, threads, recursion, dynamic allocation, unbounded loops
or external/math intrinsics are exposed. Unknown API calls are rejected with source
diagnostics. This is capability exclusion for the language, not an OS sandbox for hostile
native C++ code or proof against every compiler defect.

## Reproducible validation and performance

`Script.CompilerRuntime` covers independent stages, type/folding/resource diagnostics,
functions/procedures and call-site effects, lazy branches, loop counters and bounds,
definite assignment, arithmetic faults/reset/bypass, stereo variable blocks/sample-rate
changes, structural mapping, graph publication and revision-scoped faults, exact-offset
smoothed automation and callback C++ allocation/deallocation instrumentation.
`Script.Examples` compiles and executes all four actual source files. Both use the same
ScriptNode/AudioGraph implementation as API consumers.

```powershell
.\build\windows-native\tests\Release\VisualAudioLab.Script.Tests.exe --stress
```

`Script.Stress` executes eight stereo script nodes in series, 48 kHz, 128 frames,
300 timed blocks after ten warmups. Each kernel has a parameter, branch, inlined function
and four-trip loop. It prints artifact IR/slot/storage counts, fixed node bytes, one
off-thread compile time, callback p50/p99/max and instrumented allocations/frees.
Timings include the prepared graph and host clock/probe envelope, not vendor-device
callbacks. They are synthetic/offline, not hardware latency/xrun or real-time scheduling
qualification. Global C++ allocation overrides include aligned/sized forms but do not
prove the absence of OS/C-library allocations. Windows measurements are saved in the
session implementation report; macOS/other architectures and hardware remain unqualified.
