#pragma once
#include "ast.hpp"
#include <iostream>
#include <string>

namespace dux::ast {

class Printer final : public Visitor {
public:
    explicit Printer(std::ostream& out = std::cout) : out_(out) {}

    void print(const Program& p) { p.accept(*this); }

private:
    std::ostream& out_;
    int           indent_{0};

    void indent()   { ++indent_; }
    void dedent()   { --indent_; }
    std::string pad() const { return std::string(indent_ * 2, ' '); }

    void line(std::string_view s) { out_ << pad() << s << '\n'; }

    // ── Expressions ──────────────────────────────────────────────────────────
    void visit(const IntLitExpr& n) override    { out_ << n.value; }
    void visit(const FloatLitExpr& n) override  { out_ << n.value; }
    void visit(const BoolLitExpr& n) override   { out_ << (n.value ? "true" : "false"); }
    void visit(const NullLitExpr&) override     { out_ << "null"; }
    void visit(const ThisExpr&) override        { out_ << "this"; }
    void visit(const SuperExpr&) override       { out_ << "super"; }
    void visit(const IdentExpr& n) override     { out_ << n.name; }

    void visit(const StringLitExpr& n) override {
        out_ << '"' << n.value << '"';
    }

    void visit(const BinaryExpr& n) override {
        out_ << '(';
        n.left->accept(*this);
        out_ << ' ' << n.op << ' ';
        n.right->accept(*this);
        out_ << ')';
    }

    void visit(const UnaryExpr& n) override {
        if (n.prefix) { out_ << n.op; n.operand->accept(*this); }
        else          { n.operand->accept(*this); out_ << n.op; }
    }

    void visit(const AssignExpr& n) override {
        n.target->accept(*this);
        out_ << ' ' << n.op << ' ';
        n.value->accept(*this);
    }

    void visit(const CallExpr& n) override {
        n.callee->accept(*this);
        out_ << '(';
        for (std::size_t i = 0; i < n.args.size(); ++i) {
            if (i) out_ << ", ";
            n.args[i]->accept(*this);
        }
        out_ << ')';
    }

    void visit(const MemberExpr& n) override {
        n.object->accept(*this);
        out_ << '.' << n.member;
    }

    void visit(const IndexExpr& n) override {
        n.object->accept(*this);
        out_ << '[';
        n.index->accept(*this);
        out_ << ']';
    }

    void visit(const NewExpr& n) override {
        out_ << "new " << n.type.name << '(';
        for (std::size_t i = 0; i < n.args.size(); ++i) {
            if (i) out_ << ", ";
            n.args[i]->accept(*this);
        }
        out_ << ')';
    }

    void visit(const FormatExpr& n) override {
        n.tmpl->accept(*this);
        out_ << " % ";
        n.args->accept(*this);
    }

    void visit(const ListExpr& n) override {
        out_ << '[';
        for (std::size_t i = 0; i < n.elements.size(); ++i) {
            if (i) out_ << ", ";
            n.elements[i]->accept(*this);
        }
        out_ << ']';
    }

    void visit(const DictExpr& n) override {
        out_ << '{';
        for (std::size_t i = 0; i < n.pairs.size(); ++i) {
            if (i) out_ << ", ";
            n.pairs[i].first->accept(*this);
            out_ << ": ";
            n.pairs[i].second->accept(*this);
        }
        out_ << '}';
    }

    // ── Statements ───────────────────────────────────────────────────────────
    void visit(const BlockStmt& n) override {
        out_ << pad() << "{\n";
        indent();
        for (auto& s : n.body) s->accept(*this);
        dedent();
        out_ << pad() << "}\n";
    }

    void visit(const ExprStmt& n) override {
        out_ << pad();
        n.expr->accept(*this);
        out_ << '\n';
    }

    void visit(const VarDeclStmt& n) override {
        out_ << pad();
        if (n.is_const) out_ << "const ";
        out_ << n.type.name;
        for (std::size_t i = 0; i < n.decls.size(); ++i) {
            if (i) out_ << ',';
            out_ << ' ' << n.decls[i].first;
            if (n.decls[i].second) {
                out_ << " = ";
                n.decls[i].second->accept(*this);
            }
        }
        out_ << '\n';
    }

    void visit(const IfStmt& n) override {
        out_ << pad() << "if (";
        n.cond->accept(*this);
        out_ << ")\n";
        indent(); n.then_br->accept(*this); dedent();
        if (n.else_br) {
            out_ << pad() << "else\n";
            indent(); n.else_br->accept(*this); dedent();
        }
    }

    void visit(const WhileStmt& n) override {
        out_ << pad();
        if (n.label) out_ << '&' << *n.label << ' ';
        out_ << "while (";
        n.cond->accept(*this);
        out_ << ")\n";
        indent(); n.body->accept(*this); dedent();
    }

    void visit(const DoWhileStmt& n) override {
        out_ << pad() << "do\n";
        indent(); n.body->accept(*this); dedent();
        out_ << pad() << "while (";
        n.cond->accept(*this);
        out_ << ")\n";
    }

    void visit(const ForInStmt& n) override {
        out_ << pad() << "for " << n.var_type.name << ' ' << n.var_name << " in ";
        n.iterable->accept(*this);
        out_ << '\n';
        indent(); n.body->accept(*this); dedent();
    }

    void visit(const ForCStmt& n) override {
        out_ << pad() << "for " << n.var_type.name << ' ' << n.var_name << " = ";
        n.init->accept(*this);
        out_ << ", ";
        n.cond->accept(*this);
        out_ << ", ";
        n.incr->accept(*this);
        out_ << '\n';
        indent(); n.body->accept(*this); dedent();
    }

    void visit(const SwitchStmt& n) override {
        out_ << pad() << "switch (";
        n.expr->accept(*this);
        out_ << ") {\n";
        indent();
        for (auto& c : n.cases) {
            if (c.value) { out_ << pad() << "case "; (*c.value)->accept(*this); out_ << ":\n"; }
            else           out_ << pad() << "default:\n";
            indent();
            for (auto& s : c.body) s->accept(*this);
            dedent();
        }
        dedent();
        out_ << pad() << "}\n";
    }

    void visit(const TryCatchStmt& n) override {
        out_ << pad() << "try\n";
        indent(); n.try_body->accept(*this); dedent();
        out_ << pad() << "catch";
        if (n.catch_all)        out_ << " ...";
        else if (n.catch_type)  out_ << " (" << n.catch_type->name << (n.catch_var ? " " + *n.catch_var : "") << ')';
        out_ << '\n';
        indent(); n.catch_body->accept(*this); dedent();
    }

    void visit(const ReturnStmt& n) override {
        out_ << pad() << "return";
        if (n.value) { out_ << ' '; (*n.value)->accept(*this); }
        out_ << '\n';
    }

    void visit(const BreakStmt& n) override {
        out_ << pad() << "break";
        if (n.label) out_ << " &" << *n.label;
        out_ << '\n';
    }

    void visit(const ContinueStmt& n) override {
        out_ << pad() << "continue";
        if (n.label) out_ << " &" << *n.label;
        out_ << '\n';
    }

    void visit(const AssertStmt& n) override {
        out_ << pad() << "assert(";
        n.cond->accept(*this);
        out_ << ")\n";
    }

    void visit(const DeleteStmt& n) override {
        out_ << pad() << "delete ";
        n.expr->accept(*this);
        out_ << '\n';
    }

    void visit(const LabeledStmt& n) override {
        out_ << pad() << '&' << n.label << ' ';
        n.stmt->accept(*this);
    }

    // ── Declarations ─────────────────────────────────────────────────────────
    static const char* access_str(AccessMod m) {
        switch (m) {
        case AccessMod::Public:    return "public";
        case AccessMod::Private:   return "private";
        case AccessMod::Protected: return "protected";
        default:                   return "";
        }
    }

    void visit(const FunctionDecl& n) override {
        out_ << pad();
        if (n.is_dtor) out_ << '~';
        if (!n.is_ctor && !n.is_dtor) out_ << n.return_type.name << ' ';
        out_ << n.name << '(';
        for (std::size_t i = 0; i < n.params.size(); ++i) {
            if (i) out_ << ", ";
            out_ << n.params[i].type.name << ' ' << n.params[i].name;
        }
        out_ << ')';
        if (n.modifier) out_ << ' ' << *n.modifier;
        if (n.body) {
            out_ << '\n';
            indent(); n.body->accept(*this); dedent();
        } else {
            out_ << '\n';
        }
    }

    void visit(const FieldDecl& n) override {
        out_ << pad() << n.type.name << ' ' << n.name;
        if (n.init) { out_ << " = "; (*n.init)->accept(*this); }
        out_ << '\n';
    }

    void visit(const ClassDecl& n) override {
        out_ << pad() << "class " << n.name;
        if (!n.bases.empty()) {
            out_ << '(';
            for (std::size_t i = 0; i < n.bases.size(); ++i) {
                if (i) out_ << ", ";
                if (n.bases[i].access != AccessMod::None)
                    out_ << access_str(n.bases[i].access) << ' ';
                out_ << n.bases[i].name;
            }
            out_ << ')';
        }
        out_ << " {\n";
        indent();
        for (auto& m : n.members) {
            if (m.access != AccessMod::None && !m.decl) {
                out_ << pad() << access_str(m.access) << ":\n";
                continue;
            }
            if (m.decl) m.decl->accept(*this);
        }
        dedent();
        out_ << pad() << "}\n";
    }

    void visit(const InterfaceDecl& n) override {
        out_ << pad() << "interface " << n.name << " {\n";
        indent();
        for (auto& m : n.members) {
            if (m.decl) m.decl->accept(*this);
        }
        dedent();
        out_ << pad() << "}\n";
    }

    void visit(const ImportDecl& n) override {
        if (n.global_scope) {
            out_ << pad() << "import { ";
            for (std::size_t i = 0; i < n.symbols.size(); ++i) {
                if (i) out_ << ", ";
                out_ << n.symbols[i];
            }
            out_ << " } from " << n.path << '\n';
        } else if (!n.symbols.empty()) {
            out_ << pad() << "import " << n.path << "::{";
            for (std::size_t i = 0; i < n.symbols.size(); ++i) {
                if (i) out_ << ", ";
                out_ << n.symbols[i];
            }
            out_ << '}';
            if (!n.alias.empty()) out_ << " as " << n.alias;
            out_ << '\n';
        } else {
            out_ << pad() << "import " << n.path;
            if (!n.alias.empty()) out_ << " as " << n.alias;
            out_ << '\n';
        }
    }

    void visit(const NamespaceDecl& n) override {
        if (n.is_package_decl) {
            out_ << pad() << "namespace " << n.name << '\n';
            return;
        }
        out_ << pad() << "namespace " << n.name << " {\n";
        indent();
        for (auto& d : n.decls)  d->accept(*this);
        for (auto& s : n.stmts)  s->accept(*this);
        dedent();
        out_ << pad() << "}\n";
    }

    void visit(const Program& n) override {
        for (auto& d : n.decls) d->accept(*this);
        for (auto& s : n.stmts) s->accept(*this);
    }
};

} // namespace dux::ast
