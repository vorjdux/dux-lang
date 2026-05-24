#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dux::ast {

struct Visitor;

// ─── Source location (decoupled from bison) ───────────────────────────────────
struct SourceLoc {
    std::string file;
    int line{0}, col{0};
};

// ─── Base node ────────────────────────────────────────────────────────────────
struct Node {
    SourceLoc loc;
    virtual ~Node() = default;
    virtual void accept(Visitor&) const = 0;
};

// ─── Type expressions ─────────────────────────────────────────────────────────
struct TypeExpr {
    std::string name;          // "int", "str", "MyClass", etc.
    bool        is_const{false};
    SourceLoc   loc;
    std::vector<TypeExpr> fn_params;  // for function types: fn(T...) -> R
    std::string           fn_ret;     // for function types: return type name
    std::vector<TypeExpr> type_args;  // for generic instantiations: Stack<int>
};

// ─── Expressions ─────────────────────────────────────────────────────────────

struct Expr : Node {
    mutable int32_t type_id{-1};  // filled in by sema pass; -1 = unresolved
};
using ExprPtr  = std::unique_ptr<Expr>;
using ExprList = std::vector<ExprPtr>;

struct IntLitExpr final : Expr {
    long long value{};
    void accept(Visitor& v) const override;
};

struct LongLitExpr final : Expr {
    long long value{};
    void accept(Visitor& v) const override;
};

struct FloatLitExpr final : Expr {
    double value{};
    void accept(Visitor& v) const override;
};

struct RealLitExpr final : Expr {
    double value{};   // stored as double, lowered to f32 in codegen
    void accept(Visitor& v) const override;
};

struct StringLitExpr final : Expr {
    std::string value;
    void accept(Visitor& v) const override;
};

struct BoolLitExpr final : Expr {
    bool value{};
    void accept(Visitor& v) const override;
};

struct NullLitExpr final : Expr {
    void accept(Visitor& v) const override;
};

struct IdentExpr final : Expr {
    std::string name;
    void accept(Visitor& v) const override;
};

struct ThisExpr final : Expr {
    void accept(Visitor& v) const override;
};

struct SuperExpr final : Expr {
    void accept(Visitor& v) const override;
};

struct BinaryExpr final : Expr {
    std::string op;
    ExprPtr     left;
    ExprPtr     right;
    void accept(Visitor& v) const override;
};

struct UnaryExpr final : Expr {
    std::string op;
    ExprPtr     operand;
    bool        prefix{true};
    void accept(Visitor& v) const override;
};

struct AssignExpr final : Expr {
    std::string op;   // "=", "+=", "-=", etc.
    ExprPtr     target;
    ExprPtr     value;
    void accept(Visitor& v) const override;
};

struct CallExpr final : Expr {
    ExprPtr  callee;
    ExprList args;
    void accept(Visitor& v) const override;
};

struct MemberExpr final : Expr {
    ExprPtr     object;
    std::string member;
    void accept(Visitor& v) const override;
};

struct IndexExpr final : Expr {
    ExprPtr object;
    ExprPtr index;
    void accept(Visitor& v) const override;
};

struct NewExpr final : Expr {
    TypeExpr type;
    ExprList args;
    void accept(Visitor& v) const override;
};


struct ListExpr final : Expr {
    ExprList elements;
    void accept(Visitor& v) const override;
};

struct DictExpr final : Expr {
    std::vector<std::pair<ExprPtr, ExprPtr>> pairs;
    void accept(Visitor& v) const override;
};

// LambdaExpr is defined after Param and StmtPtr (see below).

// ─── Statements ──────────────────────────────────────────────────────────────

struct Stmt : Node {};
using StmtPtr  = std::unique_ptr<Stmt>;
using StmtList = std::vector<StmtPtr>;

struct BlockStmt final : Stmt {
    StmtList body;
    void accept(Visitor& v) const override;
};

struct ExprStmt final : Stmt {
    ExprPtr expr;
    void accept(Visitor& v) const override;
};

struct VarDeclStmt final : Stmt {
    TypeExpr                                     type;
    bool                                         is_const{false};
    bool                                         is_static{false};
    std::vector<std::pair<std::string, ExprPtr>> decls;
    void accept(Visitor& v) const override;
};

struct IfStmt final : Stmt {
    ExprPtr cond;
    StmtPtr then_br;
    StmtPtr else_br;   // may be null
    void accept(Visitor& v) const override;
};

struct WhileStmt final : Stmt {
    std::optional<std::string> label;
    ExprPtr                    cond;
    StmtPtr                    body;
    void accept(Visitor& v) const override;
};

struct DoWhileStmt final : Stmt {
    StmtPtr body;
    ExprPtr cond;
    void accept(Visitor& v) const override;
};

struct ForInStmt final : Stmt {
    TypeExpr    var_type;
    std::string var_name;
    ExprPtr     iterable;
    StmtPtr     body;
    void accept(Visitor& v) const override;
};

struct ForCStmt final : Stmt {
    TypeExpr    var_type;
    std::string var_name;
    ExprPtr     init;
    ExprPtr     cond;
    ExprPtr     incr;
    StmtPtr     body;
    void accept(Visitor& v) const override;
};

struct SwitchCase {
    std::optional<ExprPtr> value;   // nullopt == default
    StmtList               body;
};

struct SwitchStmt final : Stmt {
    ExprPtr                 expr;
    std::vector<SwitchCase> cases;
    void accept(Visitor& v) const override;
};

// ─── Match statement (enum/value pattern matching) ───────────────────────────

struct MatchPattern {
    enum class Kind { Wildcard, EnumVariant, IntLit, BoolLit, StrLit };
    Kind        kind{Kind::Wildcard};
    std::string enum_name;    // for EnumVariant: "Color"
    std::string variant_name; // for EnumVariant: "Red"
    long long   int_value{0}; // for IntLit
    bool        bool_value{false}; // for BoolLit
    std::string str_value;    // for StrLit
    SourceLoc   loc;
};

struct MatchArm {
    MatchPattern pattern;
    StmtList     body;
    SourceLoc    loc;
};

struct MatchStmt final : Stmt {
    ExprPtr                expr;
    std::vector<MatchArm>  arms;
    void accept(Visitor& v) const override;
};

struct TryCatchStmt final : Stmt {
    StmtPtr                    try_body;
    bool                       catch_all{false};
    std::optional<TypeExpr>    catch_type;
    std::optional<std::string> catch_var;
    StmtPtr                    catch_body;
    void accept(Visitor& v) const override;
};

struct ReturnStmt final : Stmt {
    std::optional<ExprPtr> value;
    void accept(Visitor& v) const override;
};

struct BreakStmt final : Stmt {
    std::optional<std::string> label;
    void accept(Visitor& v) const override;
};

struct ContinueStmt final : Stmt {
    std::optional<std::string> label;
    void accept(Visitor& v) const override;
};

struct AssertStmt final : Stmt {
    ExprPtr cond;
    void accept(Visitor& v) const override;
};

struct DeleteStmt final : Stmt {
    ExprPtr expr;
    void accept(Visitor& v) const override;
};

struct LabeledStmt final : Stmt {
    std::string label;
    StmtPtr     stmt;
    void accept(Visitor& v) const override;
};

struct DeferStmt final : Stmt {
    StmtList body;
    void accept(Visitor& v) const override;
};

struct ThrowStmt final : Stmt {
    ExprPtr expr;
    void accept(Visitor& v) const override;
};

struct UnsafeStmt final : Stmt {
    StmtList body;
    void accept(Visitor& v) const override;
};

// ─── Declarations ────────────────────────────────────────────────────────────

struct Decl : Node {};
using DeclPtr  = std::unique_ptr<Decl>;
using DeclList = std::vector<DeclPtr>;

struct Param {
    TypeExpr    type;
    std::string name;
};

struct LambdaExpr final : Expr {
    std::vector<Param> params;
    StmtPtr            body;          // always BlockStmt (expr body is wrapped in return)
    std::vector<std::string> captures; // filled by codegen
    std::optional<TypeExpr> explicit_ret;  // from fn(params) -> type { body } form
    std::string        inferred_ret;  // return type name, set by sema after type inference
    void accept(Visitor& v) const override;
};

struct Decorator {
    std::string name;   // "doc" or "doc::markdown"
    ExprList    args;
    SourceLoc   loc;
};

enum class AccessMod { None, Public, Private, Protected };

struct InitEntry {
    std::string field;
    ExprList    args;
};

struct TypeBound {
    std::string param;           // e.g. "T"
    std::string interface_name;  // e.g. "IComparable"
};

struct FunctionDecl final : Decl {
    TypeExpr                   return_type;
    std::string                name;
    std::vector<std::string>   type_params;   // generic type params, e.g. {"T"}
    std::vector<TypeBound>     type_bounds;   // bounds, e.g. {T: IComparable}
    std::vector<Param>         params;
    std::optional<std::string> modifier;   // "get" or "set"
    std::optional<BlockStmt>   body;
    bool                       is_ctor{false};
    bool                       is_dtor{false};
    bool                       is_static{false};
    std::vector<InitEntry>     init_list;
    void accept(Visitor& v) const override;
};

struct FieldDecl final : Decl {
    TypeExpr               type;
    std::string            name;
    std::optional<ExprPtr> init;
    bool                   is_static{false};
    void accept(Visitor& v) const override;
};

struct ClassMember {
    AccessMod              access{AccessMod::None};
    std::vector<Decorator> decorators;
    DeclPtr                decl;
};

struct BaseClass {
    AccessMod   access{AccessMod::None};
    std::string name;
};

struct ClassDecl final : Decl {
    std::vector<Decorator>   decorators;
    std::string              name;
    std::vector<std::string> type_params;  // generic type params, e.g. {"T"}
    std::vector<TypeBound>   type_bounds;  // bounds, e.g. {T: IComparable}
    std::vector<BaseClass>   bases;
    std::vector<ClassMember> members;
    void accept(Visitor& v) const override;
};

struct InterfaceDecl final : Decl {
    std::string              name;
    std::vector<ClassMember> members;
    void accept(Visitor& v) const override;
};

struct ImportDecl final : Decl {
    std::string              path;                 // dotted module path, e.g. "com.foo.utils"
    std::vector<std::string> symbols;              // empty=full, {"*"}=glob, else named list
    std::string              alias;                // optional rename: "import ... as alias"
    bool                     global_scope{false};  // true = inject into global scope (import {} from)
    void accept(Visitor& v) const override;
};

struct NamespaceDecl final : Decl {
    std::string name;
    DeclList    decls;
    StmtList    stmts;
    bool        is_package_decl{false}; // true = file-level "namespace foo.bar" with no body
    void accept(Visitor& v) const override;
};

struct ExternDecl final : Decl {
    std::string        abi;     // "C"
    TypeExpr           ret;
    std::string        name;
    std::vector<Param> params;
    void accept(Visitor& v) const override;
};

// ─── Enum declaration ─────────────────────────────────────────────────────────

struct EnumVariant {
    std::string            name;
    std::vector<TypeExpr>  payload;   // empty = simple (i32) variant
    SourceLoc              loc;
};

struct EnumDecl final : Decl {
    std::string              name;
    std::vector<EnumVariant> variants;
    void accept(Visitor& v) const override;
};

// ─── Program ─────────────────────────────────────────────────────────────────

struct Program final : Node {
    DeclList decls;
    StmtList stmts;
    void accept(Visitor& v) const override;
};

// ─── Visitor interface ────────────────────────────────────────────────────────

struct Visitor {
    virtual ~Visitor() = default;

    virtual void visit(const IntLitExpr&)    = 0;
    virtual void visit(const LongLitExpr&)   = 0;
    virtual void visit(const FloatLitExpr&)  = 0;
    virtual void visit(const RealLitExpr&)   = 0;
    virtual void visit(const StringLitExpr&) = 0;
    virtual void visit(const BoolLitExpr&)   = 0;
    virtual void visit(const NullLitExpr&)   = 0;
    virtual void visit(const IdentExpr&)     = 0;
    virtual void visit(const ThisExpr&)      = 0;
    virtual void visit(const SuperExpr&)     = 0;
    virtual void visit(const BinaryExpr&)    = 0;
    virtual void visit(const UnaryExpr&)     = 0;
    virtual void visit(const AssignExpr&)    = 0;
    virtual void visit(const CallExpr&)      = 0;
    virtual void visit(const MemberExpr&)    = 0;
    virtual void visit(const IndexExpr&)     = 0;
    virtual void visit(const NewExpr&)       = 0;
    virtual void visit(const ListExpr&)      = 0;
    virtual void visit(const DictExpr&)      = 0;
    virtual void visit(const LambdaExpr&)    = 0;

    virtual void visit(const BlockStmt&)     = 0;
    virtual void visit(const ExprStmt&)      = 0;
    virtual void visit(const VarDeclStmt&)   = 0;
    virtual void visit(const IfStmt&)        = 0;
    virtual void visit(const WhileStmt&)     = 0;
    virtual void visit(const DoWhileStmt&)   = 0;
    virtual void visit(const ForInStmt&)     = 0;
    virtual void visit(const ForCStmt&)      = 0;
    virtual void visit(const SwitchStmt&)    = 0;
    virtual void visit(const MatchStmt&)     = 0;
    virtual void visit(const TryCatchStmt&)  = 0;
    virtual void visit(const ReturnStmt&)    = 0;
    virtual void visit(const BreakStmt&)     = 0;
    virtual void visit(const ContinueStmt&)  = 0;
    virtual void visit(const AssertStmt&)    = 0;
    virtual void visit(const DeleteStmt&)    = 0;
    virtual void visit(const LabeledStmt&)   = 0;
    virtual void visit(const DeferStmt&)     = 0;
    virtual void visit(const ThrowStmt&)     = 0;
    virtual void visit(const UnsafeStmt&)    = 0;

    virtual void visit(const FunctionDecl&)  = 0;
    virtual void visit(const FieldDecl&)     = 0;
    virtual void visit(const ClassDecl&)     = 0;
    virtual void visit(const InterfaceDecl&) = 0;
    virtual void visit(const ImportDecl&)    = 0;
    virtual void visit(const NamespaceDecl&) = 0;
    virtual void visit(const ExternDecl&)    = 0;
    virtual void visit(const EnumDecl&)      = 0;

    virtual void visit(const Program&)       = 0;
};

} // namespace dux::ast
