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

struct RunResult {
    bool runtime_error = false;
    std::string error;
    std::string output;
};

RunResult run(std::string_view source, std::string_view stdin_text = "") {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const auto program = parser.parse();
    Compiler compiler(diagnostics);
    auto chunk = compiler.compile(program);

    RunResult result;
    if (!chunk) {
        result.runtime_error = false;
        result.error = "<compile failed>";
        return result;
    }

    std::ostringstream out;
    std::istringstream in{std::string(stdin_text)};
    VM vm(out, in);
    result.runtime_error = vm.run(*chunk) == VM::Result::RuntimeError;
    result.error = vm.error();
    result.output = out.str();
    return result;
}

}  // namespace

TEST(DivisionByZeroIsCaught) {
    const RunResult result = run("print 1 / 0;");
    CHECK(result.runtime_error);
    CHECK(result.error.find("division by zero") != std::string::npos);
}

TEST(DivisionByAZeroVariableIsCaught) {
    const RunResult result = run("let d = 0; print 10 / d;");
    CHECK(result.runtime_error);
}

TEST(AddingABooleanToAnIntegerIsCaught) {
    // The compiler has no type checker, so this is deliberately a runtime
    // error rather than a compile error. See DESIGN.md D36.
    const RunResult result = run("print 1 + true;");
    CHECK(result.runtime_error);
    CHECK(result.error.find("non-integer") != std::string::npos);
}

TEST(ComparingABooleanWithLessThanIsCaught) {
    const RunResult result = run("print true < 2;");
    CHECK(result.runtime_error);
}

TEST(NegatingABooleanIsCaught) {
    const RunResult result = run("print -true;");
    CHECK(result.runtime_error);
}

TEST(NotOnAnIntegerIsCaught) {
    const RunResult result = run("print !1;");
    CHECK(result.runtime_error);
}

TEST(NonBooleanConditionIsCaught) {
    const RunResult result = run("if (1) print 2;");
    CHECK(result.runtime_error);
    CHECK(result.error.find("not a boolean") != std::string::npos);
}

TEST(AdditionOverflowIsCaught) {
    // Signed overflow is undefined behaviour, so it must be detected before it
    // happens rather than observed afterwards.
    const RunResult result = run("let big = 9223372036854775807; print big + 1;");
    CHECK(result.runtime_error);
    CHECK(result.error.find("overflow") != std::string::npos);
}

TEST(MultiplicationOverflowIsCaught) {
    const RunResult result = run("let big = 9223372036854775807; print big * 2;");
    CHECK(result.runtime_error);
}

TEST(SubtractionOverflowIsCaught) {
    const RunResult result = run("let big = 9223372036854775807; print -big - 2;");
    CHECK(result.runtime_error);
}

TEST(ErrorsCarryASourceLine) {
    const RunResult result = run("let a = 1;\nlet b = 0;\nprint a / b;\n");
    CHECK(result.runtime_error);
    CHECK(result.error.find("line 3") != std::string::npos);
}

TEST(OutputBeforeAnErrorIsStillProduced) {
    const RunResult result = run("print 1;\nprint 2 / 0;\n");
    CHECK(result.runtime_error);
    CHECK_EQ(result.output, std::string("1\n"));
}

TEST(MissingInputIsCaught) {
    const RunResult result = run("let n = input; print n;", "");
    CHECK(result.runtime_error);
    CHECK(result.error.find("standard input") != std::string::npos);
}

TEST(NonNumericInputIsCaught) {
    const RunResult result = run("let n = input; print n;", "hello");
    CHECK(result.runtime_error);
}

TEST(DeepExpressionNestingDoesNotOverflowTheStack) {
    // The value stack is bounded; this checks a legitimate deep expression
    // still fits rather than tripping the guard.
    std::string source = "print 1";
    for (int i = 0; i < 500; ++i) {
        source += " + 1";
    }
    source += ";";

    const RunResult result = run(source);
    CHECK(!result.runtime_error);
    CHECK_EQ(result.output, std::string("501\n"));
}

TEST_MAIN()
