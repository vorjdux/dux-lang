#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "ast/ast.hpp"

namespace dux::sema {

using TypeId = int32_t;

enum class TypeKind {
    Unknown, Void, Bool, Int, Long, Real, Double,
    Str, List, Dict, Tuple, Object, Null,
    Class, Interface, Function, Enum,
};

struct EnumVariantInfo {
    std::string          name;
    int32_t              tag;            // i32 discriminant (0-based)
    std::vector<TypeId>  payload;        // empty = simple variant
};

struct TypeInfo {
    TypeKind    kind{TypeKind::Unknown};
    std::string name;
    TypeId      parent{-1};              // for class types
    std::vector<TypeId> ifaces;          // implemented interfaces
    TypeId      return_type{-1};         // for function types
    std::vector<TypeId> param_types;     // for function types
    // For enum types:
    std::vector<EnumVariantInfo> variants;
};

class TypeRegistry {
public:
    static constexpr TypeId TID_UNKNOWN = -1;
    static constexpr TypeId TID_VOID    =  0;
    static constexpr TypeId TID_BOOL    =  1;
    static constexpr TypeId TID_INT     =  2;
    static constexpr TypeId TID_LONG    =  3;
    static constexpr TypeId TID_REAL    =  4;
    static constexpr TypeId TID_DOUBLE  =  5;
    static constexpr TypeId TID_STR     =  6;
    static constexpr TypeId TID_LIST    =  7;
    static constexpr TypeId TID_DICT    =  8;
    static constexpr TypeId TID_TUPLE   =  9;
    static constexpr TypeId TID_OBJECT  = 10;
    static constexpr TypeId TID_NULL    = 11;

    TypeRegistry();

    TypeId intern(const std::string& name, TypeKind kind = TypeKind::Class);

    const TypeInfo& info(TypeId id) const;
    TypeInfo&       info(TypeId id);

    TypeId from_name(const std::string& name) const;
    TypeId from_type_expr(const ast::TypeExpr& te) const;

    bool   assignable(TypeId from, TypeId to) const;
    TypeId unify(TypeId a, TypeId b) const;

    bool is_numeric(TypeId id) const;
    bool is_integral(TypeId id) const;
    bool is_subtype(TypeId child, TypeId ancestor) const;

    void set_parent(TypeId child, TypeId parent);
    void add_interface(TypeId cls, TypeId iface);

    std::string name_of(TypeId id) const;

private:
    std::vector<TypeInfo>              types_;
    std::unordered_map<std::string, TypeId> by_name_;

    TypeId add_builtin(TypeId id, TypeKind kind, const std::string& name,
                       TypeId parent = TID_OBJECT);
};

} // namespace dux::sema
