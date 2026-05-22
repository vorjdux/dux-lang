#pragma once
#include "ast/ast.hpp"

class Driver;

namespace dux::sema {

// Expand all generic class/function instantiations in the program.
//
// This pass runs before sema and codegen. It:
//   1. Collects all ClassDecl/FunctionDecl nodes that have type_params.
//   2. Walks the entire AST to find TypeExprs with type_args (e.g. Stack<int>).
//   3. For each unique instantiation, clones the generic definition,
//      substitutes type parameters with concrete types, and adds the
//      concrete definition to prog.decls under a mangled name (Stack__int).
//   4. Replaces every TypeExpr with type_args by the corresponding mangled
//      TypeExpr (name = mangled, type_args = {}).
//   5. Removes the original generic definitions from prog.decls.
//
// After this pass the program contains only concrete definitions and sema/
// codegen need no special handling for generics.
// driver is used to emit errors (e.g. unsatisfied type bounds).
void expand_generics(ast::Program& prog, Driver& driver);

// Compute the mangled name for a generic instantiation.
// e.g. mangle_generic("Stack", [int]) → "Stack__int"
std::string mangle_generic(const std::string& name,
                           const std::vector<ast::TypeExpr>& args);

} // namespace dux::sema
