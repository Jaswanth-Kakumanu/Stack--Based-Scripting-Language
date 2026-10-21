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

// Parses `source` and returns the S-expression rendering of the tree. Comparing
// against a golden string pins both the shape and the associativity in one
// readable assertion.
std::string parse_to_sexpr(std::string_view source, std::size_t* error_count = nullptr) {
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const std::vector<StmtPtr> program = parser.parse();
    if (error_count != nullptr) {
        *error_count = diagnostics.size();
    }
    return cairn::to_sexpr(program);
}

// Convenience: parse a single expression statement and return just the
// expression's S-expression, stripped of the (program ...) wrapper.
std::string expr_sexpr(std::string_view expression_source) {
    const std::string statement = std::string(expression_source) + ";";
    std::size_t errors = 0;
    const std::string full = parse_to_sexpr(statement, &errors);
    CHECK_EQ(errors, std::size_t{0});
    const std::string prefix = "(program\n  ";
    if (full.size() > prefix.size() + 1) {
        return full.substr(prefix.size(), full.size() - prefix.size() - 1);
    }
    return full;
}

}  // namespace

TEST(EmptyProgramParses) {
    std::size_t errors = 0;
    CHECK_EQ(parse_to_sexpr("", &errors), std::string("(program)"));
    CHECK_EQ(errors, std::size_t{0});
}

TEST(LiteralsAndVariables) {
    CHECK_EQ(expr_sexpr("42"), std::string("42"));
    CHECK_EQ(expr_sexpr("true"), std::string("true"));
    CHECK_EQ(expr_sexpr("false"), std::string("false"));
    CHECK_EQ(expr_sexpr("counter"), std::string("counter"));
    CHECK_EQ(expr_sexpr("input"), std::string("(input)"));
}

TEST(MultiplicationBindsTighterThanAddition) {
    CHECK_EQ(expr_sexpr("1 + 2 * 3"), std::string("(+ 1 (* 2 3))"));
    CHECK_EQ(expr_sexpr("1 * 2 + 3"), std::string("(+ (* 1 2) 3)"));
}

TEST(ComparisonBindsLooserThanArithmetic) {
    CHECK_EQ(expr_sexpr("1 + 2 < 4"), std::string("(< (+ 1 2) 4)"));
}

TEST(EqualityBindsLoosestOfTheBinaryOperators) {
    CHECK_EQ(expr_sexpr("1 < 2 == true"), std::string("(== (< 1 2) true)"));
}

TEST(BinaryOperatorsAreLeftAssociative) {
    CHECK_EQ(expr_sexpr("1 - 2 - 3"), std::string("(- (- 1 2) 3)"));
    CHECK_EQ(expr_sexpr("8 / 4 / 2"), std::string("(/ (/ 8 4) 2)"));
}

TEST(ParenthesesOverridePrecedence) {
    CHECK_EQ(expr_sexpr("(1 + 2) * 3"), std::string("(* (+ 1 2) 3)"));
    // No Grouping node survives: only the tree shape records the parens.
    CHECK_EQ(expr_sexpr("(((7)))"), std::string("7"));
}

TEST(UnaryOperatorsBindTighterThanBinary) {
    CHECK_EQ(expr_sexpr("-1 + 2"), std::string("(+ (- 1) 2)"));
    CHECK_EQ(expr_sexpr("-(1 + 2)"), std::string("(- (+ 1 2))"));
    CHECK_EQ(expr_sexpr("!true"), std::string("(! true)"));
}

TEST(UnaryOperatorsStack) {
    CHECK_EQ(expr_sexpr("--5"), std::string("(- (- 5))"));
    CHECK_EQ(expr_sexpr("!!false"), std::string("(! (! false))"));
}

TEST(AssignmentIsRightAssociative) {
    CHECK_EQ(expr_sexpr("a = b = 3"),
             std::string("(assign a (assign b 3))"));
}

TEST(AssignmentBindsLooserThanComparison) {
    CHECK_EQ(expr_sexpr("a = 1 < 2"), std::string("(assign a (< 1 2))"));
}

TEST(VarDeclaration) {
    std::size_t errors = 0;
    CHECK_EQ(parse_to_sexpr("let x = 1 + 2;", &errors),
             std::string("(program\n  (let x (+ 1 2)))"));
    CHECK_EQ(errors, std::size_t{0});
}

TEST(PrintStatement) {
    std::size_t errors = 0;
    CHECK_EQ(parse_to_sexpr("print 7;", &errors), std::string("(program\n  (print 7))"));
    CHECK_EQ(errors, std::size_t{0});
}

TEST(BlockNesting) {
    std::size_t errors = 0;
    CHECK_EQ(parse_to_sexpr("{ print 1; print 2; }", &errors),
             std::string("(program\n  (block\n    (print 1)\n    (print 2)))"));
    CHECK_EQ(errors, std::size_t{0});
}

TEST(IfWithoutElse) {
    std::size_t errors = 0;
    CHECK_EQ(parse_to_sexpr("if (x) print 1;", &errors),
             std::string("(program\n  (if x\n    (print 1)))"));
    CHECK_EQ(errors, std::size_t{0});
}

TEST(IfWithElse) {
    std::size_t errors = 0;
    CHECK_EQ(parse_to_sexpr("if (x) print 1; else print 2;", &errors),
             std::string("(program\n  (if x\n    (print 1)\n    (print 2)))"));
    CHECK_EQ(errors, std::size_t{0});
}

TEST(DanglingElseBindsToNearestIf) {
    std::size_t errors = 0;
    const std::string tree =
        parse_to_sexpr("if (a) if (b) print 1; else print 2;", &errors);
    CHECK_EQ(errors, std::size_t{0});
    // The else must sit inside the inner if, i.e. the inner if has two branches
    // and the outer has one.
    CHECK_EQ(tree, std::string("(program\n  (if a\n    (if b\n      (print 1)\n"
                               "      (print 2))))"));
}

TEST(WhileLoop) {
    std::size_t errors = 0;
    CHECK_EQ(parse_to_sexpr("while (i < 3) { i = i + 1; }", &errors),
             std::string("(program\n  (while (< i 3)\n    (block\n"
                         "      (assign i (+ i 1)))))"));
    CHECK_EQ(errors, std::size_t{0});
}

TEST(FullProgramParsesWithoutErrors) {
    const std::string_view program =
        "let n = input;\n"
        "let total = 0;\n"
        "while (n > 0) {\n"
        "  total = total + n;\n"
        "  n = n - 1;\n"
        "}\n"
        "if (total == 55) { print true; } else { print total; }\n";

    std::size_t errors = 0;
    const std::string tree = parse_to_sexpr(program, &errors);
    CHECK_EQ(errors, std::size_t{0});
    CHECK(tree.find("(while") != std::string::npos);
    CHECK(tree.find("(if") != std::string::npos);
    CHECK(tree.find("(input)") != std::string::npos);
}

TEST(NodesCarrySourceLocations) {
    const std::string source = "let x = 1;\nprint x;\n";
    DiagnosticBag diagnostics;
    Lexer lexer(source, diagnostics);
    const auto tokens = lexer.tokenize();
    Parser parser(tokens, diagnostics);
    const std::vector<StmtPtr> program = parser.parse();

    CHECK_EQ(program.size(), std::size_t{2});
    if (program.size() == 2) {
        CHECK_EQ(program[0]->loc.line, std::uint32_t{1});
        CHECK_EQ(program[1]->loc.line, std::uint32_t{2});
        CHECK_EQ(program[1]->loc.column, std::uint32_t{1});
    }
}

TEST_MAIN()
