#include "ast.hpp"

namespace dux::ast {

void IntLitExpr::accept(Visitor& v)    const { v.visit(*this); }
void FloatLitExpr::accept(Visitor& v)  const { v.visit(*this); }
void StringLitExpr::accept(Visitor& v) const { v.visit(*this); }
void BoolLitExpr::accept(Visitor& v)   const { v.visit(*this); }
void NullLitExpr::accept(Visitor& v)   const { v.visit(*this); }
void IdentExpr::accept(Visitor& v)     const { v.visit(*this); }
void ThisExpr::accept(Visitor& v)      const { v.visit(*this); }
void SuperExpr::accept(Visitor& v)     const { v.visit(*this); }
void BinaryExpr::accept(Visitor& v)    const { v.visit(*this); }
void UnaryExpr::accept(Visitor& v)     const { v.visit(*this); }
void AssignExpr::accept(Visitor& v)    const { v.visit(*this); }
void CallExpr::accept(Visitor& v)      const { v.visit(*this); }
void MemberExpr::accept(Visitor& v)    const { v.visit(*this); }
void IndexExpr::accept(Visitor& v)     const { v.visit(*this); }
void NewExpr::accept(Visitor& v)       const { v.visit(*this); }
void ListExpr::accept(Visitor& v)      const { v.visit(*this); }
void DictExpr::accept(Visitor& v)      const { v.visit(*this); }

void BlockStmt::accept(Visitor& v)     const { v.visit(*this); }
void ExprStmt::accept(Visitor& v)      const { v.visit(*this); }
void VarDeclStmt::accept(Visitor& v)   const { v.visit(*this); }
void IfStmt::accept(Visitor& v)        const { v.visit(*this); }
void WhileStmt::accept(Visitor& v)     const { v.visit(*this); }
void DoWhileStmt::accept(Visitor& v)   const { v.visit(*this); }
void ForInStmt::accept(Visitor& v)     const { v.visit(*this); }
void ForCStmt::accept(Visitor& v)      const { v.visit(*this); }
void SwitchStmt::accept(Visitor& v)    const { v.visit(*this); }
void TryCatchStmt::accept(Visitor& v)  const { v.visit(*this); }
void ReturnStmt::accept(Visitor& v)    const { v.visit(*this); }
void BreakStmt::accept(Visitor& v)     const { v.visit(*this); }
void ContinueStmt::accept(Visitor& v)  const { v.visit(*this); }
void AssertStmt::accept(Visitor& v)    const { v.visit(*this); }
void DeleteStmt::accept(Visitor& v)    const { v.visit(*this); }
void LabeledStmt::accept(Visitor& v)   const { v.visit(*this); }

void FunctionDecl::accept(Visitor& v)  const { v.visit(*this); }
void FieldDecl::accept(Visitor& v)     const { v.visit(*this); }
void ClassDecl::accept(Visitor& v)     const { v.visit(*this); }
void InterfaceDecl::accept(Visitor& v) const { v.visit(*this); }
void ImportDecl::accept(Visitor& v)    const { v.visit(*this); }
void NamespaceDecl::accept(Visitor& v) const { v.visit(*this); }

void Program::accept(Visitor& v)       const { v.visit(*this); }

} // namespace dux::ast
