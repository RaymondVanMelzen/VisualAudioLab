#include "TestSupport.h"
#include "AudioAllocationProbe.h"
#include <val/script/Compiler.h>
#include <val/audio/ScriptNode.h>
#include <val/audio/AudioEngine.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

using namespace val::script;
using namespace val::audio;

namespace
{
std::shared_ptr<const Program> compile(std::string_view source)
{
    const auto result = Compiler::compile(source);
    if (!result.value)
    {
        const auto& diagnostic = result.diagnostics.front();
        throw std::runtime_error(std::to_string(diagnostic.source.line) + ":" + std::to_string(diagnostic.source.column) + " " + diagnostic.message);
    }
    return *result.value;
}
std::string unit(std::string body, std::string declarations = {}, std::string routines = {})
{
    return "NODE Test AT AUDIO;\nINPUT X: REAL;\nOUTPUT Y: REAL;\n" + declarations + routines + "\nBEGIN\n" + body + "\nEND.";
}
void rejects(std::string_view source, std::string_view reason)
{
    const auto result = Compiler::compile(source);
    if (result.value) throw std::runtime_error("Expected script rejection: " + std::string(reason));
    require(!result.diagnostics.empty() && result.diagnostics.front().source.line > 0 && result.diagnostics.front().source.column > 0,
            "All source diagnostics carry one-based locations.");
    if (result.diagnostics.front().message.find(reason) == std::string::npos)
        throw std::runtime_error("Expected diagnostic '" + std::string(reason) + "', got '" + result.diagnostics.front().message + "'.");
}
std::vector<float> render(ScriptNode& node, std::span<const float> input, int channels = 1, int block = 64)
{
    const auto frames = static_cast<int>(input.size()) / channels;
    std::vector<float> output(input.size());
    const val::application::AudioBuffer hardware;
    for (int offset = 0; offset < frames; offset += block)
    {
        const auto count = std::min(block, frames - offset);
        std::uint64_t allocations{}, frees{};
        {
            allocationProbe::Scope probe;
            node.process({input.data() + offset, output.data() + offset, channels, frames, count, hardware});
            allocations = allocationProbe::allocations; frees = allocationProbe::deallocations;
        }
        require(allocations == 0 && frees == 0, "Generated script processing, including faults/branches/routines, never allocates/frees.");
    }
    return output;
}
void prepare(ScriptNode& node, int channels = 1, double rate = 48000)
{
    require(node.prepare({rate, 512, channels}).succeeded, "Verified script prepares into existing node lifecycle.");
}
float once(std::string_view source, float x)
{
    ScriptNode node(compile(source)); prepare(node);
    const auto output = render(node, std::array<float, 1>{x});
    require(node.fault().code == FaultCode::none, "Successful script execution has no hidden runtime fault.");
    return output[0];
}
void language()
{
    const auto lexed = Lexer::tokenize("1..4 1.25 := <= >= <> // comment\n Node");
    require(lexed.value && lexed.value->size() == 10 && lexed.value->at(1).text == ".."
            && lexed.value->at(3).kind == Token::Kind::real && lexed.value->at(8).source.line == 2,
            "Longest match distinguishes range dots, decimals, operators, comments and locations.");
    const auto source = unit("Y := (X + Scale) * 2", "CONST Scale = 0.25;\n");
    auto tokens = Lexer::tokenize(source);
    auto ast = Parser::parse(*tokens.value);
    require(ast.value && ast.value->body && ast.value->declarations.size() == 3, "Lexer/Parser/AST are independent real stages.");
    const auto checked = SemanticAnalyzer::analyze(*ast.value);
    require(checked.value && (*checked.value)->instructions().size() > 0 && (*checked.value)->outputs()[0].name == "Y",
            "Semantic analysis lowers authored syntax to immutable typed IR.");
    require(once(source, 0.5f) == 1.5f, "Variables/constants and precedence execute real arithmetic.");
    require(once("node Mixed at audio; input InputSample: real; output ResultSample: real; begin resultsample := INPUTsample end.", 0.375f) == 0.375f,
            "Identifiers and keywords are ASCII case insensitive.");
    require(once(unit("I := -7 DIV 3; J := -7 MOD 3; Y := I * 10 + J", "VAR I, J: INTEGER;\n"), 0) == -21,
            "Checked INTEGER DIV truncates toward zero; MOD has dividend sign.");
    const auto minimum = compile(unit("Y := Minimum", "CONST Minimum = -2147483648;\n"));
    std::array<Value, Program::maxSlots> scratch{};
    require(minimum->execute(scratch).code == FaultCode::none
            && scratch[minimum->outputs()[0].slot].real == static_cast<float>(std::numeric_limits<std::int32_t>::min()),
            "Signed minimum literal is accepted as one signed expression before the audio numerical guard.");
}
void controlFlow()
{
    require(once(unit("Flag := (X <> 0) AND (1.0 / X > 0); IF Flag THEN Y := 1 ELSE Y := 0", "VAR Flag: BOOLEAN;\n"), 0) == 0,
            "AND short circuits guarding a runtime division by zero.");
    require(once(unit("Flag := (X = 0) OR (1.0 / X > 0); IF NOT Flag THEN Y := -1 ELSE Y := 1", "VAR Flag: BOOLEAN;\n"), 0) == 1,
            "OR short circuits left to right and NOT is typed BOOLEAN.");
    require(once(unit("IF X > 0 THEN IF X > 1 THEN Y := 2 ELSE Y := 1 ELSE Y := 0"), 0.5f) == 1,
            "ELSE binds nearest unmatched IF.");
    const auto loop = unit("Total := 0; FOR I := -2 TO 2 DO BEGIN Total := Total + I END NEXT I; Y := Total", "VAR I, Total: INTEGER;\n");
    require(once(loop, 0) == 0, "FOR NEXT uses inclusive compile-time bounds and checked integer computation.");
    require(once(unit("Y := 3; FOR I := 2 TO 1 DO Y := 9 NEXT I", "VAR I: INTEGER;\n"), 0) == 3, "Zero-trip loops do not establish assignments/effects.");
    require(once(unit("FOR I := 2147483647 TO 2147483647 DO Y := 0.25 NEXT I", "VAR I: INTEGER;\n"), 0) == 0.25f,
            "Inclusive INT_MAX loop completion never performs an overflowing runtime increment.");
    require(once(unit("Y := 0; FOR I := 1 TO 3 DO FOR J := 1 TO 2 DO Y := Y + 1 NEXT J NEXT I", "VAR I, J: INTEGER;\n"), 0) == 6,
            "Nested bounded loops expand with distinct protected counters.");
}
void routines()
{
    const auto routines = R"(
FUNCTION DoubleValue(V: REAL): REAL;
BEGIN DoubleValue := V * 2 END;
FUNCTION Shape(V: REAL): REAL;
VAR T: REAL;
BEGIN T := DoubleValue(V); IF T > 1 THEN Shape := 1 ELSE Shape := T END;
PROCEDURE SetValue(V: REAL);
BEGIN Working := Shape(V); Y := Working END;
)";
    require(once(unit("SetValue(X)", "VAR Working: REAL;\n", routines), 0.75f) == 1, "Functions return via own name; procedures establish caller output assignment.");
    require(once(unit("Working := X; Apply(2); Y := Working", "VAR Working: REAL;\n",
        "PROCEDURE Apply(Factor: REAL); BEGIN Working := Working * Factor END;\n"), 0.25f) == 0.5f,
        "Procedure read/write effects use actual call-site definite assignment.");
    const auto isolation = unit("A := 4; Y := Sum(A, A + 1)", "VAR A: INTEGER;\n",
        "FUNCTION Sum(First, Second: INTEGER): INTEGER; BEGIN Sum := First + Second END;\n");
    require(once(isolation, 0) == 9, "Value arguments and function results widen to REAL at assignment.");
}
void diagnostics()
{
    rejects(unit("Y := Missing"), "Unknown name");
    rejects(unit("Y := X", "VAR Temp: REAL;\n") + " extra", "Unexpected content");
    rejects(unit("Y := Temp", "VAR Temp: REAL;\n"), "read before");
    rejects(unit("IF X > 0 THEN Y := X"), "not definitely assigned");
    rejects(unit("IF X > 0 THEN Y := X; ELSE Y := 0"), "identifier");
    rejects(unit("Y := Flag + 1", "VAR Flag: BOOLEAN;\n"), "read before");
    rejects(unit("IF X THEN Y := 1 ELSE Y := 0"), "BOOLEAN");
    rejects(unit("Y := 1", "CONST K = 1 DIV 0;\n"), "constant arithmetic");
    rejects(unit("Y := 1", "CONST K = 2147483647 + 1;\n"), "constant arithmetic");
    rejects(unit("Y := 1", "CONST K = 2147483648;\n"), "32-bit");
    rejects(unit("Y := 1", "CONST K = X;\n"), "earlier constants");
    rejects(unit("X := 1; Y := X"), "read-only");
    rejects(unit("Gain := 1; Y := X", "PARAM Gain: REAL = 0.5 RANGE 0 .. 1;\n"), "read-only");
    rejects(unit("Y := 0", "PARAM Gain: REAL = 2 RANGE 0 .. 1;\n"), "RANGE");
    rejects(unit("Y := K", "CONST K = Later; Later = 1;\n"), "Unknown name");
    rejects(unit("I := X; Y := I", "VAR I: INTEGER;\n"), "Type mismatch");
    rejects(unit("Y := 1.0 DIV 2"), "INTEGER");
    rejects(unit("Y := 1", "VAR X: REAL;\n"), "Duplicate");
    rejects(unit("Y := 1; WHILE X > 0 DO Y := X"), "Expected '('");
    for (auto call : {"ReadFile", "WriteFile", "Socket", "Http", "System", "New", "GetMem", "FreeMem", "Thread", "Import"})
        rejects(unit(std::string(call) + "(); Y := X"), "external APIs");
    rejects(unit("Y := 'text'"), "Unsupported character");
    rejects(unit("Y := X[0]"), "Unsupported character");
    rejects(unit("Y := 0", {}, "FUNCTION Recurse(V: REAL): REAL; BEGIN Recurse := Recurse(V) END;\n"), "recursive");
    rejects(unit("Y := 0", {}, "PROCEDURE A(); BEGIN B() END; PROCEDURE B(); BEGIN Y := 1 END;\n"), "forward");
    rejects(unit("Y := F(X)", {}, "FUNCTION F(V: REAL): REAL; BEGIN IF V > 0 THEN F := V END;\n"), "every path");
    rejects(unit("Y := F(X)", "VAR Working: REAL;\n", "FUNCTION F(V: REAL): REAL; BEGIN F := Working END;\n"), "mutable");
    rejects(unit("Y := F(X)", {}, "FUNCTION F(V: REAL): REAL; BEGIN Y := V; F := V END;\n"), "only locals");
    rejects(unit("Y := F(X)", {}, "FUNCTION F(V: REAL): REAL; BEGIN V := 0; F := V END;\n"), "read-only");
    rejects(unit("Y := F(X)", {}, "FUNCTION F(V: REAL): REAL; BEGIN F := F + V END;\n"), "write-only");
    rejects(unit("Y := X", {}, "FUNCTION F(V: REAL): REAL; VAR X: REAL; BEGIN F := V END;\n"), "shadowed");
    rejects(unit("Apply(); Y := Working", "VAR Working: REAL;\n", "PROCEDURE Apply(); BEGIN Working := Working * 2 END;\n"), "read before");
    rejects(unit("Y := F(X)", {}, "PROCEDURE P(); BEGIN Y := X END; FUNCTION F(V: REAL): REAL; BEGIN P(); F := V END;\n"), "procedures");
    rejects(unit("FOR I := 1 TO 2 DO I := 3 NEXT I; Y := 0", "VAR I: INTEGER;\n"), "read-only");
    rejects(unit("FOR I := 1 TO 2 DO SetCounter() NEXT I; Y := 0", "VAR I: INTEGER;\n",
        "PROCEDURE SetCounter(); BEGIN I := 3 END;\n"), "read-only");
    rejects(unit("FOR I := 1 TO 2 DO Y := I NEXT J", "VAR I, J: INTEGER;\n"), "NEXT");
    rejects(unit("FOR I := 1 TO X DO Y := 0 NEXT I", "VAR I: INTEGER;\n"), "earlier constants");
    rejects(unit("FOR I := 1 TO 65 DO Y := 0 NEXT I", "VAR I: INTEGER;\n"), "64 iterations");
    rejects(unit("FOR I := 1 TO 2 DO Y := 0 NEXT I; Y := I", "VAR I: INTEGER;\n"), "read before");
    rejects(unit("FOR I := 2 TO 1 DO Y := 0 NEXT I", "VAR I: INTEGER;\n"), "not definitely assigned");
    rejects(unit("Y := 0; FOR I := 1 TO 64 DO FOR J := 1 TO 64 DO Y := Y + X NEXT J NEXT I", "VAR I, J: INTEGER;\n"), "1024 instructions");
    rejects("NODE C AT CONTROL; OUTPUT Y: REAL; BEGIN Y := 0 END.", "CONTROL");
    rejects(std::string(65537, ' '), "65536");
    rejects(unit("Y := " + std::string(80, '(') + "X" + std::string(80, ')')), "depth");
    std::string longExpression = "X";
    for (int i = 0; i < 2000; ++i) longExpression += " + X";
    rejects(unit("Y := " + longExpression), "depth");
    rejects(unit("Y := FALSE + TRUE"), "BOOLEAN");
    rejects(unit("Y := F(X) + F(X)", {}, "FUNCTION F(V: REAL): REAL; VAR T: REAL; BEGIN IF V > 0 THEN T := V; F := T END;\n"), "read before");
    const auto reused = unit("Assigned(); Unassigned(); Y := X", {},
        "PROCEDURE Assigned(); VAR T: REAL; BEGIN T := X END;\nPROCEDURE Unassigned(); VAR T: REAL; BEGIN Y := T END;\n");
    rejects(reused, "read before");
    std::stop_source cancellation; cancellation.request_stop();
    const auto result = Compiler::compile(unit("Y := X"), cancellation.get_token());
    require(!result.value && result.diagnostics[0].message.find("cancelled") != std::string::npos, "Cancellation is fail-closed and source-located.");
}
void faultBehavior()
{
    ScriptNode division(compile(unit("Y := 1.0 / X"))); prepare(division);
    const auto output = render(division, std::array<float, 4>{0.5f, 0, 0.5f, 1});
    require(output[0] == 2 && output[1] == 0 && output[2] == 0 && division.fault().code == FaultCode::divisionByZero
            && division.fault().source.line == 6, "First executed arithmetic fault is source-located and latches silence until reset.");
    division.reset();
    require(render(division, std::array<float, 1>{0.5f})[0] == 2 && division.fault().code == FaultCode::none, "Quiescent reset clears latched fault.");
    ScriptNode overflow(compile(unit("I := 2147483647; IF X > 0 THEN I := I + 1; Y := I", "VAR I: INTEGER;\n"))); prepare(overflow);
    (void)render(overflow, std::array<float, 1>{1});
    require(overflow.fault().code == FaultCode::integerOverflow, "Runtime INTEGER overflow never invokes C++ signed overflow.");
    ScriptNode realOverflow(compile(unit("A := X; FOR I := 1 TO 64 DO A := A * 100.0 NEXT I; Y := A", "VAR I: INTEGER; A: REAL;\n"))); prepare(realOverflow);
    (void)render(realOverflow, std::array<float, 1>{1});
    require(realOverflow.fault().code == FaultCode::nonFinite, "Runtime REAL overflow latches an explicit failure, not NaN audio.");
    ScriptNode bypass(compile(unit("Y := 1.0 / X"))); bypass.initialBypass(true); prepare(bypass);
    require(render(bypass, std::array<float, 3>{0.25f, 0, 0.5f}) == std::vector<float>({0.25f, 0, 0}),
            "Bypass does not conceal script execution faults or leak dry data after failure.");
    ScriptNode guard(compile(unit("Y := 2000000"))); prepare(guard);
    require(render(guard, std::array<float, 1>{0.25f})[0] == 0 && guard.fault().code == FaultCode::outputRange
            && guard.fault().source.line == 3 && guard.fault().instruction == RuntimeFault::noInstruction,
            "Finite output guard failures are not mislabeled nonfinite arithmetic or assigned fictitious instruction locations.");
    ScriptNode source(compile("NODE Source AT AUDIO; OUTPUT Y: REAL; BEGIN Y := 0.25 END."));
    source.initialBypass(true); prepare(source);
    require(render(source, std::array<float, 2>{0.5f, -0.5f}) == std::vector<float>({0, 0}),
            "Healthy bypassed zero-input generators fade to silence rather than leaking an undeclared input route.");
}
void integration()
{
    const auto program = compile("NODE Gain AT AUDIO; INPUT X: REAL; OUTPUT Y: REAL; PARAM Level: REAL = 1 RANGE 0 .. 2; BEGIN Y := X * Level END.");
    AudioGraph graph{1, {{1, AudioNodeType::input}, {2, AudioNodeType::script}, {3, AudioNodeType::output}}, {{1, 2}, {2, 3}}};
    graph.nodes[1].script = program;
    AudioEngine engine;
    require(engine.startOffline({48000, 512, 512, 1, 1}, graph, 1).succeeded, "Verified typed IR is executable through real prepared AudioGraph nodes.");
    const auto epoch = engine.telemetry().value->epoch;
    require(engine.scheduleParameter(epoch, 1, 2, 0, 128, 0).succeeded, "Script parameter uses existing exact-offset automation.");
    require(!engine.setParameter(1, 2, 0, 3).succeeded, "Script RANGE is enforced by existing engine parameter validation.");
    std::array<float, 512> input{}, output{}; input.fill(0.5f);
    const float* inputs[]{input.data()}; float* outputs[]{output.data()};
    {
        allocationProbe::Scope probe;
        engine.process({inputs, outputs, 1, 1, 512});
        require(allocationProbe::allocations == 0 && allocationProbe::deallocations == 0, "Integrated engine/scripts/automation allocate/free nothing on callback.");
    }
    require(output[127] == 0.5f && output[128] < 0.5f && output[368] == 0, "Automation takes effect at exact offset, followed by declared 5 ms smoothing.");
    const auto bad = Compiler::compile(unit("Y := Unknown"));
    require(!bad.value && engine.scriptFault(1, 2).value->code == FaultCode::none, "Failed compilation does not replace active valid audio.");
    auto invalidGraph = graph; invalidGraph.nodes[1].script.reset();
    require(!engine.publish(invalidGraph, 2).succeeded, "Preparation refuses deferred source/absent verified artifacts.");
    require(engine.publish(graph, 2).succeeded, "Publish new script generation via existing lock-free plan mailbox.");
    require(!engine.scriptFault(1, 2).value, "Stale revision cannot query a reused script identity.");
    engine.process({inputs, outputs, 1, 1, 512});
    require(output[0] == 0.5f && engine.collectRetired().succeeded, "New script generation initializes params and reclaims old storage off callback.");
    require(engine.stop().succeeded, "Script graph stops through ordinary quiescent lifecycle.");
    ScriptNode mismatched(program);
    require(!mismatched.prepare({48000, 512, 2}).succeeded, "Implicit mono/stereo expansion is rejected.");
    val::domain::Graph structural;
    structural.nodes.emplace(1, val::domain::Node{1, "Script"});
    structural.nodes.at(1).parameters["audio.type"] = std::string("Script");
    structural.nodes.at(1).parameters["audio.script"] = std::string("NODE Source AT AUDIO; OUTPUT Y: REAL; BEGIN Y := 0.25 END.");
    const auto mapped = AudioGraph::fromStructural(structural, 1);
    require(mapped.value && mapped.value->nodes[0].script, "Structural persistence fields map real source into a typed artifact off callback.");
    structural.nodes.at(1).parameters["audio.script"] = std::string("invalid");
    const auto rejected = AudioGraph::fromStructural(structural, 1);
    require(!rejected.value && rejected.error.message.find("1:1") != std::string::npos, "Structural mapping returns source-located compile errors.");
    structural.nodes.at(1).parameters["audio.script"] = std::string("NODE Source AT AUDIO; OUTPUT Y: REAL; PARAM Level: REAL = 0.1 RANGE 0 .. 1; BEGIN Y := Level END.");
    structural.nodes.at(1).parameters["audio.param.level"] = 0.25;
    const auto withParameter = AudioGraph::fromStructural(structural, 1);
    require(withParameter.value && withParameter.value->nodes[0].parameters[0] == std::pair<std::uint16_t, float>{std::uint16_t{0}, 0.25f},
            "Persisted parameter names honor script case-insensitive identities.");
    AudioGraph failing{1, {{2, AudioNodeType::script}, {3, AudioNodeType::output}}, {{2, 3}}};
    failing.nodes[0].script = compile("NODE Source AT AUDIO; OUTPUT Y: REAL; VAR Z: REAL; BEGIN Z := 0; Y := 1.0 / Z END.");
    require(engine.startOffline({48000, 512, 512, 0, 1}, failing, 1).succeeded, "A script generator uses no hidden hardware input.");
    engine.process({nullptr, outputs, 0, 1, 512});
    require(engine.scriptFault(1, 2).value->code == FaultCode::divisionByZero
            && std::all_of(output.begin(), output.end(), [](float value) { return value == 0; }),
            "First source-aware runtime failure is observable through engine identity/revision and emits silence.");
    require(engine.stop().succeeded, "Faulted script plans remain stoppable/reclaimable.");
    std::string expanded = "Y := X;";
    for (int i = 0; i < 300; ++i) expanded += " Y := Y + X;";
    const auto largeProgram = compile(unit(expanded));
    AudioGraph tooLarge{1};
    for (std::uint64_t id = 1; id <= 8; ++id)
    {
        AudioNodeDefinition definition{id, AudioNodeType::script}; definition.script = largeProgram;
        tooLarge.nodes.push_back(std::move(definition));
        if (id > 1) tooLarge.connections.push_back({id - 1, id});
    }
    tooLarge.nodes.push_back({9, AudioNodeType::output}); tooLarge.connections.push_back({8, 9});
    const auto budget = PreparedAudioGraph::prepare(tooLarge, {48000, 128, 128, 1, 1}, 1);
    require(!budget.value && budget.error.message.find("4096") != std::string::npos, "Aggregate script work is admitted per graph, not just per artifact.");
    Value primitive;
    require(evaluate(Op::integerDivide, Type::integer, Value::fromInteger(std::numeric_limits<std::int32_t>::min()), Value::fromInteger(-1), primitive)
            == FaultCode::integerOverflow, "INT_MIN DIV -1 is a checked overflow, never host undefined behavior.");
    require(evaluate(Op::modulo, Type::integer, Value::fromInteger(std::numeric_limits<std::int32_t>::min()), Value::fromInteger(-1), primitive)
            == FaultCode::none && primitive.integer == 0, "Mathematical INT_MIN MOD -1 is defined as zero.");
    ScriptNode stereo(compile("NODE Stereo AT AUDIO; INPUT L: REAL; INPUT R: REAL; OUTPUT A: REAL; OUTPUT B: REAL; BEGIN A := L * 2; B := -R END."));
    prepare(stereo, 2, 96000);
    std::array<float, 258> stereoInput{};
    std::fill_n(stereoInput.begin(), 129, 0.25f); std::fill_n(stereoInput.begin() + 129, 129, 0.75f);
    const auto stereoOutput = render(stereo, stereoInput, 2, 17);
    require(stereoOutput[128] == 0.5f && stereoOutput[257] == -0.75f, "Scalar outputs execute once per frame with correct planar stereo/variable-block geometry at 96 kHz.");
    prepare(stereo, 2, 44100);
    require(render(stereo, stereoInput, 2, 31) == stereoOutput, "Quiescent sample-rate changes reset without stale state/allocations during render.");
}
void stress()
{
    const auto source = R"(NODE Work AT AUDIO;
INPUT L: REAL; INPUT R: REAL; OUTPUT A: REAL; OUTPUT B: REAL;
PARAM Level: REAL = 0.99 RANGE 0 .. 1;
VAR I: INTEGER; T: REAL;
FUNCTION Shape(V: REAL): REAL; BEGIN IF V > 1 THEN Shape := 1 ELSE Shape := V END;
BEGIN T := L; FOR I := 1 TO 4 DO T := Shape(T * Level) NEXT I; A := T; B := R * Level END.)";
    const auto started = std::chrono::steady_clock::now();
    const auto program = compile(source);
    const auto compileUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    AudioGraph graph{2, {{1, AudioNodeType::input}}, {}};
    for (std::uint64_t id = 2; id <= 9; ++id)
    {
        AudioNodeDefinition definition{id, AudioNodeType::script}; definition.script = program;
        graph.nodes.push_back(std::move(definition)); graph.connections.push_back({id - 1, id});
    }
    graph.nodes.push_back({10, AudioNodeType::output}); graph.connections.push_back({9, 10});
    auto prepared = PreparedAudioGraph::prepare(graph, {48000, 128, 128, 2, 2}, 1);
    require(prepared.value.has_value(), "Reproducible eight-script stereo workload fits graph resource bounds.");
    std::array<float, 128> left{}, right{}, leftOut{}, rightOut{}; left.fill(0.25f); right.fill(-0.25f);
    const float* inputs[]{left.data(), right.data()}; float* outputs[]{leftOut.data(), rightOut.data()};
    std::vector<double> timings(300);
    std::uint64_t allocations = 0, frees = 0;
    auto& plan = **prepared.value;
    for (int i = 0; i < 310; ++i)
    {
        const auto before = std::chrono::steady_clock::now();
        {
            allocationProbe::Scope probe;
            plan.process({inputs, outputs, 2, 2, 128});
            allocations += allocationProbe::allocations; frees += allocationProbe::deallocations;
        }
        if (i >= 10) timings[i - 10] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - before).count();
    }
    require(allocations == 0 && frees == 0 && plan.scriptFault(2)->code == FaultCode::none,
            "Repeated generated DSP including inline functions/loops has zero instrumented callback allocation/free.");
    std::sort(timings.begin(), timings.end());
    std::cout << "Offline synthetic scripts=8 channels=2 rate=48000 frames=128 blocks=300 warmup=10"
              << " expanded_IR_per_script=" << program->instructions().size() << " slots_per_script=" << program->slots()
              << " artifact_bytes=" << program->storageBytes() << " fixed_node_bytes=" << sizeof(ScriptNode)
              << " compile_us=" << compileUs << " callback_p50_us=" << timings[150] << " callback_p99_us=" << timings[296]
              << " callback_max_us=" << timings.back() << " allocations=" << allocations << " frees=" << frees << '\n';
}
void examples(const std::filesystem::path& directory)
{
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".vals")
        {
            std::ifstream stream(entry.path()); std::ostringstream text; text << stream.rdbuf();
            require(stream.good() || stream.eof(), "Host reads example source successfully; scripts have no file APIs.");
            const auto program = compile(text.str());
            ScriptNode node(program); prepare(node, static_cast<int>(program->outputs().size()));
            std::vector<float> input(program->outputs().size() * 512, 0.25f);
            const auto output = render(node, input, static_cast<int>(program->outputs().size()));
            require(node.fault().code == FaultCode::none && std::all_of(output.begin(), output.end(), [](auto sample) { return std::isfinite(sample); }),
                    "Every supplied example executes real finite DSP without allocating on callback.");
            std::cout << program->name() << ": channels=" << program->outputs().size() << " IR=" << program->instructions().size()
                      << " scratch=" << program->slots() << " first_output=" << output[0] << '\n';
            ++count;
        }
    require(count == 4, "All four runnable example files were exercised.");
}
}
int main(int argc, char** argv)
{
    return runTests([&]
    {
        if (argc == 3 && std::string_view(argv[1]) == "--examples") { examples(std::filesystem::path(argv[2])); return; }
        if (argc == 2 && std::string_view(argv[1]) == "--stress") { stress(); return; }
        language(); controlFlow(); routines(); diagnostics(); faultBehavior(); integration();
    });
}
