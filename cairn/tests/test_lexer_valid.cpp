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

// Golden-stream helper: scan `source` and compare the token kinds against the
// expected sequence. The trailing EndOfFile is appended automatically.
void expect_kinds(std::string_view source, std::vector<TokenKind> expected) {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    expected.push_back(TokenKind::EndOfFile);

    CHECK_EQ(diagnostics.size(), std::size_t{0});
    CHECK_EQ(tokens.size(), expected.size());
    if (tokens.size() != expected.size()) {
        return;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK_EQ(std::string(cairn::to_string(tokens[i].kind)),
                 std::string(cairn::to_string(expected[i])));
    }
}

}  // namespace

TEST(EmptySourceYieldsOnlyEof) {
    expect_kinds("", {});
}

TEST(WhitespaceOnlyYieldsOnlyEof) {
    expect_kinds("   \n\t\r\n  ", {});
}

TEST(SingleCharacterOperators) {
    expect_kinds("+ - * / ( ) { } ; = < > !",
                 {TokenKind::Plus, TokenKind::Minus, TokenKind::Star,
                  TokenKind::Slash, TokenKind::LParen, TokenKind::RParen,
                  TokenKind::LBrace, TokenKind::RBrace, TokenKind::Semicolon,
                  TokenKind::Assign, TokenKind::Less, TokenKind::Greater,
                  TokenKind::Bang});
}

TEST(MaximalMunchPrefersTwoCharacterOperators) {
    expect_kinds("== != <= >= = < > !",
                 {TokenKind::EqualEqual, TokenKind::BangEqual,
                  TokenKind::LessEqual, TokenKind::GreaterEqual,
                  TokenKind::Assign, TokenKind::Less, TokenKind::Greater,
                  TokenKind::Bang});
}

TEST(KeywordsAreRecognised) {
    expect_kinds("let if else while print input true false",
                 {TokenKind::Let, TokenKind::If, TokenKind::Else,
                  TokenKind::While, TokenKind::Print, TokenKind::Input,
                  TokenKind::True, TokenKind::False});
}

TEST(KeywordPrefixesRemainIdentifiers) {
    // 'lets' and 'iffy' merely start with a keyword; they are not reserved.
    expect_kinds("lets iffy printer trueish",
                 {TokenKind::Identifier, TokenKind::Identifier,
                  TokenKind::Identifier, TokenKind::Identifier});
}

TEST(IdentifiersAllowUnderscoresAndDigits) {
    expect_kinds("_x x1 snake_case _9",
                 {TokenKind::Identifier, TokenKind::Identifier,
                  TokenKind::Identifier, TokenKind::Identifier});
}

TEST(IntegerLiteralsCarryTheirValue) {
    DiagnosticBag diagnostics;
    Lexer lexer("0 7 42 9223372036854775807", diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK_EQ(diagnostics.size(), std::size_t{0});
    CHECK_EQ(tokens.size(), std::size_t{5});
    CHECK_EQ(tokens[0].int_value, std::int64_t{0});
    CHECK_EQ(tokens[1].int_value, std::int64_t{7});
    CHECK_EQ(tokens[2].int_value, std::int64_t{42});
    CHECK_EQ(tokens[3].int_value, std::int64_t{9223372036854775807});
}

TEST(LexemesAreViewsIntoTheOriginalBuffer) {
    const std::string source = "let total = 123;";
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK_EQ(std::string(tokens[1].lexeme), std::string("total"));
    // The view must point inside `source`, not at a copy.
    CHECK(tokens[1].lexeme.data() >= source.data());
    CHECK(tokens[1].lexeme.data() < source.data() + source.size());
}

TEST(LineAndColumnTracking) {
    DiagnosticBag diagnostics;
    Lexer lexer("let x\n  = 5;\n", diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK_EQ(tokens[0].line, std::uint32_t{1});   // let
    CHECK_EQ(tokens[0].column, std::uint32_t{1});
    CHECK_EQ(tokens[1].line, std::uint32_t{1});   // x
    CHECK_EQ(tokens[1].column, std::uint32_t{5});
    CHECK_EQ(tokens[2].line, std::uint32_t{2});   // =
    CHECK_EQ(tokens[2].column, std::uint32_t{3});
    CHECK_EQ(tokens[3].line, std::uint32_t{2});   // 5
    CHECK_EQ(tokens[3].column, std::uint32_t{5});
}

TEST(LineCommentsAreTrivia) {
    expect_kinds("let // this is ignored\nx", {TokenKind::Let, TokenKind::Identifier});
}

TEST(BlockCommentsAreTrivia) {
    expect_kinds("let /* ignored\nacross lines */ x",
                 {TokenKind::Let, TokenKind::Identifier});
}

TEST(SlashIsStillDivisionWhenNotAComment) {
    expect_kinds("6 / 2", {TokenKind::Integer, TokenKind::Slash, TokenKind::Integer});
}

TEST(CommentDoesNotBreakLineTracking) {
    DiagnosticBag diagnostics;
    Lexer lexer("/* two\nlines */ x", diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();
    CHECK_EQ(tokens[0].line, std::uint32_t{2});
    CHECK_EQ(tokens[0].column, std::uint32_t{10});
}

TEST(FullProgramScansCleanly) {
    const std::string_view program =
        "let n = input;\n"
        "let acc = 0;\n"
        "while (n > 0) {\n"
        "  acc = acc + n;\n"
        "  n = n - 1;\n"
        "}\n"
        "if (acc == 55) { print true; } else { print acc; }\n";

    DiagnosticBag diagnostics;
    Lexer lexer(program, diagnostics);
    const std::vector<Token> tokens = lexer.tokenize();

    CHECK_EQ(diagnostics.size(), std::size_t{0});
    CHECK(tokens.size() > 40);
    CHECK_EQ(std::string(cairn::to_string(tokens.back().kind)),
             std::string("EndOfFile"));
}

TEST_MAIN()
