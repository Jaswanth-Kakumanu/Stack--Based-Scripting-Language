#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cairn/chunk.hpp"
#include "cairn/compiler.hpp"
#include "cairn/diagnostic.hpp"
#include "cairn/lexer.hpp"
#include "cairn/parser.hpp"
#include "test_framework.hpp"

using cairn::Chunk;
using cairn::Compiler;
using cairn::DiagnosticBag;
using cairn::Lexer;
using cairn::OpCode;
using cairn::Parser;

namespace {

struct Compiled {
    bool ok = false;
    std::size_t errors = 0;
    Chunk chunk;
};

Compiled compile_source(std::string_view source) {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const auto program = parser.parse();

    Compiled result;
    Compiler compiler(diagnostics);
    auto chunk = compiler.compile(program);
    result.errors = diagnostics.size();
    if (chunk) {
        result.ok = true;
        result.chunk = std::move(*chunk);
    }
    return result;
}

// Space-separated opcode mnemonics, operands omitted. Pins the instruction
// sequence in one readable assertion without hard-coding byte offsets.
std::string mnemonics(const Chunk& chunk) {
    std::string out;
    const auto& code = chunk.code();
    std::size_t offset = 0;
    while (offset < code.size()) {
        const auto op = static_cast<OpCode>(code[offset]);
        if (!out.empty()) {
            out += ' ';
        }
        out += cairn::to_string(op);
        offset += 1 + static_cast<std::size_t>(cairn::operand_bytes(op));
    }
    return out;
}

std::string mnemonics_of(std::string_view source) {
    const Compiled compiled = compile_source(source);
    CHECK_EQ(compiled.errors, std::size_t{0});
    CHECK(compiled.ok);
    return mnemonics(compiled.chunk);
}

}  // namespace

TEST(EmptyProgramCompilesToHalt) {
    CHECK_EQ(mnemonics_of(""), std::string("HALT"));
}

TEST(OperandsArePostOrder) {
    // Stack machines need no register allocation precisely because of this:
    // both operands are pushed before the operator consumes them.
    CHECK_EQ(mnemonics_of("print 1 + 2;"), std::string("CONST CONST ADD PRINT HALT"));
}

TEST(PrecedenceSurvivesIntoTheBytecode) {
    // 1 + 2 * 3 must multiply first.
    CHECK_EQ(mnemonics_of("print 1 + 2 * 3;"),
             std::string("CONST CONST CONST MUL ADD PRINT HALT"));
}

TEST(BooleansUseDedicatedOpcodesNotTheConstantPool) {
    const Compiled compiled = compile_source("print true; print false;");
    CHECK_EQ(mnemonics(compiled.chunk), std::string("TRUE PRINT FALSE PRINT HALT"));
    CHECK_EQ(compiled.chunk.constants().size(), std::size_t{0});
}

TEST(ConstantsAreDeduplicated) {
    const Compiled compiled = compile_source("print 5; print 5; print 5;");
    CHECK_EQ(compiled.chunk.constants().size(), std::size_t{1});
}

TEST(DistinctConstantsAreKept) {
    const Compiled compiled = compile_source("print 5; print 6;");
    CHECK_EQ(compiled.chunk.constants().size(), std::size_t{2});
}

TEST(IntAndBoolAreNotMergedInThePool) {
    // A pool keyed loosely could collapse 1 and true. Value equality is
    // type-strict so that it cannot.
    const Compiled compiled = compile_source("print 1;");
    CHECK_EQ(compiled.chunk.constants().size(), std::size_t{1});
    CHECK(compiled.chunk.constants()[0].is_int());
}

TEST(DeclarationEmitsNoStoreInstruction) {
    // The initializer's result already sits where the variable lives.
    CHECK_EQ(mnemonics_of("let x = 1;"), std::string("CONST HALT"));
}

TEST(VariableReadsBecomeSlotIndices) {
    const Compiled compiled = compile_source("let x = 1; print x;");
    CHECK_EQ(mnemonics(compiled.chunk), std::string("CONST GET_LOCAL PRINT HALT"));
    // GET_LOCAL is at offset 3; its operand is slot 0.
    CHECK_EQ(compiled.chunk.read_u16(4), std::uint16_t{0});
}

TEST(SecondVariableGetsTheNextSlot) {
    const Compiled compiled = compile_source("let a = 1; let b = 2; print b;");
    CHECK_EQ(compiled.chunk.read_u16(7), std::uint16_t{1});
}

TEST(AssignmentLeavesItsValueAndTheStatementPopsIt) {
    CHECK_EQ(mnemonics_of("let x = 1; x = 2;"),
             std::string("CONST CONST SET_LOCAL POP HALT"));
}

TEST(ExpressionStatementsBalanceTheStack) {
    CHECK_EQ(mnemonics_of("let x = 1; x; x;"),
             std::string("CONST GET_LOCAL POP GET_LOCAL POP HALT"));
}

TEST(BlockPopsItsOwnLocals) {
    // Two declarations inside the block means two POPs when it ends.
    CHECK_EQ(mnemonics_of("{ let a = 1; let b = 2; }"),
             std::string("CONST CONST POP POP HALT"));
}

TEST(InnerScopeShadowsOuter) {
    const Compiled compiled = compile_source("let x = 1; { let x = 2; print x; }");
    CHECK_EQ(compiled.errors, std::size_t{0});
    CHECK(compiled.ok);
    // The inner print must read slot 1, not slot 0.
    CHECK_EQ(compiled.chunk.read_u16(7), std::uint16_t{1});
}

TEST(IfWithoutElseHasOneForwardJump) {
    CHECK_EQ(mnemonics_of("if (true) print 1;"),
             std::string("TRUE JUMP_IF_FALSE CONST PRINT HALT"));
}

TEST(IfWithElseHasTwoJumps) {
    CHECK_EQ(mnemonics_of("if (true) print 1; else print 2;"),
             std::string("TRUE JUMP_IF_FALSE CONST PRINT JUMP CONST PRINT HALT"));
}

TEST(ForwardJumpTargetIsCorrect) {
    const Compiled compiled = compile_source("if (true) print 1;");
    // TRUE at 0, JUMP_IF_FALSE at 1, operand at 2, next instruction at 4.
    const std::uint16_t offset = compiled.chunk.read_u16(2);
    const std::size_t target = 4 + offset;
    // The jump must land on HALT: CONST(3 bytes) + PRINT(1) after offset 4.
    CHECK_EQ(target, std::size_t{8});
    CHECK_EQ(std::string(cairn::to_string(static_cast<OpCode>(compiled.chunk.code()[target]))),
             std::string("HALT"));
}

TEST(WhileEmitsABackwardLoop) {
    CHECK_EQ(mnemonics_of("while (true) { print 1; }"),
             std::string("TRUE JUMP_IF_FALSE CONST PRINT LOOP HALT"));
}

TEST(BackwardJumpLandsOnTheConditionAgain) {
    const Compiled compiled = compile_source("while (true) { print 1; }");
    const auto& code = compiled.chunk.code();
    // Find the LOOP instruction.
    std::size_t offset = 0;
    std::size_t loop_at = 0;
    while (offset < code.size()) {
        const auto op = static_cast<OpCode>(code[offset]);
        if (op == OpCode::Loop) {
            loop_at = offset;
            break;
        }
        offset += 1 + static_cast<std::size_t>(cairn::operand_bytes(op));
    }
    const std::uint16_t back = compiled.chunk.read_u16(loop_at + 1);
    const std::size_t target = (loop_at + 3) - back;
    CHECK_EQ(target, std::size_t{0});  // back to re-evaluating the condition
}

TEST(NestedLoopsPatchIndependently) {
    const Compiled compiled =
        compile_source("let i = 0; while (i < 2) { let j = 0; while (j < 2) { j = j + 1; } i = i + 1; }");
    CHECK_EQ(compiled.errors, std::size_t{0});
    CHECK(compiled.ok);
}

TEST(LineTableMapsInstructionsBackToSource) {
    const Compiled compiled = compile_source("print 1;\n\n\nprint 2;\n");
    CHECK_EQ(compiled.chunk.line_at(0), std::uint32_t{1});
    CHECK_EQ(compiled.chunk.line_at(compiled.chunk.size() - 2), std::uint32_t{4});
}

TEST(DisassemblyIsStableForASmallProgram) {
    const Compiled compiled = compile_source("let x = 1;\nprint x + 2;\n");
    const std::string listing = compiled.chunk.disassemble("test");
    CHECK(listing.find("CONST") != std::string::npos);
    CHECK(listing.find("GET_LOCAL") != std::string::npos);
    CHECK(listing.find("ADD") != std::string::npos);
    CHECK(listing.find("HALT") != std::string::npos);
    // Every instruction offset must be reachable by decoding from zero, which
    // the mnemonic walk above already assumes; assert the walk consumed it all.
    CHECK_EQ(mnemonics(compiled.chunk),
             std::string("CONST GET_LOCAL CONST ADD PRINT HALT"));
}

TEST(FullProgramCompilesCleanly) {
    const std::string_view program =
        "let n = 10;\n"
        "let total = 0;\n"
        "while (n > 0) {\n"
        "  total = total + n;\n"
        "  n = n - 1;\n"
        "}\n"
        "if (total == 55) { print true; } else { print total; }\n";

    const Compiled compiled = compile_source(program);
    CHECK_EQ(compiled.errors, std::size_t{0});
    CHECK(compiled.ok);
    CHECK(compiled.chunk.size() > std::size_t{20});
}

TEST_MAIN()
