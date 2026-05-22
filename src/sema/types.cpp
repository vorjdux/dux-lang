#include "sema/types.hpp"
#include <stdexcept>

namespace dux::sema {

TypeRegistry::TypeRegistry() {
    // Pre-allocate 12 built-in slots so indices match TID_* constants.
    types_.resize(12);
    add_builtin(TID_VOID,   TypeKind::Void,   "void",   -1);
    add_builtin(TID_BOOL,   TypeKind::Bool,   "bool",   TID_OBJECT);
    add_builtin(TID_INT,    TypeKind::Int,    "int",    TID_OBJECT);
    add_builtin(TID_LONG,   TypeKind::Long,   "long",   TID_OBJECT);
    add_builtin(TID_REAL,   TypeKind::Real,   "real",   TID_OBJECT);
    add_builtin(TID_DOUBLE, TypeKind::Double, "double", TID_OBJECT);
    add_builtin(TID_STR,    TypeKind::Str,    "str",    TID_OBJECT);
    add_builtin(TID_LIST,   TypeKind::List,   "list",   TID_OBJECT);
    add_builtin(TID_DICT,   TypeKind::Dict,   "dict",   TID_OBJECT);
    add_builtin(TID_TUPLE,  TypeKind::Tuple,  "tuple",  TID_OBJECT);
    add_builtin(TID_OBJECT, TypeKind::Object, "object", -1);
    add_builtin(TID_NULL,   TypeKind::Null,   "null",   -1);
}

TypeId TypeRegistry::add_builtin(TypeId id, TypeKind kind,
                                 const std::string& name, TypeId parent) {
    TypeInfo& ti = types_[static_cast<std::size_t>(id)];
    ti.kind   = kind;
    ti.name   = name;
    ti.parent = parent;
    by_name_[name] = id;
    return id;
}

TypeId TypeRegistry::intern(const std::string& name, TypeKind kind) {
    auto it = by_name_.find(name);
    if (it != by_name_.end()) return it->second;
    TypeId id = static_cast<TypeId>(types_.size());
    TypeInfo ti;
    ti.kind   = kind;
    ti.name   = name;
    ti.parent = TID_OBJECT;
    types_.push_back(std::move(ti));
    by_name_[name] = id;
    return id;
}

const TypeInfo& TypeRegistry::info(TypeId id) const {
    if (id < 0 || id >= static_cast<TypeId>(types_.size()))
        return types_[0]; // return void info as fallback
    return types_[static_cast<std::size_t>(id)];
}

TypeInfo& TypeRegistry::info(TypeId id) {
    if (id < 0 || id >= static_cast<TypeId>(types_.size()))
        return types_[0];
    return types_[static_cast<std::size_t>(id)];
}

TypeId TypeRegistry::from_name(const std::string& name) const {
    if (name == "ptr") return TID_OBJECT;
    auto it = by_name_.find(name);
    return it != by_name_.end() ? it->second : TID_UNKNOWN;
}

TypeId TypeRegistry::from_type_expr(const ast::TypeExpr& te) const {
    return from_name(te.name);
}

bool TypeRegistry::is_numeric(TypeId id) const {
    return id == TID_INT || id == TID_LONG || id == TID_REAL ||
           id == TID_DOUBLE || id == TID_BOOL;
}

bool TypeRegistry::is_integral(TypeId id) const {
    return id == TID_INT || id == TID_LONG || id == TID_BOOL;
}

bool TypeRegistry::is_subtype(TypeId child, TypeId ancestor) const {
    if (child == ancestor) return true;
    if (ancestor == TID_OBJECT) return true;
    if (child == TID_NULL)      return true;   // null fits any class slot
    if (child  == TID_UNKNOWN || ancestor == TID_UNKNOWN) return true;

    // Walk the parent chain
    TypeId cur = child;
    while (cur != TID_UNKNOWN && cur >= 0) {
        if (cur == ancestor) return true;
        const TypeInfo& ti = info(cur);
        if (ti.parent == cur) break;  // avoid infinite loop
        cur = ti.parent;
    }
    return false;
}

bool TypeRegistry::assignable(TypeId from, TypeId to) const {
    if (from == TID_UNKNOWN || to == TID_UNKNOWN) return true;
    if (from == to) return true;
    if (to == TID_OBJECT) return true;
    if (from == TID_NULL) {
        // null is assignable to any class/interface/list/dict/str
        const TypeInfo& ti = info(to);
        return ti.kind == TypeKind::Class || ti.kind == TypeKind::Interface ||
               to == TID_LIST || to == TID_DICT || to == TID_STR;
    }
    // Any numeric type is assignable to any other numeric type (narrowing allowed)
    if (is_numeric(from) && is_numeric(to)) return true;
    // list literal can initialize a tuple
    if (from == TID_LIST && to == TID_TUPLE) return true;

    // Class subtyping
    return is_subtype(from, to);
}

TypeId TypeRegistry::unify(TypeId a, TypeId b) const {
    if (a == TID_UNKNOWN) return b;
    if (b == TID_UNKNOWN) return a;
    if (a == b)           return a;
    if (a == TID_NULL)    return b;
    if (b == TID_NULL)    return a;

    // Numeric promotions
    auto rank = [](TypeId t) -> int {
        switch (t) {
            case TID_BOOL:   return 1;
            case TID_INT:    return 2;
            case TID_LONG:   return 3;
            case TID_REAL:   return 4;
            case TID_DOUBLE: return 5;
            default:         return 0;
        }
    };
    int ra = rank(a), rb = rank(b);
    if (ra > 0 && rb > 0) return ra >= rb ? a : b;

    // Class hierarchy: find common ancestor
    if (is_subtype(a, b)) return b;
    if (is_subtype(b, a)) return a;
    return TID_OBJECT;
}

void TypeRegistry::set_parent(TypeId child, TypeId parent) {
    if (child >= 0 && child < static_cast<TypeId>(types_.size()))
        types_[static_cast<std::size_t>(child)].parent = parent;
}

void TypeRegistry::add_interface(TypeId cls, TypeId iface) {
    if (cls >= 0 && cls < static_cast<TypeId>(types_.size()))
        types_[static_cast<std::size_t>(cls)].ifaces.push_back(iface);
}

std::string TypeRegistry::name_of(TypeId id) const {
    if (id == TID_UNKNOWN) return "<unknown>";
    if (id < 0 || id >= static_cast<TypeId>(types_.size())) return "<invalid>";
    return types_[static_cast<std::size_t>(id)].name;
}

} // namespace dux::sema
