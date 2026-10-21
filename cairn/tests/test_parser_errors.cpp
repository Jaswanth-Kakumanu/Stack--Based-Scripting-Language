#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cairn/ast.hpp"
#include "cairn/diagnostic.hpp"
#include "cairn/lexer.hpp"
#include "cairn/parser.hpp"
#include "test_framework.hpp"

using cairn::DiagnosticBag;
using cairn::Lexer;
using cairn::Parser;
using cairn::StmtPtr;

namespace {

struct ParseResult {
    std::size_t errors = 0;
    std::size_t statements = 0;
    std::vector<cairn::Diagnostic> diagnostics;
};

ParseResult parse(std::string_view source) {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const std::vector<StmtPtr> program = parser.parse();

    ParseResult result;
    result.errors = diagnostics.size();
    result.statements = program.size();
    result.diagnostics = diagnostics.all();
    return result;
}

}  // namespace

TEST(MissingSemicolonIsReported) {
    const ParseResult result = parse("let x = 1");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(MissingSemicolonDoesNotCascade) {
    // One missing semicolon, three good statements after it. Panic-mode
    // recovery must yield exactly one diagnostic, not four.
    const ParseResult result = parse("let x = 1\nprint 2;\nprint 3;\nprint 4;");
    CHECK_EQ(result.errors, std::size_t{1});
    // The three well-formed statements still made it into the tree.
    CHECK(result.statements >= 3);
}

TEST(RecoveryFindsLaterIndependentErrors) {
    const ParseResult result = parse("let = 1;\nprint 2;\nlet = 3;");
    CHECK_EQ(result.errors, std::size_t{2});
}

TEST(UnclosedParenthesisIsReported) {
    const ParseResult result = parse("print (1 + 2;");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(UnclosedBraceIsReported) {
    const ParseResult result = parse("{ print 1;");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(MissingExpressionIsReported) {
    const ParseResult result = parse("print ;");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(DanglingBinaryOperatorIsReported) {
    const ParseResult result = parse("print 1 + ;");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(InvalidAssignmentTargetIsReported) {
    const ParseResult result = parse("1 + 2 = 3;");
    CHECK_EQ(result.errors, std::size_t{1});
    if (result.errors == 1) {
        CHECK(result.diagnostics[0].message.find("not a variable") != std::string::npos);
    }
}

TEST(MissingConditionParensAreReported) {
    CHECK_EQ(parse("if x { print 1; }").errors, std::size_t{1});
    CHECK_EQ(parse("while x { print 1; }").errors, std::size_t{1});
}

TEST(LetWithoutNameIsReported) {
    const ParseResult result = parse("let 5 = 1;");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(ErrorPointsAtTheOffendingToken) {
    const ParseResult result = parse("let x = 1;\nprint ;\n");
    CHECK_EQ(result.errors, std::size_t{1});
    if (result.errors == 1) {
        CHECK_EQ(result.diagnostics[0].line, std::uint32_t{2});
        CHECK_EQ(result.diagnostics[0].column, std::uint32_t{7});
    }
}

TEST(LexicalErrorIsNotReportedTwiceByTheParser) {
    // '@' is a lexical error. The parser drops the Invalid token rather than
    // producing a second, less useful complaint about the same character.
    const ParseResult result = parse("let x = @;");
    CHECK_EQ(result.errors, std::size_t{1});
}

TEST(LexicalAndSyntaxErrorsSurfaceInOneRun) {
    const ParseResult result = parse("let x = @;\nprint ;\n");
    CHECK_EQ(result.errors, std::size_t{2});
}

TEST(DeclarationCannotBeAConditionalBody) {
    // A variable whose existence depends on a branch cannot be given a stack
    // slot at compile time. The grammar rejects it; see DESIGN.md D27.
    const ParseResult if_body = parse("if (c) let y = 1;");
    CHECK_EQ(if_body.errors, std::size_t{1});
    if (if_body.errors == 1) {
        CHECK(if_body.diagnostics[0].message.find("wrap it in braces") !=
              std::string::npos);
    }

    const ParseResult while_body = parse("while (c) let y = 1;");
    CHECK_EQ(while_body.errors, std::size_t{1});

    const ParseResult else_body = parse("if (c) print 1; else let y = 1;");
    CHECK_EQ(else_body.errors, std::size_t{1});
}

TEST(BracedDeclarationInAConditionalIsFine) {
    const ParseResult result = parse("if (c) { let y = 1; }");
    CHECK_EQ(result.errors, std::size_t{0});
}

TEST(ParserNeverLeaksParseErrorToTheCaller) {
    // Deliberately hostile input: if a ParseError escaped parse(), this test
    // would terminate instead of failing.
    const ParseResult result = parse("} ) ; else while if ( ( (");
    CHECK(result.errors > std::size_t{0});
}

TEST_MAIN()
