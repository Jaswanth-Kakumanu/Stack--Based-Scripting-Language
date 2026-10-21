#include <string>
#include <string_view>
#include <vector>

#include "cairn/diagnostic.hpp"
#include "cairn/lexer.hpp"
#include "cairn/token.hpp"
#include "test_framework.hpp"

using cairn::DiagnosticBag;
using cairn::Lexer;
using cairn::Token;
using cairn::TokenKind;

namespace {

std::size_t error_count(std::string_view source) {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    (void)lexer.tokenize();
    return diagnostics.size();
}

}  // namespace

TEST(UnexpectedCharacterIsReported) {
    CHECK_EQ(error_count("let x = @;"), std::size_t{1});
}

TEST(ScanningContinuesAfterAnError) {
    // Three bad characters must produce three diagnostics, not one abort.
    CHECK_EQ(error_count("@ # $"), std::size_t{3});
}

TEST(TokenStreamStillTerminatesAfterErrors) {
    DiagnosticBag diagnostics;
    Lexer lexer("@ let x", diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK(diagnostics.has_errors());
    CHECK_EQ(std::string(cairn::to_string(tokens.back().kind)),
             std::string("EndOfFile"));
    // The valid tail was still scanned: Invalid, Let, Identifier, EndOfFile.
    CHECK_EQ(tokens.size(), std::size_t{4});
    CHECK_EQ(std::string(cairn::to_string(tokens[1].kind)), std::string("Let"));
}

TEST(ErrorCarriesAccurateLineAndColumn) {
    DiagnosticBag diagnostics;
    Lexer lexer("let a = 1;\nlet b = @;\n", diagnostics);
    (void)lexer.tokenize();

    CHECK_EQ(diagnostics.size(), std::size_t{1});
    if (diagnostics.size() == 1) {
        CHECK_EQ(diagnostics.all()[0].line, std::uint32_t{2});
        CHECK_EQ(diagnostics.all()[0].column, std::uint32_t{9});
    }
}

TEST(NumberFollowedByLetterIsRejected) {
    DiagnosticBag diagnostics;
    Lexer lexer("123abc", diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK_EQ(diagnostics.size(), std::size_t{1});
    CHECK_EQ(std::string(cairn::to_string(tokens[0].kind)), std::string("Invalid"));
    // The whole malformed run is consumed, so we get one error and not two.
    CHECK_EQ(tokens.size(), std::size_t{2});
}

TEST(IntegerOverflowIsReported) {
    DiagnosticBag diagnostics;
    Lexer lexer("9223372036854775808", diagnostics);  // int64 max + 1
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK_EQ(diagnostics.size(), std::size_t{1});
    CHECK_EQ(std::string(cairn::to_string(tokens[0].kind)), std::string("Invalid"));
}

TEST(UnterminatedBlockCommentIsReported) {
    DiagnosticBag diagnostics;
    Lexer lexer("let x = 1; /* never closed", diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK_EQ(diagnostics.size(), std::size_t{1});
    CHECK_EQ(std::string(cairn::to_string(tokens.back().kind)),
             std::string("EndOfFile"));
}

TEST(UnterminatedCommentReportsItsOpeningPosition) {
    DiagnosticBag diagnostics;
    Lexer lexer("let x;\n  /* oops", diagnostics);
    (void)lexer.tokenize();

    if (diagnostics.size() == 1) {
        CHECK_EQ(diagnostics.all()[0].line, std::uint32_t{2});
        CHECK_EQ(diagnostics.all()[0].column, std::uint32_t{3});
    }
}

TEST(NonAsciiByteIsReportedWithoutUndefinedBehaviour) {
    // 0xFF would be a negative char; std::isalpha on it is UB.
    CHECK_EQ(error_count("let \xFF = 1;"), std::size_t{1});
}

TEST(DiagnosticRenderProducesCaretUnderTheProblem) {
    const std::string source = "let x = @;";
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    (void)lexer.tokenize();

    std::ostringstream out;
    diagnostics.render(out, source, "inline.cairn");
    const std::string rendered = out.str();

    CHECK(rendered.find("inline.cairn:1:9: error:") != std::string::npos);
    CHECK(rendered.find('^') != std::string::npos);
}

TEST_MAIN()
