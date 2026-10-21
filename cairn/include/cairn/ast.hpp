#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cairn {

// Where a node started in the source, kept so Phase 3 and Phase 4 can report
// runtime and type errors against real positions long after the tokens are gone.
struct SourceLocation {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

enum class UnaryOp : std::uint8_t {
    Negate,  // -x
    Not,     // !x
};

enum class BinaryOp : std::uint8_t {
    Add, Sub, Mul, Div,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
};

const char* to_string(UnaryOp op) noexcept;
const char* to_string(BinaryOp op) noexcept;

// Node tags.
//
// These exist so that structural questions ("is this expression a valid
// assignment target?") are answered with a byte compare instead of dynamic_cast.
// RTTI is off the table in the kind of codebase this project is practice for.
enum class ExprKind : std::uint8_t {
    IntLiteral, BoolLiteral, Variable, Input, Unary, Binary, Assign,
};

enum class StmtKind : std::uint8_t {
    VarDecl, Print, ExprStmt, Block, If, While,
};

struct IntLiteralExpr;
struct BoolLiteralExpr;
struct VariableExpr;
struct InputExpr;
struct UnaryExpr;
struct BinaryExpr;
struct AssignExpr;

struct ExprVisitor {
    virtual ~ExprVisitor() = default;
    virtual void visit(const IntLiteralExpr&) = 0;
    virtual void visit(const BoolLiteralExpr&) = 0;
    virtual void visit(const VariableExpr&) = 0;
    virtual void visit(const InputExpr&) = 0;
    virtual void visit(const UnaryExpr&) = 0;
    virtual void visit(const BinaryExpr&) = 0;
    virtual void visit(const AssignExpr&) = 0;
};

struct Expr {
    ExprKind kind;
    SourceLocation loc;

    explicit Expr(ExprKind k, SourceLocation l) noexcept : kind(k), loc(l) {}
    virtual ~Expr() = default;
    virtual void accept(ExprVisitor& visitor) const = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteralExpr final : Expr {
    std::int64_t value;

    IntLiteralExpr(std::int64_t v, SourceLocation l) noexcept
        : Expr(ExprKind::IntLiteral, l), value(v) {}
    void accept(ExprVisitor& visitor) const override { visitor.visit(*this); }
};

struct BoolLiteralExpr final : Expr {
    bool value;

    BoolLiteralExpr(bool v, SourceLocation l) noexcept
        : Expr(ExprKind::BoolLiteral, l), value(v) {}
    void accept(ExprVisitor& visitor) const override { visitor.visit(*this); }
};

struct VariableExpr final : Expr {
    // A view into the source buffer, exactly as the Token carried it.
    std::string_view name;

    VariableExpr(std::string_view n, SourceLocation l) noexcept
        : Expr(ExprKind::Variable, l), name(n) {}
    void accept(ExprVisitor& visitor) const override { visitor.visit(*this); }
};

struct InputExpr final : Expr {
    explicit InputExpr(SourceLocation l) noexcept : Expr(ExprKind::Input, l) {}
    void accept(ExprVisitor& visitor) const override { visitor.visit(*this); }
};

struct UnaryExpr final : Expr {
    UnaryOp op;
    ExprPtr operand;

    UnaryExpr(UnaryOp o, ExprPtr operand_, SourceLocation l) noexcept
        : Expr(ExprKind::Unary, l), op(o), operand(std::move(operand_)) {}
    void accept(ExprVisitor& visitor) const override { visitor.visit(*this); }
};

struct BinaryExpr final : Expr {
    BinaryOp op;
    ExprPtr left;
    ExprPtr right;

    BinaryExpr(BinaryOp o, ExprPtr l_, ExprPtr r_, SourceLocation l) noexcept
        : Expr(ExprKind::Binary, l), op(o), left(std::move(l_)), right(std::move(r_)) {}
    void accept(ExprVisitor& visitor) const override { visitor.visit(*this); }
};

struct AssignExpr final : Expr {
    std::string_view name;
    ExprPtr value;

    AssignExpr(std::string_view n, ExprPtr v, SourceLocation l) noexcept
        : Expr(ExprKind::Assign, l), name(n), value(std::move(v)) {}
    void accept(ExprVisitor& visitor) const override { visitor.visit(*this); }
};

struct VarDeclStmt;
struct PrintStmt;
struct ExpressionStmt;
struct BlockStmt;
struct IfStmt;
struct WhileStmt;

struct StmtVisitor {
    virtual ~StmtVisitor() = default;
    virtual void visit(const VarDeclStmt&) = 0;
    virtual void visit(const PrintStmt&) = 0;
    virtual void visit(const ExpressionStmt&) = 0;
    virtual void visit(const BlockStmt&) = 0;
    virtual void visit(const IfStmt&) = 0;
    virtual void visit(const WhileStmt&) = 0;
};

struct Stmt {
    StmtKind kind;
    SourceLocation loc;

    explicit Stmt(StmtKind k, SourceLocation l) noexcept : kind(k), loc(l) {}
    virtual ~Stmt() = default;
    virtual void accept(StmtVisitor& visitor) const = 0;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct VarDeclStmt final : Stmt {
    std::string_view name;
    ExprPtr initializer;

    VarDeclStmt(std::string_view n, ExprPtr init, SourceLocation l) noexcept
        : Stmt(StmtKind::VarDecl, l), name(n), initializer(std::move(init)) {}
    void accept(StmtVisitor& visitor) const override { visitor.visit(*this); }
};

struct PrintStmt final : Stmt {
    ExprPtr value;

    PrintStmt(ExprPtr v, SourceLocation l) noexcept
        : Stmt(StmtKind::Print, l), value(std::move(v)) {}
    void accept(StmtVisitor& visitor) const override { visitor.visit(*this); }
};

struct ExpressionStmt final : Stmt {
    ExprPtr expression;

    ExpressionStmt(ExprPtr e, SourceLocation l) noexcept
        : Stmt(StmtKind::ExprStmt, l), expression(std::move(e)) {}
    void accept(StmtVisitor& visitor) const override { visitor.visit(*this); }
};

struct BlockStmt final : Stmt {
    std::vector<StmtPtr> statements;

    BlockStmt(std::vector<StmtPtr> stmts, SourceLocation l) noexcept
        : Stmt(StmtKind::Block, l), statements(std::move(stmts)) {}
    void accept(StmtVisitor& visitor) const override { visitor.visit(*this); }
};

struct IfStmt final : Stmt {
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch;  // null when there is no else

    IfStmt(ExprPtr cond, StmtPtr then_, StmtPtr else_, SourceLocation l) noexcept
        : Stmt(StmtKind::If, l), condition(std::move(cond)),
          then_branch(std::move(then_)), else_branch(std::move(else_)) {}
    void accept(StmtVisitor& visitor) const override { visitor.visit(*this); }
};

struct WhileStmt final : Stmt {
    ExprPtr condition;
    StmtPtr body;

    WhileStmt(ExprPtr cond, StmtPtr body_, SourceLocation l) noexcept
        : Stmt(StmtKind::While, l), condition(std::move(cond)), body(std::move(body_)) {}
    void accept(StmtVisitor& visitor) const override { visitor.visit(*this); }
};

// Renders a program as indented S-expressions. This is the `--ast` output and
// the primary way the parser is inspected by hand.
std::string to_sexpr(const std::vector<StmtPtr>& program);
std::string to_sexpr(const Expr& expr);

}  // namespace cairn
