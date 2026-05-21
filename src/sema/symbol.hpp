#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ast/ast.hpp"
#include "sema/types.hpp"

namespace dux::sema {

enum class SymKind { Var, Param, Function, Class, Interface, Namespace, BuiltIn };

struct Symbol {
    std::string  name;
    SymKind      kind{SymKind::Var};
    TypeId       type{TypeRegistry::TID_UNKNOWN};        // declared / inferred type
    TypeId       return_type{TypeRegistry::TID_UNKNOWN}; // for functions
    std::vector<std::pair<std::string, TypeId>> params;  // for functions
    ast::Node*   decl{nullptr};  // non-owning back-pointer
    ast::SourceLoc loc;
    bool         is_const{false};
};

class Scope {
public:
    explicit Scope(Scope* parent = nullptr, bool is_class_scope = false)
        : parent_(parent), is_class_(is_class_scope) {}

    bool           define(const std::string& name, Symbol sym);
    Symbol*        lookup_local(const std::string& name);
    const Symbol*  lookup_local(const std::string& name) const;
    Symbol*        lookup(const std::string& name);

    Scope* parent()         const { return parent_; }
    bool   is_class_scope() const { return is_class_; }

    void for_each(const std::function<void(const std::string&, const Symbol&)>& fn) const;

private:
    Scope* parent_{nullptr};
    bool   is_class_{false};
    std::unordered_map<std::string, Symbol> symbols_;
};

class ScopeStack {
public:
    ScopeStack();  // creates and pre-populates the global scope

    Scope& global_scope()  { return *scopes_.front(); }
    Scope& current_scope() { return *scopes_.back();  }

    void push(bool is_class = false);
    void pop();

    bool    define(const std::string& name, Symbol sym);
    Symbol* lookup(const std::string& name);

    std::size_t depth() const { return scopes_.size(); }

private:
    std::vector<std::unique_ptr<Scope>> scopes_;
};

} // namespace dux::sema
