#include "cairn/compiler.hpp"

#include <limits>
#include <string>
#include <utility>

namespace cairn {
namespace {

OpCode opcode_for(BinaryOp op) noexcept {
    switch (op) {
        case BinaryOp::Add:          return OpCode::Add;
        case BinaryOp::Sub:          return OpCode::Sub;
        case BinaryOp::Mul:          return OpCode::Mul;
        case BinaryOp::Div:          return OpCode::Div;
        case BinaryOp::Equal:        return OpCode::Equal;
        case BinaryOp::NotEqual:     return OpCode::NotEqual;
        case BinaryOp::Less:         return OpCode::Less;
        case BinaryOp::LessEqual:    return OpCode::LessEqual;
        case BinaryOp::Greater:      return OpCode::Greater;
        case BinaryOp::GreaterEqual: return OpCode::GreaterEqual;
    }
    return OpCode::Add;
}

}  // namespace

Compiler::Compiler(DiagnosticBag& diagnostics) noexcept : diagnostics_(diagnostics) {}

void Compiler::error(const SourceLocation& loc, const std::string& message) {
    // Unlike the parser, the compiler does not unwind. A semantic error is
    // local to one node, and the surrounding tree is still walkable, so it
    // records the problem and keeps going to find the rest in one run.
    diagnostics_.error(message, loc.line, loc.column);
    failed_ = true;
}

void Compiler::emit(OpCode op, const SourceLocation& loc) { chunk_.write(op, loc.line); }

void Compiler::emit_u16(std::uint16_t operand, const SourceLocation& loc) {
    chunk_.write_u16(operand, loc.line);
}

std::size_t Compiler::emit_jump(OpCode op, const SourceLocation& loc) {
    emit(op, loc);
    const std::size_t operand_offset = chunk_.size();
    emit_u16(0xFFFF, loc);  // placeholder, backpatched once the target is known
    return operand_offset;
}

void Compiler::patch_jump(std::size_t operand_offset, const SourceLocation& loc) {
    // Offsets are relative to the byte after the operand, which keeps them
    // small and makes the encoded chunk position-independent.
    const std::size_t target = chunk_.size();
    const std::size_t from = operand_offset + 2;
    const std::size_t distance = target - from;

    if (distance > std::numeric_limits<std::uint16_t>::max()) {
        error(loc, "branch target is too far away to encode (limit 65535 bytes)");
        return;
    }
    chunk_.patch_u16(operand_offset, static_cast<std::uint16_t>(distance));
}

void Compiler::emit_loop(std::size_t loop_start, const SourceLocation& loc) {
    emit(OpCode::Loop, loc);
    const std::size_t from = chunk_.size() + 2;
    const std::size_t distance = from - loop_start;

    if (distance > std::numeric_limits<std::uint16_t>::max()) {
        error(loc, "loop body is too large to encode (limit 65535 bytes)");
        emit_u16(0, loc);
        return;
    }
    emit_u16(static_cast<std::uint16_t>(distance), loc);
}

void Compiler::begin_scope() noexcept { ++scope_depth_; }

void Compiler::end_scope(const SourceLocation& loc) {
    --scope_depth_;
    // Locals live on the VM's value stack. Leaving a scope means popping the
    // slots it introduced, which is why a block emits one POP per declaration.
    while (!locals_.empty() && locals_.back().depth > scope_depth_) {
        emit(OpCode::Pop, loc);
        locals_.pop_back();
    }
}

void Compiler::declare_local(std::string_view name, const SourceLocation& loc) {
    for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
        if (it->depth < scope_depth_) {
            break;  // shadowing an outer scope is allowed
        }
        if (it->name == name) {
            error(loc, "variable '" + std::string(name) +
                           "' is already declared in this scope");
            return;
        }
    }

    if (locals_.size() >= kMaxLocals) {
        error(loc, "too many variables in scope (limit 65535)");
        return;
    }
    locals_.push_back(Local{name, scope_depth_});
}

int Compiler::resolve_local(std::string_view name) const noexcept {
    // Backwards, so an inner declaration shadows an outer one with the same name.
    for (std::size_t i = locals_.size(); i > 0; --i) {
        if (locals_[i - 1].name == name) {
            return static_cast<int>(i - 1);
        }
    }
    return -1;
}

void Compiler::compile_stmt(const Stmt& stmt) { stmt.accept(*this); }
void Compiler::compile_expr(const Expr& expr) { expr.accept(*this); }

std::optional<Chunk> Compiler::compile(const std::vector<StmtPtr>& program) {
    for (const StmtPtr& stmt : program) {
        compile_stmt(*stmt);
    }

    const SourceLocation end = program.empty() ? SourceLocation{}
                                               : program.back()->loc;
    emit(OpCode::Halt, end);

    if (failed_) {
        return std::nullopt;
    }
    return std::move(chunk_);
}

// --- expressions -----------------------------------------------------------

void Compiler::visit(const IntLiteralExpr& node) {
    const std::uint32_t index = chunk_.add_constant(Value::integer(node.value));
    if (index == Chunk::kInvalidConstant) {
        error(node.loc, "too many distinct constants in the program (limit 65536)");
        return;
    }
    emit(OpCode::Constant, node.loc);
    emit_u16(static_cast<std::uint16_t>(index), node.loc);
}

void Compiler::visit(const BoolLiteralExpr& node) {
    // true/false get dedicated opcodes rather than pool entries: one byte
    // instead of three, and no pool load in the dispatch loop.
    emit(node.value ? OpCode::True : OpCode::False, node.loc);
}

void Compiler::visit(const VariableExpr& node) {
    const int slot = resolve_local(node.name);
    if (slot < 0) {
        error(node.loc, "undefined variable '" + std::string(node.name) + "'");
        return;
    }
    emit(OpCode::GetLocal, node.loc);
    emit_u16(static_cast<std::uint16_t>(slot), node.loc);
}

void Compiler::visit(const InputExpr& node) { emit(OpCode::Input, node.loc); }

void Compiler::visit(const UnaryExpr& node) {
    compile_expr(*node.operand);
    emit(node.op == UnaryOp::Negate ? OpCode::Negate : OpCode::Not, node.loc);
}

void Compiler::visit(const BinaryExpr& node) {
    // Post-order: both operands are on the stack before the operator runs.
    // This is the entire reason a stack machine needs no register allocator.
    compile_expr(*node.left);
    compile_expr(*node.right);
    emit(opcode_for(node.op), node.loc);
}

void Compiler::visit(const AssignExpr& node) {
    const int slot = resolve_local(node.name);
    if (slot < 0) {
        error(node.loc, "undefined variable '" + std::string(node.name) + "'");
        return;
    }
    compile_expr(*node.value);
    // SET_LOCAL leaves the value on the stack, because assignment is an
    // expression: `a = b = 1` needs the inner result. Statement context pops it.
    emit(OpCode::SetLocal, node.loc);
    emit_u16(static_cast<std::uint16_t>(slot), node.loc);
}

// --- statements ------------------------------------------------------------

void Compiler::visit(const VarDeclStmt& node) {
    compile_expr(*node.initializer);
    // No store instruction: the initializer's result is already on top of the
    // stack, and that position becomes the variable's slot.
    declare_local(node.name, node.loc);
}

void Compiler::visit(const PrintStmt& node) {
    compile_expr(*node.value);
    emit(OpCode::Print, node.loc);
}

void Compiler::visit(const ExpressionStmt& node) {
    compile_expr(*node.expression);
    emit(OpCode::Pop, node.loc);  // a statement leaves the stack as it found it
}

void Compiler::visit(const BlockStmt& node) {
    begin_scope();
    for (const StmtPtr& stmt : node.statements) {
        compile_stmt(*stmt);
    }
    end_scope(node.loc);
}

void Compiler::visit(const IfStmt& node) {
    compile_expr(*node.condition);

    const std::size_t else_jump = emit_jump(OpCode::JumpIfFalse, node.loc);
    compile_stmt(*node.then_branch);

    if (node.else_branch) {
        const std::size_t end_jump = emit_jump(OpCode::Jump, node.loc);
        patch_jump(else_jump, node.loc);
        compile_stmt(*node.else_branch);
        patch_jump(end_jump, node.loc);
    } else {
        patch_jump(else_jump, node.loc);
    }
}

void Compiler::visit(const WhileStmt& node) {
    const std::size_t loop_start = chunk_.size();
    compile_expr(*node.condition);

    const std::size_t exit_jump = emit_jump(OpCode::JumpIfFalse, node.loc);
    compile_stmt(*node.body);
    emit_loop(loop_start, node.loc);
    patch_jump(exit_jump, node.loc);
}

}  // namespace cairn
