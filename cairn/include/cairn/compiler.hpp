#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "cairn/ast.hpp"
#include "cairn/chunk.hpp"
#include "cairn/diagnostic.hpp"

namespace cairn {

// Single-pass AST-to-bytecode compiler.
//
// Walks the tree once as a visitor and emits instructions as it goes. No
// intermediate representation, no optimisation passes: the AST is the IR.
//
// The one non-obvious job here is variable resolution. Every `let` is assigned
// a stack slot at compile time, so a variable reference emits GET_LOCAL with a
// slot index. Nothing looks up a name at runtime - no hash map, no string
// compare in the dispatch loop.
class Compiler final : public ExprVisitor, public StmtVisitor {
public:
    explicit Compiler(DiagnosticBag& diagnostics) noexcept;

    // Returns nullopt if any semantic error was reported (undefined variable,
    // redeclaration, a jump too far to encode).
    std::optional<Chunk> compile(const std::vector<StmtPtr>& program);

    // Slots are u16, and the operand encoding is what sets this ceiling.
    static constexpr std::size_t kMaxLocals = 65535;

private:
    struct Local {
        std::string_view name;
        int depth = 0;
    };

    void emit(OpCode op, const SourceLocation& loc);
    void emit_u16(std::uint16_t operand, const SourceLocation& loc);
    // Emits a jump with a placeholder operand; returns the operand's offset.
    std::size_t emit_jump(OpCode op, const SourceLocation& loc);
    void patch_jump(std::size_t operand_offset, const SourceLocation& loc);
    void emit_loop(std::size_t loop_start, const SourceLocation& loc);

    void begin_scope() noexcept;
    void end_scope(const SourceLocation& loc);
    void declare_local(std::string_view name, const SourceLocation& loc);
    // Returns the slot, or -1 when the name is not in scope.
    int resolve_local(std::string_view name) const noexcept;

    void error(const SourceLocation& loc, const std::string& message);

    void compile_stmt(const Stmt& stmt);
    void compile_expr(const Expr& expr);

    void visit(const IntLiteralExpr&) override;
    void visit(const BoolLiteralExpr&) override;
    void visit(const VariableExpr&) override;
    void visit(const InputExpr&) override;
    void visit(const UnaryExpr&) override;
    void visit(const BinaryExpr&) override;
    void visit(const AssignExpr&) override;

    void visit(const VarDeclStmt&) override;
    void visit(const PrintStmt&) override;
    void visit(const ExpressionStmt&) override;
    void visit(const BlockStmt&) override;
    void visit(const IfStmt&) override;
    void visit(const WhileStmt&) override;

    Chunk chunk_;
    DiagnosticBag& diagnostics_;
    std::vector<Local> locals_;
    int scope_depth_ = 0;
    bool failed_ = false;
};

}  // namespace cairn
