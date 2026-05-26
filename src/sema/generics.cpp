#include "sema/generics.hpp"
#include "driver/driver.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace dux::sema {

using namespace ast;

// ─── Mangling ─────────────────────────────────────────────────────────────────

std::string mangle_generic(const std::string& name,
                            const std::vector<TypeExpr>& args) {
    std::string result = name;
    for (const auto& a : args) {
        result += "__";
        if (!a.type_args.empty())
            result += mangle_generic(a.name, a.type_args);
        else
            result += a.name;
    }
    return result;
}

// ─── Substitution ─────────────────────────────────────────────────────────────
// Substitution map: type param name → concrete TypeExpr
using SubstMap = std::unordered_map<std::string, TypeExpr>;

static TypeExpr subst_te(const TypeExpr& te, const SubstMap& s) {
    // If it's a type param reference, substitute
    if (te.name != "__fn" && te.type_args.empty() && s.count(te.name)) {
        TypeExpr r = s.at(te.name);
        r.is_const = r.is_const || te.is_const;
        return r;
    }
    TypeExpr r = te;
    for (auto& fp : r.fn_params) fp = subst_te(fp, s);
    for (auto& ta : r.type_args) ta = subst_te(ta, s);
    return r;
}

// ─── AST deep-clone + substitute ─────────────────────────────────────────────

static ExprPtr  clone_expr (const Expr* e,  const SubstMap& s);
static StmtPtr  clone_stmt (const Stmt* st, const SubstMap& s);
static StmtList clone_stmts(const StmtList& stmts, const SubstMap& s);

static ExprPtr clone_expr(const Expr* e, const SubstMap& s) {
    if (!e) return nullptr;

    if (auto* n = dynamic_cast<const IntLitExpr*>(e)) {
        auto r = std::make_unique<IntLitExpr>(); r->loc = n->loc; r->value = n->value; return r;
    }
    if (auto* n = dynamic_cast<const FloatLitExpr*>(e)) {
        auto r = std::make_unique<FloatLitExpr>(); r->loc = n->loc; r->value = n->value; return r;
    }
    if (auto* n = dynamic_cast<const LongLitExpr*>(e)) {
        auto r = std::make_unique<LongLitExpr>(); r->loc = n->loc; r->value = n->value; return r;
    }
    if (auto* n = dynamic_cast<const RealLitExpr*>(e)) {
        auto r = std::make_unique<RealLitExpr>(); r->loc = n->loc; r->value = n->value; return r;
    }
    if (auto* n = dynamic_cast<const StringLitExpr*>(e)) {
        auto r = std::make_unique<StringLitExpr>(); r->loc = n->loc; r->value = n->value; return r;
    }
    if (auto* n = dynamic_cast<const BoolLitExpr*>(e)) {
        auto r = std::make_unique<BoolLitExpr>(); r->loc = n->loc; r->value = n->value; return r;
    }
    if (dynamic_cast<const NullLitExpr*>(e)) {
        auto r = std::make_unique<NullLitExpr>(); r->loc = e->loc; return r;
    }
    if (auto* n = dynamic_cast<const IdentExpr*>(e)) {
        auto r = std::make_unique<IdentExpr>(); r->loc = n->loc; r->name = n->name; return r;
    }
    if (dynamic_cast<const ThisExpr*>(e)) {
        auto r = std::make_unique<ThisExpr>(); r->loc = e->loc; return r;
    }
    if (dynamic_cast<const SuperExpr*>(e)) {
        auto r = std::make_unique<SuperExpr>(); r->loc = e->loc; return r;
    }
    if (auto* n = dynamic_cast<const BinaryExpr*>(e)) {
        auto r = std::make_unique<BinaryExpr>();
        r->loc = n->loc; r->op = n->op;
        r->left  = clone_expr(n->left.get(),  s);
        r->right = clone_expr(n->right.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const UnaryExpr*>(e)) {
        auto r = std::make_unique<UnaryExpr>();
        r->loc = n->loc; r->op = n->op; r->prefix = n->prefix;
        r->operand = clone_expr(n->operand.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const AssignExpr*>(e)) {
        auto r = std::make_unique<AssignExpr>();
        r->loc = n->loc; r->op = n->op;
        r->target = clone_expr(n->target.get(), s);
        r->value  = clone_expr(n->value.get(),  s);
        return r;
    }
    if (auto* n = dynamic_cast<const CallExpr*>(e)) {
        auto r = std::make_unique<CallExpr>();
        r->loc = n->loc;
        r->callee = clone_expr(n->callee.get(), s);
        for (const auto& a : n->args) r->args.push_back(clone_expr(a.get(), s));
        return r;
    }
    if (auto* n = dynamic_cast<const MemberExpr*>(e)) {
        auto r = std::make_unique<MemberExpr>();
        r->loc = n->loc; r->member = n->member;
        r->object = clone_expr(n->object.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const IndexExpr*>(e)) {
        auto r = std::make_unique<IndexExpr>();
        r->loc = n->loc;
        r->object = clone_expr(n->object.get(), s);
        r->index  = clone_expr(n->index.get(),  s);
        return r;
    }
    if (auto* n = dynamic_cast<const NewExpr*>(e)) {
        auto r = std::make_unique<NewExpr>();
        r->loc  = n->loc;
        r->type = subst_te(n->type, s);
        for (const auto& a : n->args) r->args.push_back(clone_expr(a.get(), s));
        return r;
    }
    if (auto* n = dynamic_cast<const ListExpr*>(e)) {
        auto r = std::make_unique<ListExpr>(); r->loc = n->loc;
        for (const auto& el : n->elements) r->elements.push_back(clone_expr(el.get(), s));
        return r;
    }
    if (auto* n = dynamic_cast<const DictExpr*>(e)) {
        auto r = std::make_unique<DictExpr>(); r->loc = n->loc;
        for (const auto& p : n->pairs)
            r->pairs.emplace_back(clone_expr(p.first.get(), s), clone_expr(p.second.get(), s));
        return r;
    }
    if (auto* n = dynamic_cast<const LambdaExpr*>(e)) {
        auto r = std::make_unique<LambdaExpr>(); r->loc = n->loc;
        for (const auto& p : n->params)
            r->params.push_back(Param{subst_te(p.type, s), p.name});
        r->body = clone_stmt(n->body.get(), s);
        return r;
    }
    // Fallback: return null (should not happen for well-formed ASTs)
    return nullptr;
}

static StmtList clone_stmts(const StmtList& stmts, const SubstMap& s) {
    StmtList r;
    for (const auto& st : stmts) r.push_back(clone_stmt(st.get(), s));
    return r;
}

static StmtPtr clone_stmt(const Stmt* st, const SubstMap& s) {
    if (!st) return nullptr;

    if (auto* n = dynamic_cast<const BlockStmt*>(st)) {
        auto r = std::make_unique<BlockStmt>(); r->loc = n->loc;
        r->body = clone_stmts(n->body, s);
        return r;
    }
    if (auto* n = dynamic_cast<const ExprStmt*>(st)) {
        auto r = std::make_unique<ExprStmt>(); r->loc = n->loc;
        r->expr = clone_expr(n->expr.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const VarDeclStmt*>(st)) {
        auto r = std::make_unique<VarDeclStmt>(); r->loc = n->loc;
        r->type     = subst_te(n->type, s);
        r->is_const = n->is_const;
        for (const auto& [name, init] : n->decls)
            r->decls.emplace_back(name, init ? clone_expr(init.get(), s) : nullptr);
        return r;
    }
    if (auto* n = dynamic_cast<const IfStmt*>(st)) {
        auto r = std::make_unique<IfStmt>(); r->loc = n->loc;
        r->cond    = clone_expr(n->cond.get(), s);
        r->then_br = clone_stmt(n->then_br.get(), s);
        if (n->else_br) r->else_br = clone_stmt(n->else_br.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const WhileStmt*>(st)) {
        auto r = std::make_unique<WhileStmt>(); r->loc = n->loc;
        r->label = n->label;
        r->cond  = clone_expr(n->cond.get(), s);
        r->body  = clone_stmt(n->body.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const DoWhileStmt*>(st)) {
        auto r = std::make_unique<DoWhileStmt>(); r->loc = n->loc;
        r->body = clone_stmt(n->body.get(), s);
        r->cond = clone_expr(n->cond.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const ForInStmt*>(st)) {
        auto r = std::make_unique<ForInStmt>(); r->loc = n->loc;
        r->var_type = subst_te(n->var_type, s);
        r->var_name = n->var_name;
        r->iterable = clone_expr(n->iterable.get(), s);
        r->body     = clone_stmt(n->body.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const ForCStmt*>(st)) {
        auto r = std::make_unique<ForCStmt>(); r->loc = n->loc;
        r->var_type = subst_te(n->var_type, s);
        r->var_name = n->var_name;
        r->init  = clone_expr(n->init.get(), s);
        r->cond  = clone_expr(n->cond.get(), s);
        r->incr  = clone_expr(n->incr.get(), s);
        r->body  = clone_stmt(n->body.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const SwitchStmt*>(st)) {
        auto r = std::make_unique<SwitchStmt>(); r->loc = n->loc;
        r->expr = clone_expr(n->expr.get(), s);
        for (const auto& c : n->cases) {
            SwitchCase sc;
            if (c.value) sc.value = clone_expr(c.value->get(), s);
            sc.body = clone_stmts(c.body, s);
            r->cases.push_back(std::move(sc));
        }
        return r;
    }
    if (auto* n = dynamic_cast<const TryCatchStmt*>(st)) {
        auto r = std::make_unique<TryCatchStmt>(); r->loc = n->loc;
        r->try_body   = clone_stmt(n->try_body.get(), s);
        r->catch_all  = n->catch_all;
        if (n->catch_type) r->catch_type = subst_te(*n->catch_type, s);
        r->catch_var  = n->catch_var;
        r->catch_body = clone_stmt(n->catch_body.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const ReturnStmt*>(st)) {
        auto r = std::make_unique<ReturnStmt>(); r->loc = n->loc;
        if (n->value) r->value = clone_expr(n->value->get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const BreakStmt*>(st)) {
        auto r = std::make_unique<BreakStmt>(); r->loc = n->loc; r->label = n->label; return r;
    }
    if (auto* n = dynamic_cast<const ContinueStmt*>(st)) {
        auto r = std::make_unique<ContinueStmt>(); r->loc = n->loc; r->label = n->label; return r;
    }
    if (auto* n = dynamic_cast<const AssertStmt*>(st)) {
        auto r = std::make_unique<AssertStmt>(); r->loc = n->loc;
        r->cond = clone_expr(n->cond.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const DeleteStmt*>(st)) {
        auto r = std::make_unique<DeleteStmt>(); r->loc = n->loc;
        r->expr = clone_expr(n->expr.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const LabeledStmt*>(st)) {
        auto r = std::make_unique<LabeledStmt>(); r->loc = n->loc;
        r->label = n->label;
        r->stmt  = clone_stmt(n->stmt.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const DeferStmt*>(st)) {
        auto r = std::make_unique<DeferStmt>(); r->loc = n->loc;
        r->body = clone_stmts(n->body, s);
        return r;
    }
    if (auto* n = dynamic_cast<const ThrowStmt*>(st)) {
        auto r = std::make_unique<ThrowStmt>(); r->loc = n->loc;
        r->expr = clone_expr(n->expr.get(), s);
        return r;
    }
    if (auto* n = dynamic_cast<const UnsafeStmt*>(st)) {
        auto r = std::make_unique<UnsafeStmt>(); r->loc = n->loc;
        r->body = clone_stmts(n->body, s);
        return r;
    }
    return nullptr;
}

static DeclPtr clone_func_decl(const FunctionDecl& f, const SubstMap& s) {
    auto r         = std::make_unique<FunctionDecl>();
    r->loc         = f.loc;
    r->return_type = subst_te(f.return_type, s);
    r->name        = f.name;
    r->modifier    = f.modifier;
    r->is_ctor     = f.is_ctor;
    r->is_dtor     = f.is_dtor;
    for (const auto& p : f.params)
        r->params.push_back(Param{subst_te(p.type, s), p.name});
    for (const auto& ie : f.init_list) {
        InitEntry entry; entry.field = ie.field;
        for (const auto& a : ie.args) entry.args.push_back(clone_expr(a.get(), s));
        r->init_list.push_back(std::move(entry));
    }
    if (f.body) {
        BlockStmt b; b.loc = f.body->loc;
        b.body = clone_stmts(f.body->body, s);
        r->body = std::move(b);
    }
    return r;
}

static DeclPtr clone_field_decl(const FieldDecl& f, const SubstMap& s) {
    auto r  = std::make_unique<FieldDecl>();
    r->loc  = f.loc;
    r->type = subst_te(f.type, s);
    r->name = f.name;
    if (f.init) r->init = clone_expr(f.init->get(), s);
    return r;
}

static DeclPtr clone_class_decl(const ClassDecl& c, const std::string& new_name,
                                 const SubstMap& s) {
    auto r           = std::make_unique<ClassDecl>();
    r->loc           = c.loc;
    r->name          = new_name;
    r->type_params   = {};   // instantiated class has no type params
    r->type_bounds   = {};   // instantiated class has no bounds
    // Skip decorators (they have non-copyable ExprList); not needed for generics
    r->bases         = c.bases;
    for (const auto& m : c.members) {
        ClassMember cm; cm.access = m.access;
        // Skip member decorators for the same reason
        if (m.decl) {
            if (auto* fd = dynamic_cast<const FunctionDecl*>(m.decl.get())) {
                auto cloned = clone_func_decl(*fd, s);
                // Rename constructor/destructor to match the instantiated class name
                // so gen_new can find it via mangle(cls, cls)
                auto* cf = dynamic_cast<FunctionDecl*>(cloned.get());
                if (cf->is_ctor || cf->is_dtor) cf->name = new_name;
                cm.decl = std::move(cloned);
            } else if (auto* field = dynamic_cast<const FieldDecl*>(m.decl.get())) {
                cm.decl = clone_field_decl(*field, s);
            }
        }
        r->members.push_back(std::move(cm));
    }
    return r;
}

static DeclPtr clone_generic_func(const FunctionDecl& f, const std::string& new_name,
                                   const SubstMap& s) {
    auto r = dynamic_cast<FunctionDecl*>(clone_func_decl(f, s).release());
    r->name = new_name;
    r->type_params = {};
    r->type_bounds = {};
    return DeclPtr(r);
}

// ─── AST walker to fix TypeExprs in non-const nodes ──────────────────────────
// For each TypeExpr with type_args, compute the mangled name and replace in-place.
// Fills `pending` with (mangled_name, TypeExprs) tuples that need instantiation.

struct InstKey {
    std::string          generic_name;
    std::vector<TypeExpr> type_args;
    std::string           mangled;
    SourceLoc             loc;       // instantiation site location
};

static void fix_te(TypeExpr& te,
                   std::unordered_set<std::string>& done,
                   std::vector<InstKey>& pending);

static void fix_expr(Expr* e,
                     std::unordered_set<std::string>& done,
                     std::vector<InstKey>& pending);

static void fix_stmts(StmtList& stmts,
                      std::unordered_set<std::string>& done,
                      std::vector<InstKey>& pending);

static void fix_stmt(Stmt* st,
                     std::unordered_set<std::string>& done,
                     std::vector<InstKey>& pending);

static void fix_te(TypeExpr& te,
                   std::unordered_set<std::string>& done,
                   std::vector<InstKey>& pending) {
    // Recurse first so nested type args are resolved
    for (auto& ta : te.type_args) fix_te(ta, done, pending);
    for (auto& fp : te.fn_params) fix_te(fp, done, pending);

    if (!te.type_args.empty()) {
        std::string mangled = mangle_generic(te.name, te.type_args);
        if (!done.count(mangled))
            pending.push_back(InstKey{te.name, te.type_args, mangled, te.loc});
        te.name      = mangled;
        te.type_args = {};
    }
}

static void fix_expr(Expr* e,
                     std::unordered_set<std::string>& done,
                     std::vector<InstKey>& pending) {
    if (!e) return;

    if (auto* n = dynamic_cast<NewExpr*>(e)) {
        fix_te(n->type, done, pending);
        for (auto& a : n->args) fix_expr(a.get(), done, pending);
        return;
    }
    if (auto* n = dynamic_cast<BinaryExpr*>(e)) {
        fix_expr(n->left.get(), done, pending);
        fix_expr(n->right.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<UnaryExpr*>(e)) {
        fix_expr(n->operand.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<AssignExpr*>(e)) {
        fix_expr(n->target.get(), done, pending);
        fix_expr(n->value.get(),  done, pending); return;
    }
    if (auto* n = dynamic_cast<CallExpr*>(e)) {
        fix_expr(n->callee.get(), done, pending);
        for (auto& a : n->args) fix_expr(a.get(), done, pending);
        return;
    }
    if (auto* n = dynamic_cast<MemberExpr*>(e)) {
        fix_expr(n->object.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<IndexExpr*>(e)) {
        fix_expr(n->object.get(), done, pending);
        fix_expr(n->index.get(),  done, pending); return;
    }
    if (auto* n = dynamic_cast<ListExpr*>(e)) {
        for (auto& el : n->elements) fix_expr(el.get(), done, pending);
        return;
    }
    if (auto* n = dynamic_cast<DictExpr*>(e)) {
        for (auto& p : n->pairs) {
            fix_expr(p.first.get(),  done, pending);
            fix_expr(p.second.get(), done, pending);
        }
        return;
    }
    if (auto* n = dynamic_cast<LambdaExpr*>(e)) {
        for (auto& p : n->params) fix_te(p.type, done, pending);
        fix_stmt(n->body.get(), done, pending);
        return;
    }
}

static void fix_stmt(Stmt* st,
                     std::unordered_set<std::string>& done,
                     std::vector<InstKey>& pending) {
    if (!st) return;

    if (auto* n = dynamic_cast<BlockStmt*>(st)) {
        fix_stmts(n->body, done, pending); return;
    }
    if (auto* n = dynamic_cast<ExprStmt*>(st)) {
        fix_expr(n->expr.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<VarDeclStmt*>(st)) {
        fix_te(n->type, done, pending);
        for (auto& [nm, init] : n->decls) fix_expr(init.get(), done, pending);
        return;
    }
    if (auto* n = dynamic_cast<IfStmt*>(st)) {
        fix_expr(n->cond.get(),    done, pending);
        fix_stmt(n->then_br.get(), done, pending);
        fix_stmt(n->else_br.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<WhileStmt*>(st)) {
        fix_expr(n->cond.get(), done, pending);
        fix_stmt(n->body.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<DoWhileStmt*>(st)) {
        fix_stmt(n->body.get(), done, pending);
        fix_expr(n->cond.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<ForInStmt*>(st)) {
        fix_te(n->var_type, done, pending);
        fix_expr(n->iterable.get(), done, pending);
        fix_stmt(n->body.get(),     done, pending); return;
    }
    if (auto* n = dynamic_cast<ForCStmt*>(st)) {
        fix_te(n->var_type, done, pending);
        fix_expr(n->init.get(), done, pending);
        fix_expr(n->cond.get(), done, pending);
        fix_expr(n->incr.get(), done, pending);
        fix_stmt(n->body.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<SwitchStmt*>(st)) {
        fix_expr(n->expr.get(), done, pending);
        for (auto& c : n->cases) {
            if (c.value) fix_expr(c.value->get(), done, pending);
            fix_stmts(c.body, done, pending);
        }
        return;
    }
    if (auto* n = dynamic_cast<TryCatchStmt*>(st)) {
        fix_stmt(n->try_body.get(),   done, pending);
        if (n->catch_type) fix_te(*n->catch_type, done, pending);
        fix_stmt(n->catch_body.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<ReturnStmt*>(st)) {
        if (n->value) fix_expr(n->value->get(), done, pending);
        return;
    }
    if (auto* n = dynamic_cast<AssertStmt*>(st)) {
        fix_expr(n->cond.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<DeleteStmt*>(st)) {
        fix_expr(n->expr.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<LabeledStmt*>(st)) {
        fix_stmt(n->stmt.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<DeferStmt*>(st)) {
        fix_stmts(n->body, done, pending); return;
    }
    if (auto* n = dynamic_cast<ThrowStmt*>(st)) {
        fix_expr(n->expr.get(), done, pending); return;
    }
    if (auto* n = dynamic_cast<UnsafeStmt*>(st)) {
        fix_stmts(n->body, done, pending); return;
    }
}

static void fix_stmts(StmtList& stmts,
                      std::unordered_set<std::string>& done,
                      std::vector<InstKey>& pending) {
    for (auto& sp : stmts) fix_stmt(sp.get(), done, pending);
}

static void fix_func_decl(FunctionDecl& f,
                          std::unordered_set<std::string>& done,
                          std::vector<InstKey>& pending) {
    fix_te(f.return_type, done, pending);
    for (auto& p : f.params) fix_te(p.type, done, pending);
    if (f.body) fix_stmts(f.body->body, done, pending);
}

static void fix_decl(Decl* d,
                     std::unordered_set<std::string>& done,
                     std::vector<InstKey>& pending,
                     bool skip_generic) {
    if (auto* c = dynamic_cast<ClassDecl*>(d)) {
        if (skip_generic && !c->type_params.empty()) return;
        for (auto& m : c->members) {
            if (!m.decl) continue;
            if (auto* fd = dynamic_cast<FunctionDecl*>(m.decl.get()))
                fix_func_decl(*fd, done, pending);
            else if (auto* fld = dynamic_cast<FieldDecl*>(m.decl.get()))
                fix_te(fld->type, done, pending);
        }
    } else if (auto* f = dynamic_cast<FunctionDecl*>(d)) {
        if (skip_generic && !f->type_params.empty()) return;
        fix_func_decl(*f, done, pending);
    } else if (auto* ns = dynamic_cast<NamespaceDecl*>(d)) {
        for (auto& dp : ns->decls) fix_decl(dp.get(), done, pending, skip_generic);
        fix_stmts(ns->stmts, done, pending);
    }
}

// ─── Bounds checking (AST-level, before TypeRegistry exists) ─────────────────

// Check whether a concrete type name satisfies a bound by scanning prog.decls.
// Returns true if satisfied or if the concrete type is unknown (built-in/opaque).
static bool ast_satisfies_bound(const std::string& concrete_type,
                                 const std::string& interface_name,
                                 const ast::Program& prog) {
    if (interface_name.empty()) return true;
    for (const auto& dp : prog.decls) {
        if (auto* c = dynamic_cast<const ClassDecl*>(dp.get())) {
            if (c->name == concrete_type) {
                for (const auto& base : c->bases)
                    if (base.name == interface_name) return true;
                return false;
            }
        }
    }
    return true; // unknown (built-in) — skip check
}

// ─── Main entry point ─────────────────────────────────────────────────────────

void expand_generics(ast::Program& prog, Driver& driver) {
    // Step 1: collect generic class and function templates.
    // Recurse into NamespaceDecl so templates inside imported stdlib files are found.
    std::unordered_map<std::string, const ClassDecl*>    generic_classes;
    std::unordered_map<std::string, const FunctionDecl*> generic_funcs;

    std::function<void(const DeclList&)> collect_templates;
    collect_templates = [&](const DeclList& decls) {
        for (const auto& dp : decls) {
            if (auto* c = dynamic_cast<const ClassDecl*>(dp.get()))
                if (!c->type_params.empty()) generic_classes[c->name] = c;
            if (auto* f = dynamic_cast<const FunctionDecl*>(dp.get()))
                if (!f->type_params.empty()) generic_funcs[f->name] = f;
            if (auto* ns = dynamic_cast<const NamespaceDecl*>(dp.get()))
                collect_templates(ns->decls);
        }
    };
    collect_templates(prog.decls);

    if (generic_classes.empty() && generic_funcs.empty()) return;

    // Step 2: walk the program and fix all TypeExprs with type_args
    std::unordered_set<std::string> done;
    std::vector<InstKey> pending;

    // Walk existing declarations (skip generic definitions themselves)
    for (auto& dp : prog.decls)
        fix_decl(dp.get(), done, pending, /*skip_generic=*/true);

    // Walk top-level statements
    fix_stmts(prog.stmts, done, pending);

    // Step 3: process pending instantiations (work-list)
    DeclList new_decls;
    while (!pending.empty()) {
        InstKey key = std::move(pending.back());
        pending.pop_back();

        if (done.count(key.mangled)) continue;
        done.insert(key.mangled);

        // Try to instantiate as a class
        auto cit = generic_classes.find(key.generic_name);
        if (cit != generic_classes.end()) {
            const ClassDecl& tmpl = *cit->second;
            if (tmpl.type_params.size() != key.type_args.size()) continue;

            // Build substitution map: T → concrete TypeExpr
            SubstMap subst;
            for (size_t i = 0; i < tmpl.type_params.size(); ++i)
                subst[tmpl.type_params[i]] = key.type_args[i];

            // Check type bounds
            for (const auto& bound : tmpl.type_bounds) {
                auto it = subst.find(bound.param);
                if (it == subst.end()) continue;
                const std::string& concrete_type = it->second.name;
                if (!ast_satisfies_bound(concrete_type, bound.interface_name, prog)) {
                    driver.error(key.loc,
                        "type '" + concrete_type + "' does not implement '" +
                        bound.interface_name + "' (required by generic parameter '" +
                        bound.param + "')");
                }
            }

            DeclPtr concrete = clone_class_decl(tmpl, key.mangled, subst);

            // Walk the new concrete class for more instantiations
            fix_decl(concrete.get(), done, pending, /*skip_generic=*/false);

            new_decls.push_back(std::move(concrete));
            continue;
        }

        // Try to instantiate as a function
        auto fit = generic_funcs.find(key.generic_name);
        if (fit != generic_funcs.end()) {
            const FunctionDecl& tmpl = *fit->second;
            if (tmpl.type_params.size() != key.type_args.size()) continue;

            SubstMap subst;
            for (size_t i = 0; i < tmpl.type_params.size(); ++i)
                subst[tmpl.type_params[i]] = key.type_args[i];

            // Check type bounds
            for (const auto& bound : tmpl.type_bounds) {
                auto it = subst.find(bound.param);
                if (it == subst.end()) continue;
                const std::string& concrete_type = it->second.name;
                if (!ast_satisfies_bound(concrete_type, bound.interface_name, prog)) {
                    driver.error(key.loc,
                        "type '" + concrete_type + "' does not implement '" +
                        bound.interface_name + "' (required by generic parameter '" +
                        bound.param + "')");
                }
            }

            DeclPtr concrete = clone_generic_func(tmpl, key.mangled, subst);
            fix_decl(concrete.get(), done, pending, /*skip_generic=*/false);
            new_decls.push_back(std::move(concrete));
        }
    }

    // Step 4: prepend instantiated definitions and remove generic templates
    DeclList result;
    // First the instantiated concrete classes/functions
    for (auto& dp : new_decls) result.push_back(std::move(dp));
    // Then the original non-generic declarations
    for (auto& dp : prog.decls) {
        bool is_generic = false;
        if (auto* c = dynamic_cast<const ClassDecl*>(dp.get()))
            is_generic = !c->type_params.empty();
        if (auto* f = dynamic_cast<const FunctionDecl*>(dp.get()))
            is_generic = !f->type_params.empty();
        if (!is_generic) result.push_back(std::move(dp));
    }
    // Also strip generic templates from inside namespaces (e.g. imported stdlib files).
    for (auto& dp : result) {
        if (auto* ns = dynamic_cast<NamespaceDecl*>(dp.get())) {
            DeclList kept;
            for (auto& nd : ns->decls) {
                bool is_ns_generic = false;
                if (auto* c = dynamic_cast<const ClassDecl*>(nd.get()))
                    is_ns_generic = !c->type_params.empty();
                if (auto* f = dynamic_cast<const FunctionDecl*>(nd.get()))
                    is_ns_generic = !f->type_params.empty();
                if (!is_ns_generic) kept.push_back(std::move(nd));
            }
            ns->decls = std::move(kept);
        }
    }
    prog.decls = std::move(result);
}

} // namespace dux::sema
