#include "sema/sema.hpp"
#include "driver/driver.hpp"
#include <sstream>
#include <unordered_set>

namespace dux::sema {

using TR = TypeRegistry;

// ─── Constructor ─────────────────────────────────────────────────────────────

Sema::Sema(Driver& driver) : driver_(driver) {
    // Pre-populate global scope with built-in functions.
    auto builtin = [&](const std::string& name, TypeId ret,
                       std::vector<std::pair<std::string, TypeId>> params = {}) {
        Symbol s;
        s.name        = name;
        s.kind        = SymKind::BuiltIn;
        s.type        = ret;
        s.return_type = ret;
        s.params      = std::move(params);
        scopes_.define(name, std::move(s));
    };

    builtin("println",  TR::TID_VOID, {{"value", TR::TID_OBJECT}});
    builtin("print",    TR::TID_VOID, {{"value", TR::TID_OBJECT}});
    builtin("readline", TR::TID_STR);
    builtin("len",      TR::TID_INT,  {{"x", TR::TID_OBJECT}});
    builtin("str",      TR::TID_STR,  {{"x", TR::TID_OBJECT}});
    builtin("int",      TR::TID_INT,  {{"x", TR::TID_OBJECT}});
    builtin("double",   TR::TID_DOUBLE, {{"x", TR::TID_OBJECT}});
    builtin("range",    TR::TID_LIST, {{"end", TR::TID_INT}});
    builtin("assert",   TR::TID_VOID, {{"cond", TR::TID_BOOL}});

    // Built-in root class symbols (so classes can extend them without error)
    auto builtin_class = [&](const std::string& name, TypeId tid) {
        Symbol s;
        s.name = name;
        s.kind = SymKind::Class;
        s.type = tid;
        scopes_.define(name, std::move(s));
    };
    builtin_class("object", TR::TID_OBJECT);
    builtin_class("any",    TR::TID_OBJECT);
}

// ─── Entry point ─────────────────────────────────────────────────────────────

bool Sema::run(const ast::Program& prog) {
    hoist_top(prog.decls);
    for (const auto& d : prog.decls) check_decl(*d);
    check_stmts(prog.stmts);
    return error_count_ == 0;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void Sema::err(const ast::SourceLoc& loc, const std::string& msg) {
    driver_.error(loc, msg);
    ++error_count_;
}

void Sema::warn(const ast::SourceLoc& loc, const std::string& msg) {
    driver_.warning(loc, msg);
}

TypeId Sema::type_from_te(const ast::TypeExpr& te) const {
    if (te.name == "ptr") return TR::TID_OBJECT;
    TypeId id = types_.from_type_expr(te);
    return id == TR::TID_UNKNOWN ? TR::TID_UNKNOWN : id;
}

bool Sema::require_assignable(TypeId from, TypeId to,
                              const ast::SourceLoc& loc, const std::string& ctx) {
    if (types_.assignable(from, to)) return true;
    // Inside unsafe blocks ptr (object) can be reinterpreted as any type
    if (in_unsafe_ && (from == TR::TID_OBJECT || to == TR::TID_OBJECT)) return true;
    std::ostringstream os;
    os << ctx << ": cannot assign '" << types_.name_of(from)
       << "' to '" << types_.name_of(to) << "'";
    err(loc, os.str());
    return false;
}

bool Sema::require_numeric(TypeId t, const ast::SourceLoc& loc, const std::string& ctx) {
    if (t == TR::TID_UNKNOWN || types_.is_numeric(t)) return true;
    err(loc, ctx + ": expected numeric type, got '" + types_.name_of(t) + "'");
    return false;
}

bool Sema::require_bool(TypeId t, const ast::SourceLoc& loc, const std::string& ctx) {
    if (t == TR::TID_UNKNOWN || t == TR::TID_BOOL ||
        types_.assignable(t, TR::TID_BOOL)) return true;
    // Allow any type in boolean context (like C / Python — warn only)
    warn(loc, ctx + ": condition is '" + types_.name_of(t) + "', expected 'bool'");
    return true;
}

// ─── Hoisting (pass 1) ───────────────────────────────────────────────────────

void Sema::hoist_top(const ast::DeclList& decls) {
    for (const auto& dp : decls) {
        if (auto* c = dynamic_cast<const ast::ClassDecl*>(dp.get()))
            hoist_class(*c);
        else if (auto* i = dynamic_cast<const ast::InterfaceDecl*>(dp.get()))
            hoist_interface(*i);
        else if (auto* e = dynamic_cast<const ast::EnumDecl*>(dp.get()))
            hoist_enum(*e);
        else if (auto* ns = dynamic_cast<const ast::NamespaceDecl*>(dp.get()))
            hoist_namespace(*ns);
        else if (auto* f = dynamic_cast<const ast::FunctionDecl*>(dp.get())) {
            TypeId ret = type_from_te(f->return_type);
            Symbol s;
            s.name        = f->name;
            s.kind        = SymKind::Function;
            s.type        = ret;
            s.return_type = ret;
            s.decl        = dp.get();
            s.loc         = f->loc;
            for (const auto& p : f->params)
                s.params.emplace_back(p.name, type_from_te(p.type));
            if (!scopes_.define(s.name, s))
                err(f->loc, "function '" + f->name + "' already declared in this scope");
        }
        else if (auto* ext = dynamic_cast<const ast::ExternDecl*>(dp.get()))
            hoist_extern(*ext);
    }
}

void Sema::hoist_enum(const ast::EnumDecl& e) {
    // Register the enum type
    TypeId id = types_.intern(e.name, TypeKind::Enum);
    TypeInfo& ti = types_.info(id);
    ti.variants.clear();

    // Register each variant and its tag constant in the global scope
    int32_t tag = 0;
    for (const auto& v : e.variants) {
        EnumVariantInfo vi;
        vi.name = v.name;
        vi.tag  = tag++;
        for (const auto& pt : v.payload)
            vi.payload.push_back(types_.from_type_expr(pt));
        ti.variants.push_back(vi);

        // Register "EnumName.VariantName" as a const int symbol
        Symbol s;
        s.name     = e.name + "." + v.name;
        s.kind     = SymKind::Var;
        s.type     = id;   // type is the enum itself
        s.is_const = true;
        s.loc      = v.loc;
        scopes_.global_scope().define(s.name, s);
    }

    // Register the enum type itself as a symbol
    Symbol s;
    s.name = e.name;
    s.kind = SymKind::Class;   // treat like class for lookup purposes
    s.type = id;
    s.decl = const_cast<ast::EnumDecl*>(&e);
    s.loc  = e.loc;
    if (!scopes_.define(s.name, s))
        err(e.loc, "enum '" + e.name + "' already declared");
}

void Sema::hoist_extern(const ast::ExternDecl& e) {
    TypeId ret = type_from_te(e.ret);
    Symbol s;
    s.name        = e.name;
    s.kind        = SymKind::Function;
    s.type        = ret;
    s.return_type = ret;
    s.loc         = e.loc;
    for (const auto& p : e.params)
        s.params.emplace_back(p.name, type_from_te(p.type));
    scopes_.define(s.name, s);
}

void Sema::hoist_class(const ast::ClassDecl& c) {
    TypeId id = types_.intern(c.name, TypeKind::Class);
    // Wire up parent
    if (!c.bases.empty()) {
        TypeId parent = types_.from_name(c.bases[0].name);
        if (parent == TR::TID_UNKNOWN) parent = TR::TID_OBJECT;
        types_.set_parent(id, parent);
    }
    Symbol s;
    s.name = c.name;
    s.kind = SymKind::Class;
    s.type = id;
    s.decl = const_cast<ast::ClassDecl*>(&c);
    s.loc  = c.loc;
    if (!scopes_.define(s.name, s))
        err(c.loc, "class '" + c.name + "' already declared");
}

void Sema::hoist_interface(const ast::InterfaceDecl& i) {
    TypeId id = types_.intern(i.name, TypeKind::Interface);
    Symbol s;
    s.name = i.name;
    s.kind = SymKind::Interface;
    s.type = id;
    s.decl = const_cast<ast::InterfaceDecl*>(&i);
    s.loc  = i.loc;
    if (!scopes_.define(s.name, s))
        err(i.loc, "interface '" + i.name + "' already declared");
}

void Sema::hoist_namespace(const ast::NamespaceDecl& ns) {
    Symbol s;
    s.name = ns.name;
    s.kind = SymKind::Namespace;
    s.type = TR::TID_UNKNOWN;
    s.decl = const_cast<ast::NamespaceDecl*>(&ns);
    s.loc  = ns.loc;
    scopes_.define(s.name, s); // namespaces can shadow, don't error
    // Also hoist the namespace's own decls into global scope (flat for now)
    scopes_.push();
    hoist_top(ns.decls);
    // Promote to global scope so namespace members are reachable
    scopes_.current_scope().for_each([&](const std::string& name, const Symbol& sym) {
        Symbol qualified = sym;
        qualified.name   = ns.name + "." + name;
        scopes_.global_scope().define(qualified.name, qualified);
        // Also unqualified within same namespace context
        scopes_.global_scope().define(name, sym);
    });
    scopes_.pop();
}

// ─── Declarations ────────────────────────────────────────────────────────────

void Sema::check_decl(const ast::Decl& d) {
    if (auto* f  = dynamic_cast<const ast::FunctionDecl*>(&d))  check_func(*f);
    else if (auto* c  = dynamic_cast<const ast::ClassDecl*>(&d))     check_class(*c);
    else if (auto* i  = dynamic_cast<const ast::InterfaceDecl*>(&d)) check_interface(*i);
    else if (auto* ns = dynamic_cast<const ast::NamespaceDecl*>(&d)) check_namespace(*ns);
    else if (auto* im = dynamic_cast<const ast::ImportDecl*>(&d))    check_import(*im);
    // EnumDecl: already fully processed in hoist_enum (pass 1)
    // ExternDecl: already hoisted in pass 1; nothing to check in pass 2
}

void Sema::check_func(const ast::FunctionDecl& f, TypeId /*class_type*/) {
    TypeId ret = type_from_te(f.return_type);
    scopes_.push();

    // Define 'this' if inside a class
    if (!current_class_name_.empty()) {
        Symbol th;
        th.name = "this";
        th.kind = SymKind::Var;
        th.type = current_class_type_;
        scopes_.define("this", th);

        // Define 'super' as the parent class type
        TypeId parent = types_.info(current_class_type_).parent;
        if (parent != TR::TID_UNKNOWN && parent >= 0) {
            Symbol su;
            su.name = "super";
            su.kind = SymKind::Var;
            su.type = parent;
            scopes_.define("super", su);
        }
    }

    // Define parameters
    for (const auto& p : f.params) {
        Symbol sym;
        sym.name = p.name;
        sym.kind = SymKind::Param;
        sym.type = type_from_te(p.type);
        sym.loc  = p.type.loc;
        if (!scopes_.define(p.name, sym))
            err(p.type.loc, "duplicate parameter '" + p.name + "'");
    }

    // Check body
    auto saved_ret  = current_return_type_;
    current_return_type_ = ret;

    if (f.body) check_stmts(f.body->body);

    current_return_type_ = saved_ret;
    scopes_.pop();
}

void Sema::check_class(const ast::ClassDecl& c) {
    TypeId class_type = types_.from_name(c.name);

    // Validate base classes
    for (const auto& base : c.bases) {
        Symbol* bs = scopes_.lookup(base.name);
        if (!bs) {
            // Unknown base — treat as object (forward-declared or external)
            warn(c.loc, "unknown base class '" + base.name + "', treating as object");
        } else if (bs->kind != SymKind::Class && bs->kind != SymKind::Interface) {
            warn(c.loc, "'" + base.name + "' is not a class or interface");
        } else {
            types_.set_parent(class_type, bs->type);
        }
    }

    // Collect methods defined in this class
    std::unordered_set<std::string> defined_methods;
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get()))
            if (!f->is_ctor && !f->is_dtor)
                defined_methods.insert(f->name);
    }

    // Verify that all interface contracts are satisfied
    for (const auto& base : c.bases) {
        Symbol* bs = scopes_.lookup(base.name);
        if (!bs || bs->kind != SymKind::Interface) continue;
        auto* iface = dynamic_cast<const ast::InterfaceDecl*>(bs->decl);
        if (!iface) continue;
        for (const auto& m : iface->members) {
            if (!m.decl) continue;
            if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
                if (!defined_methods.count(f->name))
                    err(c.loc, "class '" + c.name + "' implements interface '" +
                        base.name + "' but does not define method '" + f->name + "'");
            }
        }
    }

    auto saved_class  = current_class_name_;
    auto saved_ctype  = current_class_type_;
    current_class_name_ = c.name;
    current_class_type_ = class_type;

    // Hoist member function names into the class scope first
    scopes_.push(true);
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            Symbol sym;
            sym.name        = f->name;
            sym.kind        = SymKind::Function;
            sym.type        = type_from_te(f->return_type);
            sym.return_type = sym.type;
            sym.decl        = m.decl.get();
            sym.loc         = f->loc;
            for (const auto& p : f->params)
                sym.params.emplace_back(p.name, type_from_te(p.type));
            scopes_.define(sym.name, sym);
        } else if (auto* fd = dynamic_cast<const ast::FieldDecl*>(m.decl.get())) {
            Symbol sym;
            sym.name = fd->name;
            sym.kind = SymKind::Var;
            sym.type = type_from_te(fd->type);
            sym.decl = m.decl.get();
            sym.loc  = fd->loc;
            scopes_.define(sym.name, sym);
        }
    }

    // Check members
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get()))
            check_func(*f, class_type);
        else if (auto* fd = dynamic_cast<const ast::FieldDecl*>(m.decl.get())) {
            if (fd->init) {
                TypeId init_t = check_expr(**fd->init);
                TypeId decl_t = type_from_te(fd->type);
                require_assignable(init_t, decl_t, fd->loc,
                                   "field '" + fd->name + "' initialiser");
            }
        }
    }

    scopes_.pop();
    current_class_name_ = saved_class;
    current_class_type_ = saved_ctype;
}

void Sema::check_interface(const ast::InterfaceDecl& i) {
    // Interface members are abstract; just validate the method signatures.
    for (const auto& m : i.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            TypeId ret = type_from_te(f->return_type);
            if (ret == TR::TID_UNKNOWN && !f->return_type.name.empty())
                warn(f->loc, "interface method '" + f->name + "': unknown return type '" +
                              f->return_type.name + "'");
        }
    }
}

void Sema::check_namespace(const ast::NamespaceDecl& ns) {
    scopes_.push();
    hoist_top(ns.decls);
    for (const auto& d : ns.decls) check_decl(*d);
    check_stmts(ns.stmts);
    scopes_.pop();
}

void Sema::check_import(const ast::ImportDecl& imp) {
    // Simplified: register the module name as an object-typed symbol.
    // Full resolution (loading files) is M5 work.
    std::string first = imp.path.substr(0, imp.path.find('.'));
    if (!first.empty() && !scopes_.lookup(first)) {
        Symbol s;
        s.name = first;
        s.kind = SymKind::Namespace;
        s.type = TR::TID_OBJECT;
        scopes_.define(first, s);
    }
}

// ─── Statements ──────────────────────────────────────────────────────────────

void Sema::check_stmts(const ast::StmtList& stmts) {
    for (const auto& sp : stmts) check_stmt(*sp);
}

void Sema::check_stmt(const ast::Stmt& s) {
    if (auto* b  = dynamic_cast<const ast::BlockStmt*>(&s))     { check_block(*b);    return; }
    if (auto* e  = dynamic_cast<const ast::ExprStmt*>(&s))      { check_expr(*e->expr); return; }
    if (auto* v  = dynamic_cast<const ast::VarDeclStmt*>(&s))   { check_var_decl(*v); return; }
    if (auto* i  = dynamic_cast<const ast::IfStmt*>(&s))        { check_if(*i);       return; }
    if (auto* w  = dynamic_cast<const ast::WhileStmt*>(&s))     { check_while(*w);    return; }
    if (auto* dw = dynamic_cast<const ast::DoWhileStmt*>(&s))   { check_do_while(*dw);return; }
    if (auto* fi = dynamic_cast<const ast::ForInStmt*>(&s))     { check_for_in(*fi);  return; }
    if (auto* fc = dynamic_cast<const ast::ForCStmt*>(&s))      { check_for_c(*fc);   return; }
    if (auto* sw = dynamic_cast<const ast::SwitchStmt*>(&s))    { check_switch(*sw);  return; }
    if (auto* mx = dynamic_cast<const ast::MatchStmt*>(&s))     { check_match(*mx);   return; }
    if (auto* tc = dynamic_cast<const ast::TryCatchStmt*>(&s))  { check_try_catch(*tc);return;}
    if (auto* r  = dynamic_cast<const ast::ReturnStmt*>(&s))    { check_return(*r);   return; }
    if (auto* br = dynamic_cast<const ast::BreakStmt*>(&s))     {
        if (!in_loop_) err(br->loc, "'break' outside of loop");
        return;
    }
    if (auto* co = dynamic_cast<const ast::ContinueStmt*>(&s))  {
        if (!in_loop_) err(co->loc, "'continue' outside of loop");
        return;
    }
    if (auto* a  = dynamic_cast<const ast::AssertStmt*>(&s))    { check_assert(*a);   return; }
    if (auto* d  = dynamic_cast<const ast::DeleteStmt*>(&s))    { check_delete(*d);   return; }
    // LabeledStmt, etc. — visit the inner stmt
    if (auto* ls = dynamic_cast<const ast::LabeledStmt*>(&s))   { check_stmt(*ls->stmt); return; }
    if (auto* us = dynamic_cast<const ast::UnsafeStmt*>(&s)) {
        bool prev = in_unsafe_;
        in_unsafe_ = true;
        check_stmts(us->body);
        in_unsafe_ = prev;
        return;
    }
}

void Sema::check_block(const ast::BlockStmt& b) {
    scopes_.push();
    check_stmts(b.body);
    scopes_.pop();
}

void Sema::check_if(const ast::IfStmt& s) {
    TypeId ct = check_expr(*s.cond);
    require_bool(ct, s.cond->loc, "if condition");
    check_stmt(*s.then_br);
    if (s.else_br) check_stmt(*s.else_br);
}

void Sema::check_while(const ast::WhileStmt& s) {
    TypeId ct = check_expr(*s.cond);
    require_bool(ct, s.cond->loc, "while condition");
    bool saved = in_loop_;
    in_loop_ = true;
    check_stmt(*s.body);
    in_loop_ = saved;
}

void Sema::check_do_while(const ast::DoWhileStmt& s) {
    bool saved = in_loop_;
    in_loop_ = true;
    check_stmt(*s.body);
    in_loop_ = saved;
    TypeId ct = check_expr(*s.cond);
    require_bool(ct, s.cond->loc, "do-while condition");
}

void Sema::check_for_in(const ast::ForInStmt& s) {
    TypeId iter_t = check_expr(*s.iterable);
    (void)iter_t; // accept any iterable for now

    scopes_.push();
    Symbol var;
    var.name = s.var_name;
    var.kind = SymKind::Var;
    var.type = type_from_te(s.var_type);
    var.loc  = s.var_type.loc;
    scopes_.define(s.var_name, var);

    bool saved = in_loop_;
    in_loop_ = true;
    check_stmt(*s.body);
    in_loop_ = saved;
    scopes_.pop();
}

void Sema::check_for_c(const ast::ForCStmt& s) {
    scopes_.push();
    TypeId vt = type_from_te(s.var_type);
    Symbol var;
    var.name = s.var_name;
    var.kind = SymKind::Var;
    var.type = vt;
    scopes_.define(s.var_name, var);

    TypeId init_t = check_expr(*s.init);
    require_assignable(init_t, vt, s.init->loc, "for init");
    check_expr(*s.cond);
    check_expr(*s.incr);

    bool saved = in_loop_;
    in_loop_ = true;
    check_stmt(*s.body);
    in_loop_ = saved;
    scopes_.pop();
}

void Sema::check_switch(const ast::SwitchStmt& s) {
    check_expr(*s.expr);
    bool saved = in_loop_;
    in_loop_ = true;
    for (const auto& c : s.cases) {
        if (c.value) check_expr(**c.value);
        scopes_.push();
        check_stmts(c.body);
        scopes_.pop();
    }
    in_loop_ = saved;
}

void Sema::check_match(const ast::MatchStmt& s) {
    TypeId expr_t = check_expr(*s.expr);

    for (const auto& arm : s.arms) {
        // Validate enum variant patterns against the matched expression type
        if (arm.pattern.kind == ast::MatchPattern::Kind::EnumVariant) {
            // Verify the enum exists and has this variant
            Symbol* enum_sym = scopes_.lookup(arm.pattern.enum_name);
            if (!enum_sym ||
                types_.info(enum_sym->type).kind != TypeKind::Enum) {
                err(arm.loc, "'" + arm.pattern.enum_name + "' is not an enum type");
            } else {
                bool found = false;
                for (const auto& v : types_.info(enum_sym->type).variants) {
                    if (v.name == arm.pattern.variant_name) { found = true; break; }
                }
                if (!found)
                    err(arm.loc, "enum '" + arm.pattern.enum_name +
                        "' has no variant '" + arm.pattern.variant_name + "'");
            }
        }
        // Type-check the arm body
        scopes_.push();
        check_stmts(arm.body);
        scopes_.pop();
    }
    (void)expr_t;
}

void Sema::check_try_catch(const ast::TryCatchStmt& s) {
    check_stmt(*s.try_body);
    scopes_.push();
    if (!s.catch_all && s.catch_var) {
        Symbol cv;
        cv.name = *s.catch_var;
        cv.kind = SymKind::Var;
        cv.type = s.catch_type ? type_from_te(*s.catch_type) : TR::TID_OBJECT;
        scopes_.define(*s.catch_var, cv);
    }
    check_stmt(*s.catch_body);
    scopes_.pop();
}

void Sema::check_return(const ast::ReturnStmt& s) {
    if (s.value) {
        TypeId val_t = check_expr(**s.value);
        require_assignable(val_t, current_return_type_, (*s.value)->loc,
                           "return value");
    } else {
        if (current_return_type_ != TR::TID_VOID &&
            current_return_type_ != TR::TID_UNKNOWN)
            warn(s.loc, "missing return value in non-void function");
    }
}

void Sema::check_var_decl(const ast::VarDeclStmt& s) {
    TypeId decl_type = type_from_te(s.type);
    bool   is_const  = s.is_const;

    for (const auto& [name, init_ptr] : s.decls) {
        TypeId var_type = decl_type;

        // const variables must have an initializer
        if (is_const && !init_ptr) {
            err(s.loc, "const variable '" + name + "' must have an initializer");
        }

        if (init_ptr) {
            TypeId init_t = check_expr(*init_ptr);
            if (var_type == TR::TID_UNKNOWN)
                var_type = init_t;  // type inference
            else
                require_assignable(init_t, var_type, init_ptr->loc,
                                   "variable '" + name + "' initialiser");
        }

        Symbol sym;
        sym.name     = name;
        sym.kind     = SymKind::Var;
        sym.type     = var_type;
        sym.is_const = is_const;
        sym.loc      = s.loc;
        if (!scopes_.define(name, sym))
            err(s.loc, "variable '" + name + "' already declared in this scope");
    }
}

void Sema::check_assert(const ast::AssertStmt& s) {
    TypeId t = check_expr(*s.cond);
    require_bool(t, s.cond->loc, "assert");
}

void Sema::check_delete(const ast::DeleteStmt& s) {
    TypeId t = check_expr(*s.expr);
    if (t != TR::TID_UNKNOWN && types_.is_numeric(t))
        warn(s.expr->loc, "'delete' on numeric value — did you mean a pointer?");
}

// ─── Expressions ─────────────────────────────────────────────────────────────

TypeId Sema::check_expr(const ast::Expr& e) {
    TypeId result = TR::TID_UNKNOWN;

    // Each branch uses a distinct name to avoid -Wshadow in the else-if chain.
    if (dynamic_cast<const ast::IntLitExpr*>(&e)) {
        result = TR::TID_INT;
    } else if (dynamic_cast<const ast::FloatLitExpr*>(&e)) {
        result = TR::TID_DOUBLE;
    } else if (dynamic_cast<const ast::StringLitExpr*>(&e)) {
        result = TR::TID_STR;
    } else if (dynamic_cast<const ast::BoolLitExpr*>(&e)) {
        result = TR::TID_BOOL;
    } else if (dynamic_cast<const ast::NullLitExpr*>(&e)) {
        result = TR::TID_NULL;
    } else if (auto* id_e = dynamic_cast<const ast::IdentExpr*>(&e)) {
        Symbol* sym = scopes_.lookup(id_e->name);
        if (!sym) {
            err(id_e->loc, "undefined identifier '" + id_e->name + "'");
        } else {
            result = sym->type;
        }
    } else if (dynamic_cast<const ast::ThisExpr*>(&e)) {
        result = current_class_type_;
    } else if (dynamic_cast<const ast::SuperExpr*>(&e)) {
        result = types_.info(current_class_type_).parent;
        if (result < 0) result = TR::TID_OBJECT;
    } else if (auto* bin_e  = dynamic_cast<const ast::BinaryExpr*>(&e)) {
        result = check_binary(*bin_e);
    } else if (auto* un_e   = dynamic_cast<const ast::UnaryExpr*>(&e)) {
        result = check_unary(*un_e);
    } else if (auto* asgn_e = dynamic_cast<const ast::AssignExpr*>(&e)) {
        result = check_assign(*asgn_e);
    } else if (auto* call_e = dynamic_cast<const ast::CallExpr*>(&e)) {
        result = check_call(*call_e);
    } else if (auto* mem_e  = dynamic_cast<const ast::MemberExpr*>(&e)) {
        result = check_member(*mem_e);
    } else if (auto* idx_e  = dynamic_cast<const ast::IndexExpr*>(&e)) {
        result = check_index(*idx_e);
    } else if (auto* new_e  = dynamic_cast<const ast::NewExpr*>(&e)) {
        result = check_new(*new_e);
    } else if (auto* lst_e  = dynamic_cast<const ast::ListExpr*>(&e)) {
        result = check_list(*lst_e);
    } else if (auto* dct_e  = dynamic_cast<const ast::DictExpr*>(&e)) {
        result = check_dict(*dct_e);
    } else if (dynamic_cast<const ast::FormatExpr*>(&e)) {
        result = TR::TID_STR;
    }
    // else: unknown Expr subtype — leave as TID_UNKNOWN

    e.type_id = result;
    return result;
}

TypeId Sema::check_assign(const ast::AssignExpr& e) {
    TypeId rhs = check_expr(*e.value);

    // For plain assignment to an undeclared identifier: implicit declaration
    if (e.op == "=" && dynamic_cast<const ast::IdentExpr*>(e.target.get())) {
        const auto& name =
            static_cast<const ast::IdentExpr&>(*e.target).name;
        if (!scopes_.lookup(name)) {
            // Implicit var decl — Python-style inside function bodies
            Symbol sym;
            sym.name = name;
            sym.kind = SymKind::Var;
            sym.type = rhs;
            sym.loc  = e.target->loc;
            scopes_.define(name, sym);
            e.target->type_id = rhs;
            return rhs;
        }
    }

    // Guard: reject any assignment (plain or compound) to a const variable
    if (auto* id_target = dynamic_cast<const ast::IdentExpr*>(e.target.get())) {
        if (Symbol* sym = scopes_.lookup(id_target->name)) {
            if (sym->is_const) {
                err(e.target->loc,
                    "cannot assign to const variable '" + id_target->name + "'");
                return sym->type;
            }
        }
    }

    TypeId lhs = check_expr(*e.target);

    // For compound assignment (+= etc.), LHS must be numeric (or str for +=)
    if (e.op != "=") {
        if (e.op == "+=" && lhs == TR::TID_STR) {
            require_assignable(rhs, TR::TID_STR, e.value->loc, "'+=' on str");
        } else {
            require_numeric(lhs, e.target->loc, "compound assignment target");
            require_numeric(rhs, e.value->loc,  "compound assignment value");
        }
        return lhs;
    }

    require_assignable(rhs, lhs, e.value->loc, "assignment");
    return lhs;
}

TypeId Sema::check_binary(const ast::BinaryExpr& e) {
    TypeId L = check_expr(*e.left);
    TypeId R = check_expr(*e.right);
    const std::string& op = e.op;

    // Arithmetic operators
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        if (op == "+" && (L == TR::TID_STR || R == TR::TID_STR)) {
            // String concatenation — either side can be str; warn on type mismatch
            if (L != TR::TID_STR)
                warn(e.left->loc,  "'+' with str: left operand is '" + types_.name_of(L) + "'");
            if (R != TR::TID_STR)
                warn(e.right->loc, "'+' with str: right operand is '" + types_.name_of(R) + "'");
            return TR::TID_STR;
        }
        require_numeric(L, e.left->loc,  "'" + op + "' left operand");
        require_numeric(R, e.right->loc, "'" + op + "' right operand");
        return types_.unify(L, R);
    }

    // Comparison operators → bool
    if (op == "==" || op == "!=" || op == "<" || op == ">" ||
        op == "<=" || op == ">=" ) {
        // Type compatibility is enforced loosely (warn, not error)
        if (L != TR::TID_UNKNOWN && R != TR::TID_UNKNOWN &&
            !types_.assignable(L, R) && !types_.assignable(R, L))
            warn(e.loc, "comparing '" + types_.name_of(L) + "' with '" +
                        types_.name_of(R) + "'");
        return TR::TID_BOOL;
    }

    // Logical operators → bool
    if (op == "&&" || op == "||") {
        require_bool(L, e.left->loc,  "'" + op + "' left operand");
        require_bool(R, e.right->loc, "'" + op + "' right operand");
        return TR::TID_BOOL;
    }

    // Range operators → object (range type)
    if (op == "..<" || op == "..=") {
        require_numeric(L, e.left->loc,  "range start");
        require_numeric(R, e.right->loc, "range end");
        return TR::TID_OBJECT; // range type — will be refined in M4
    }

    return TR::TID_UNKNOWN;
}

TypeId Sema::check_unary(const ast::UnaryExpr& e) {
    TypeId t = check_expr(*e.operand);
    const std::string& op = e.op;

    if (op == "!" || op == "not") {
        require_bool(t, e.operand->loc, "'" + op + "' operand");
        return TR::TID_BOOL;
    }
    if (op == "-" || op == "+") {
        require_numeric(t, e.operand->loc, "'" + op + "' operand");
        return t;
    }
    if (op == "~") {
        if (!types_.is_integral(t) && t != TR::TID_UNKNOWN)
            err(e.operand->loc, "'~' requires integral operand, got '" + types_.name_of(t) + "'");
        return t;
    }
    if (op == "++" || op == "--") {
        require_numeric(t, e.operand->loc, "'" + op + "' operand");
        return t;
    }
    return t;
}

TypeId Sema::check_call(const ast::CallExpr& e) {
    // Try to resolve callee to a symbol to check args.
    Symbol* sym = nullptr;

    if (auto* id = dynamic_cast<const ast::IdentExpr*>(e.callee.get())) {
        sym = scopes_.lookup(id->name);
        if (!sym) {
            err(id->loc, "undefined function '" + id->name + "'");
            // still check arg expressions
            for (const auto& a : e.args) check_expr(*a);
            return TR::TID_UNKNOWN;
        }
        id->type_id = sym->type;
    } else {
        // Member call or complex expression — check callee, skip signature check
        TypeId callee_t = check_expr(*e.callee);
        (void)callee_t;
        for (const auto& a : e.args) check_expr(*a);
        return TR::TID_UNKNOWN;
    }

    // Check argument count
    if (sym->kind == SymKind::Function || sym->kind == SymKind::BuiltIn) {
        if (!sym->params.empty() &&
            sym->params.size() != e.args.size()) {
            std::ostringstream os;
            os << "'" << sym->name << "' expects " << sym->params.size()
               << " argument(s), got " << e.args.size();
            err(e.loc, os.str());
        }
        // Check each argument type
        for (std::size_t i = 0; i < e.args.size(); ++i) {
            TypeId at = check_expr(*e.args[i]);
            if (i < sym->params.size()) {
                require_assignable(at, sym->params[i].second,
                    e.args[i]->loc,
                    "argument " + std::to_string(i + 1) + " to '" + sym->name + "'");
            }
        }
        return sym->return_type;
    }

    // Class constructor call
    if (sym->kind == SymKind::Class) {
        for (const auto& a : e.args) check_expr(*a);
        return sym->type;
    }

    for (const auto& a : e.args) check_expr(*a);
    return TR::TID_UNKNOWN;
}

TypeId Sema::check_member(const ast::MemberExpr& e) {
    // Enum variant access: `Color.Red`
    // The object is an identifier resolving to an enum type symbol.
    if (auto* id = dynamic_cast<const ast::IdentExpr*>(e.object.get())) {
        Symbol* sym = scopes_.lookup(id->name);
        if (sym && sym->kind == SymKind::Class &&
            types_.info(sym->type).kind == TypeKind::Enum) {
            TypeId enum_tid = sym->type;
            const TypeInfo& ti = types_.info(enum_tid);
            for (const auto& v : ti.variants) {
                if (v.name == e.member) {
                    e.object->type_id = enum_tid;
                    return enum_tid;
                }
            }
            err(e.loc, "enum '" + id->name + "' has no variant '" + e.member + "'");
            return TR::TID_UNKNOWN;
        }
    }

    TypeId obj_t = check_expr(*e.object);

    // Try to find the member in the class type info scope
    if (obj_t != TR::TID_UNKNOWN && obj_t >= 0) {
        // For now: if the object is a known class, allow any member access.
        // Full field resolution would require per-class symbol tables (M3 work).
    }
    // Return unknown — type will be refined during codegen
    return TR::TID_UNKNOWN;
}

TypeId Sema::check_index(const ast::IndexExpr& e) {
    TypeId obj_t = check_expr(*e.object);
    TypeId idx_t = check_expr(*e.index);

    if (obj_t == TR::TID_LIST) {
        if (!types_.is_integral(idx_t) && idx_t != TR::TID_UNKNOWN)
            err(e.index->loc,
                "list index must be integral, got '" + types_.name_of(idx_t) + "'");
        return TR::TID_OBJECT;
    }
    if (obj_t == TR::TID_DICT) {
        return TR::TID_OBJECT;
    }
    if (obj_t == TR::TID_STR) {
        return TR::TID_STR;
    }
    return TR::TID_UNKNOWN;
}

TypeId Sema::check_new(const ast::NewExpr& e) {
    TypeId t = type_from_te(e.type);
    if (t == TR::TID_UNKNOWN) {
        err(e.loc, "unknown type '" + e.type.name + "' in 'new'");
    }
    for (const auto& a : e.args) check_expr(*a);
    return t;
}

TypeId Sema::check_list(const ast::ListExpr& e) {
    for (const auto& elem : e.elements) check_expr(*elem);
    return TR::TID_LIST;
}

TypeId Sema::check_dict(const ast::DictExpr& e) {
    for (const auto& [k, v] : e.pairs) {
        check_expr(*k);
        check_expr(*v);
    }
    return TR::TID_DICT;
}

} // namespace dux::sema
