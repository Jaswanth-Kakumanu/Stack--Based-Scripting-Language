#include "cairn/ast.hpp"

#include <string>

namespace cairn {

const char* to_string(UnaryOp op) noexcept {
    switch (op) {
        case UnaryOp::Negate: return "-";
        case UnaryOp::Not:    return "!";
    }
    return "?";
}

const char* to_string(BinaryOp op) noexcept {
    switch (op) {
        case BinaryOp::Add:          return "+";
        case BinaryOp::Sub:          return "-";
        case BinaryOp::Mul:          return "*";
        case BinaryOp::Div:          return "/";
        case BinaryOp::Equal:        return "==";
        case BinaryOp::NotEqual:     return "!=";
        case BinaryOp::Less:         return "<";
        case BinaryOp::LessEqual:    return "<=";
        case BinaryOp::Greater:      return ">";
        case BinaryOp::GreaterEqual: return ">=";
    }
    return "?";
}

namespace {

// Prints the tree as indented S-expressions. Deliberately a visitor rather than
// a virtual print() on each node: keeping traversal out of the node types is
// what lets Phase 3's compiler be another visitor over the same tree.
class SexprPrinter final : public ExprVisitor, public StmtVisitor {
public:
    std::string take() { return std::move(out_); }

    void print(const Stmt& stmt) { stmt.accept(*this); }
    void print(const Expr& expr) { expr.accept(*this); }

    // --- expressions -------------------------------------------------------

    void visit(const IntLiteralExpr& node) override {
        out_ += std::to_string(node.value);
    }

    void visit(const BoolLiteralExpr& node) override {
        out_ += node.value ? "true" : "false";
    }

    void visit(const VariableExpr& node) override { out_.append(node.name); }

    void visit(const InputExpr&) override { out_ += "(input)"; }

    void visit(const UnaryExpr& node) override {
        out_ += '(';
        out_ += to_string(node.op);
        out_ += ' ';
        node.operand->accept(*this);
        out_ += ')';
    }

    void visit(const BinaryExpr& node) override {
        out_ += '(';
        out_ += to_string(node.op);
        out_ += ' ';
        node.left->accept(*this);
        out_ += ' ';
        node.right->accept(*this);
        out_ += ')';
    }

    void visit(const AssignExpr& node) override {
        out_ += "(assign ";
        out_.append(node.name);
        out_ += ' ';
        node.value->accept(*this);
        out_ += ')';
    }

    // --- statements --------------------------------------------------------

    void visit(const VarDeclStmt& node) override {
        indent();
        out_ += "(let ";
        out_.append(node.name);
        out_ += ' ';
        node.initializer->accept(*this);
        out_ += ')';
    }

    void visit(const PrintStmt& node) override {
        indent();
        out_ += "(print ";
        node.value->accept(*this);
        out_ += ')';
    }

    void visit(const ExpressionStmt& node) override {
        indent();
        node.expression->accept(*this);
    }

    void visit(const BlockStmt& node) override {
        indent();
        out_ += "(block";
        ++depth_;
        for (const StmtPtr& stmt : node.statements) {
            out_ += '\n';
            stmt->accept(*this);
        }
        --depth_;
        out_ += ')';
    }

    void visit(const IfStmt& node) override {
        indent();
        out_ += "(if ";
        node.condition->accept(*this);
        ++depth_;
        out_ += '\n';
        node.then_branch->accept(*this);
        if (node.else_branch) {
            out_ += '\n';
            node.else_branch->accept(*this);
        }
        --depth_;
        out_ += ')';
    }

    void visit(const WhileStmt& node) override {
        indent();
        out_ += "(while ";
        node.condition->accept(*this);
        ++depth_;
        out_ += '\n';
        node.body->accept(*this);
        --depth_;
        out_ += ')';
    }

private:
    void indent() { out_.append(static_cast<std::size_t>(depth_) * 2, ' '); }

    std::string out_;
    int depth_ = 0;
};

}  // namespace

std::string to_sexpr(const std::vector<StmtPtr>& program) {
    SexprPrinter printer;
    std::string out = "(program";
    for (const StmtPtr& stmt : program) {
        SexprPrinter inner;
        inner.print(*stmt);
        out += '\n';
        out += "  ";
        // Re-indent nested lines by two spaces so the whole program nests under
        // the (program ...) head.
        const std::string body = inner.take();
        for (const char c : body) {
            out += c;
            if (c == '\n') {
                out += "  ";
            }
        }
    }
    out += ')';
    return out;
}

std::string to_sexpr(const Expr& expr) {
    SexprPrinter printer;
    printer.print(expr);
    return printer.take();
}

}  // namespace cairn
