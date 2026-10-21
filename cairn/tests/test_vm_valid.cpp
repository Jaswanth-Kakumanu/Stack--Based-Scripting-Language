#include <sstream>
#include <string>
#include <string_view>

#include "cairn/chunk.hpp"
#include "cairn/compiler.hpp"
#include "cairn/diagnostic.hpp"
#include "cairn/lexer.hpp"
#include "cairn/parser.hpp"
#include "cairn/vm.hpp"
#include "test_framework.hpp"

using cairn::Compiler;
using cairn::DiagnosticBag;
using cairn::Lexer;
using cairn::Parser;
using cairn::VM;

namespace {

// End-to-end: source in, printed output out. These are the tests that would
// catch a wrong opcode, a mispatched jump or an off-by-one slot index - the
// earlier suites pin the pieces, this one pins the whole pipeline.
std::string run(std::string_view source, std::string_view stdin_text = "") {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const auto program = parser.parse();
    Compiler compiler(diagnostics);
    auto chunk = compiler.compile(program);

    CHECK_EQ(diagnostics.size(), std::size_t{0});
    if (!chunk) {
        return "<compile failed>";
    }

    std::ostringstream out;
    std::istringstream in{std::string(stdin_text)};
    VM vm(out, in);
    if (vm.run(*chunk) == VM::Result::RuntimeError) {
        return "<runtime error: " + vm.error() + ">";
    }
    return out.str();
}

}  // namespace

TEST(PrintsAnInteger) { CHECK_EQ(run("print 42;"), std::string("42\n")); }

TEST(PrintsBooleans) {
    CHECK_EQ(run("print true; print false;"), std::string("true\nfalse\n"));
}

TEST(Arithmetic) {
    CHECK_EQ(run("print 2 + 3;"), std::string("5\n"));
    CHECK_EQ(run("print 10 - 4;"), std::string("6\n"));
    CHECK_EQ(run("print 6 * 7;"), std::string("42\n"));
    CHECK_EQ(run("print 20 / 3;"), std::string("6\n"));  // truncating division
}

TEST(PrecedenceHoldsAtRuntime) {
    CHECK_EQ(run("print 1 + 2 * 3;"), std::string("7\n"));
    CHECK_EQ(run("print (1 + 2) * 3;"), std::string("9\n"));
    CHECK_EQ(run("print 10 - 2 - 3;"), std::string("5\n"));  // left-associative
}

TEST(UnaryOperators) {
    CHECK_EQ(run("print -5;"), std::string("-5\n"));
    CHECK_EQ(run("print --5;"), std::string("5\n"));
    CHECK_EQ(run("print !true;"), std::string("false\n"));
    CHECK_EQ(run("print -(2 + 3);"), std::string("-5\n"));
}

TEST(Comparisons) {
    CHECK_EQ(run("print 1 < 2;"), std::string("true\n"));
    CHECK_EQ(run("print 2 <= 2;"), std::string("true\n"));
    CHECK_EQ(run("print 3 > 4;"), std::string("false\n"));
    CHECK_EQ(run("print 4 >= 4;"), std::string("true\n"));
    CHECK_EQ(run("print 1 == 1;"), std::string("true\n"));
    CHECK_EQ(run("print 1 != 1;"), std::string("false\n"));
}

TEST(EqualityIsTypeStrict) {
    // 1 and true must not compare equal, or the constant pool and the VM
    // disagree about what a value is.
    CHECK_EQ(run("print 1 == true;"), std::string("false\n"));
    CHECK_EQ(run("print 0 == false;"), std::string("false\n"));
}

TEST(VariablesReadAndWrite) {
    CHECK_EQ(run("let x = 7; print x;"), std::string("7\n"));
    CHECK_EQ(run("let x = 7; x = x + 1; print x;"), std::string("8\n"));
}

TEST(SeveralVariablesKeepTheirOwnSlots) {
    CHECK_EQ(run("let a = 1; let b = 2; let c = 3; print a; print b; print c;"),
             std::string("1\n2\n3\n"));
}

TEST(AssignmentIsAnExpression) {
    CHECK_EQ(run("let a = 0; let b = 0; a = b = 5; print a; print b;"),
             std::string("5\n5\n"));
}

TEST(ShadowingResolvesToTheInnerVariable) {
    CHECK_EQ(run("let x = 1; { let x = 2; print x; } print x;"),
             std::string("2\n1\n"));
}

TEST(BlockLocalsAreDiscardedOnExit) {
    // If end_scope emitted the wrong number of POPs, the second print would
    // read a stale slot.
    CHECK_EQ(run("let a = 1; { let b = 2; let c = 3; } print a;"),
             std::string("1\n"));
}

TEST(IfTakesTheThenBranch) {
    CHECK_EQ(run("if (1 < 2) print 10; else print 20;"), std::string("10\n"));
}

TEST(IfTakesTheElseBranch) {
    CHECK_EQ(run("if (1 > 2) print 10; else print 20;"), std::string("20\n"));
}

TEST(IfWithoutElseSkipsCleanly) {
    CHECK_EQ(run("if (false) print 10; print 99;"), std::string("99\n"));
}

TEST(NestedIfElseBindsAsParsed) {
    CHECK_EQ(run("if (true) { if (false) print 1; else print 2; }"),
             std::string("2\n"));
}

TEST(WhileLoopRuns) {
    CHECK_EQ(run("let i = 0; while (i < 3) { print i; i = i + 1; }"),
             std::string("0\n1\n2\n"));
}

TEST(WhileLoopWithFalseConditionNeverRuns) {
    CHECK_EQ(run("while (false) { print 1; } print 2;"), std::string("2\n"));
}

TEST(NestedLoops) {
    CHECK_EQ(run("let i = 0;\n"
                 "while (i < 2) {\n"
                 "  let j = 0;\n"
                 "  while (j < 2) { print i * 10 + j; j = j + 1; }\n"
                 "  i = i + 1;\n"
                 "}"),
             std::string("0\n1\n10\n11\n"));
}

TEST(LoopWithLocalsDoesNotGrowTheStack) {
    // 5000 iterations, each declaring a local in the body. If the block failed
    // to pop its slot, this would overflow the stack rather than finish.
    CHECK_EQ(run("let i = 0;\n"
                 "while (i < 5000) { let t = i; i = i + 1; }\n"
                 "print i;"),
             std::string("5000\n"));
}

TEST(SumsToTheExpectedTotal) {
    CHECK_EQ(run("let n = 10; let total = 0;\n"
                 "while (n > 0) { total = total + n; n = n - 1; }\n"
                 "print total;"),
             std::string("55\n"));
}

TEST(FibonacciByIteration) {
    CHECK_EQ(run("let a = 0; let b = 1; let i = 0;\n"
                 "while (i < 10) { let t = a + b; a = b; b = t; i = i + 1; }\n"
                 "print a;"),
             std::string("55\n"));
}

TEST(InputIsRead) {
    CHECK_EQ(run("let n = input; print n * 2;", "21"), std::string("42\n"));
}

TEST(SeveralInputsAreReadInOrder) {
    CHECK_EQ(run("let a = input; let b = input; print a - b;", "10 4"),
             std::string("6\n"));
}

TEST(InstructionCounterAdvances) {
    DiagnosticBag diagnostics;
    Lexer lexer("print 1 + 2;", diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const auto program = parser.parse();
    Compiler compiler(diagnostics);
    auto chunk = compiler.compile(program);

    std::ostringstream out;
    std::istringstream in;
    VM vm(out, in);
    CHECK(vm.run(*chunk) == VM::Result::Ok);
    // CONST CONST ADD PRINT HALT
    CHECK_EQ(vm.instructions_executed(), std::uint64_t{5});
}

TEST(SameProgramRunsIdenticallyTwice) {
    // run() must fully reset state; a stale stack pointer would show up here.
    DiagnosticBag diagnostics;
    Lexer lexer("let i = 0; while (i < 3) { print i; i = i + 1; }", diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const auto program = parser.parse();
    Compiler compiler(diagnostics);
    auto chunk = compiler.compile(program);

    std::ostringstream first_out;
    std::ostringstream second_out;
    std::istringstream in;
    VM vm(first_out, in);
    vm.run(*chunk);
    const std::uint64_t first_count = vm.instructions_executed();

    VM vm2(second_out, in);
    vm2.run(*chunk);
    vm2.run(*chunk);

    CHECK_EQ(first_out.str(), std::string("0\n1\n2\n"));
    CHECK_EQ(vm2.instructions_executed(), first_count);
}

TEST_MAIN()
