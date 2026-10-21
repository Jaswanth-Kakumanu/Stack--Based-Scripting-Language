#include <string>
#include <string_view>
#include <vector>

#include "cairn/chunk.hpp"
#include "cairn/compiler.hpp"
#include "cairn/diagnostic.hpp"
#include "cairn/lexer.hpp"
#include "cairn/parser.hpp"
#include "test_framework.hpp"

using cairn::Compiler;
using cairn::DiagnosticBag;
using cairn::Lexer;
using cairn::Parser;

namespace {

struct Result {
    bool produced_chunk = false;
    std::size_t errors = 0;
    std::vector<cairn::Diagnostic> diagnostics;
};

Result compile_source(std::string_view source) {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const auto program = parser.parse();

    Compiler compiler(diagnostics);
    const auto chunk = compiler.compile(program);

    Result result;
    result.produced_chunk = chunk.has_value();
    result.errors = diagnostics.size();
    result.diagnostics = diagnostics.all();
    return result;
}

}  // namespace

TEST(UndefinedVariableIsRejected) {
    const Result result = compile_source("print x;");
    CHECK_EQ(result.errors, std::size_t{1});
    CHECK(!result.produced_chunk);
    if (result.errors == 1) {
        CHECK(result.diagnostics[0].message.find("undefined variable") !=
              std::string::npos);
    }
}

TEST(AssignmentToUndefinedVariableIsRejected) {
    const Result result = compile_source("x = 1;");
    CHECK_EQ(result.errors, std::size_t{1});
    CHECK(!result.produced_chunk);
}

TEST(VariableIsNotVisibleBeforeItsDeclaration) {
    const Result result = compile_source("print x; let x = 1;");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(VariableIsNotVisibleAfterItsScopeEnds) {
    const Result result = compile_source("{ let x = 1; }\nprint x;");
    CHECK_EQ(result.errors, std::size_t{1});
    if (result.errors == 1) {
        CHECK_EQ(result.diagnostics[0].line, std::uint32_t{2});
    }
}

TEST(RedeclarationInTheSameScopeIsRejected) {
    const Result result = compile_source("let x = 1; let x = 2;");
    CHECK_EQ(result.errors, std::size_t{1});
    if (result.errors == 1) {
        CHECK(result.diagnostics[0].message.find("already declared") !=
              std::string::npos);
    }
}

TEST(ShadowingInAnInnerScopeIsAllowed) {
    const Result result = compile_source("let x = 1; { let x = 2; }");
    CHECK_EQ(result.errors, std::size_t{0});
    CHECK(result.produced_chunk);
}

TEST(SiblingScopesMayReuseAName) {
    const Result result = compile_source("{ let x = 1; }\n{ let x = 2; }");
    CHECK_EQ(result.errors, std::size_t{0});
    CHECK(result.produced_chunk);
}

TEST(CompilerCollectsSeveralErrorsInOneRun) {
    // Unlike the parser, the compiler does not unwind: a bad node does not
    // hide the ones after it.
    const Result result = compile_source("print a; print b; print c;");
    CHECK_EQ(result.errors, std::size_t{3});
}

TEST(ErrorCarriesTheVariableLocation) {
    const Result result = compile_source("let a = 1;\nprint zzz;\n");
    CHECK_EQ(result.errors, std::size_t{1});
    if (result.errors == 1) {
        CHECK_EQ(result.diagnostics[0].line, std::uint32_t{2});
        CHECK_EQ(result.diagnostics[0].column, std::uint32_t{7});
    }
}

TEST(NoChunkIsProducedWhenCompilationFails) {
    // A partially emitted chunk must never escape to the VM: a jump whose
    // target was never patched would run into whatever byte follows it.
    const Result result = compile_source("print nope;");
    CHECK(!result.produced_chunk);
}

TEST(LoopBodyBeyondTheJumpRangeIsRejected) {
    // The u16 offset caps a loop body at 65535 bytes. Each `print 1;` costs
    // four bytes, so ~20k of them push past it.
    std::string source = "let x = 1;\nwhile (x < 2) {\n";
    source.reserve(200000);
    for (int i = 0; i < 20000; ++i) {
        source += "print 1;\n";
    }
    source += "}\n";

    const Result result = compile_source(source);
    CHECK(result.errors > std::size_t{0});
    CHECK(!result.produced_chunk);
    if (result.errors > 0) {
        CHECK(result.diagnostics[0].message.find("too large") != std::string::npos);
    }
}

TEST(ForwardBranchBeyondTheJumpRangeIsRejected) {
    std::string source = "if (true) {\n";
    source.reserve(200000);
    for (int i = 0; i < 20000; ++i) {
        source += "print 1;\n";
    }
    source += "}\n";

    const Result result = compile_source(source);
    CHECK(result.errors > std::size_t{0});
    CHECK(!result.produced_chunk);
    if (result.errors > 0) {
        CHECK(result.diagnostics[0].message.find("too far") != std::string::npos);
    }
}

TEST_MAIN()
