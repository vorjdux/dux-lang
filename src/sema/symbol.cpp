#include "sema/symbol.hpp"

namespace dux::sema {

// ─── Scope ───────────────────────────────────────────────────────────────────

bool Scope::define(const std::string& name, Symbol sym) {
    auto [it, inserted] = symbols_.emplace(name, std::move(sym));
    return inserted;
}

Symbol* Scope::lookup_local(const std::string& name) {
    auto it = symbols_.find(name);
    return it != symbols_.end() ? &it->second : nullptr;
}

const Symbol* Scope::lookup_local(const std::string& name) const {
    auto it = symbols_.find(name);
    return it != symbols_.end() ? &it->second : nullptr;
}

Symbol* Scope::lookup(const std::string& name) {
    if (auto* s = lookup_local(name)) return s;
    return parent_ ? parent_->lookup(name) : nullptr;
}

void Scope::for_each(
    const std::function<void(const std::string&, const Symbol&)>& fn) const {
    for (const auto& [k, v] : symbols_) fn(k, v);
}

// ─── ScopeStack ──────────────────────────────────────────────────────────────

ScopeStack::ScopeStack() {
    scopes_.push_back(std::make_unique<Scope>(nullptr, false));
}

void ScopeStack::push(bool is_class) {
    scopes_.push_back(
        std::make_unique<Scope>(scopes_.back().get(), is_class));
}

void ScopeStack::pop() {
    if (scopes_.size() > 1) scopes_.pop_back();
}

bool ScopeStack::define(const std::string& name, Symbol sym) {
    return scopes_.back()->define(name, std::move(sym));
}

Symbol* ScopeStack::lookup(const std::string& name) {
    return scopes_.back()->lookup(name);
}

} // namespace dux::sema
