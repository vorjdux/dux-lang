#pragma once
#include <string>
#include <vector>
#include "ast/ast.hpp"
#include "sema/types.hpp"
#include "sema/symbol.hpp"

class Driver;

namespace dux::sema {

class Sema {
public:
    explicit Sema(Driver& driver);

    bool run(const ast::Program& prog);
    int  error_count() const { return error_count_; }

private:
    Driver&      driver_;
    TypeRegistry types_;
    ScopeStack   scopes_;

    TypeId      current_return_type_{TypeRegistry::TID_VOID};
    std::string current_class_name_;
    TypeId      current_class_type_{TypeRegistry::TID_UNKNOWN};
    bool        in_loop_{false};
    int         error_count_{0};

    // ── Hoisting (pass 1) ──────────────────────────────────────────────────
    void hoist_top(const ast::DeclList& decls);
    void hoist_class(const ast::ClassDecl& c);
    void hoist_interface(const ast::InterfaceDecl& i);
    void hoist_namespace(const ast::NamespaceDecl& ns);

    // ── Declarations (pass 2) ──────────────────────────────────────────────
    void check_decl(const ast::Decl& d);
    void check_func(const ast::FunctionDecl& f, TypeId class_type = TypeRegistry::TID_UNKNOWN);
    void check_class(const ast::ClassDecl& c);
    void check_interface(const ast::InterfaceDecl& i);
    void check_namespace(const ast::NamespaceDecl& ns);
    void check_import(const ast::ImportDecl& imp);

    // ── Statements ────────────────────────────────────────────────────────
    void check_stmts(const ast::StmtList& stmts);
    void check_stmt(const ast::Stmt& s);
    void check_block(const ast::BlockStmt& b);
    void check_if(const ast::IfStmt& s);
    void check_while(const ast::WhileStmt& s);
    void check_do_while(const ast::DoWhileStmt& s);
    void check_for_in(const ast::ForInStmt& s);
    void check_for_c(const ast::ForCStmt& s);
    void check_switch(const ast::SwitchStmt& s);
    void check_try_catch(const ast::TryCatchStmt& s);
    void check_return(const ast::ReturnStmt& s);
    void check_var_decl(const ast::VarDeclStmt& s);
    void check_assert(const ast::AssertStmt& s);
    void check_delete(const ast::DeleteStmt& s);

    // ── Expressions ───────────────────────────────────────────────────────
    TypeId check_expr(const ast::Expr& e);
    TypeId check_assign(const ast::AssignExpr& e);
    TypeId check_binary(const ast::BinaryExpr& e);
    TypeId check_unary(const ast::UnaryExpr& e);
    TypeId check_call(const ast::CallExpr& e);
    TypeId check_member(const ast::MemberExpr& e);
    TypeId check_index(const ast::IndexExpr& e);
    TypeId check_new(const ast::NewExpr& e);
    TypeId check_list(const ast::ListExpr& e);
    TypeId check_dict(const ast::DictExpr& e);

    // ── Helpers ───────────────────────────────────────────────────────────
    TypeId type_from_te(const ast::TypeExpr& te) const;
    bool   require_assignable(TypeId from, TypeId to,
                              const ast::SourceLoc& loc, const std::string& ctx);
    bool   require_numeric(TypeId t, const ast::SourceLoc& loc, const std::string& ctx);
    bool   require_bool(TypeId t, const ast::SourceLoc& loc, const std::string& ctx);

    void err(const ast::SourceLoc& loc, const std::string& msg);
    void warn(const ast::SourceLoc& loc, const std::string& msg);
};

} // namespace dux::sema
