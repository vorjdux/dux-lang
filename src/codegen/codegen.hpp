#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Suppress LLVM header warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wshadow"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#pragma GCC diagnostic pop

// Forward declarations for less-used LLVM types
namespace llvm {
class Function;
class BasicBlock;
class Value;
class Type;
class StructType;
class DIBuilder;
} // namespace llvm

#include "ast/ast.hpp"
#include "sema/types.hpp"
#include "sema/symbol.hpp"

class Driver;

namespace dux::codegen {

using sema::TypeId;
using sema::TypeRegistry;
using sema::ScopeStack;
using sema::Symbol;
using sema::SymKind;

// ─── Per-class layout info ───────────────────────────────────────────────────

struct FieldInfo {
    std::string            name;
    TypeId                 type;
    unsigned               index; // struct field index (0 = vtable ptr)
    const ast::FieldDecl*  decl{nullptr}; // original AST node (for default init)
};

struct MethodInfo {
    std::string    name;
    unsigned       vtable_slot;
    llvm::Function* fn{nullptr};
    std::string    modifier;  // "get", "set", or ""
};

struct ClassLayout {
    std::string              class_name;
    std::string              parent_name;  // first non-interface base, empty if none
    llvm::StructType*        struct_type{nullptr};
    llvm::Value*             vtable_global{nullptr};
    std::vector<FieldInfo>   fields;   // excludes vtable ptr (includes inherited fields first)
    std::vector<MethodInfo>  methods;  // virtual methods (own only)
};

// ─── Loop context (for break/continue) ──────────────────────────────────────

struct LoopCtx {
    llvm::BasicBlock* header{nullptr};  // continue target
    llvm::BasicBlock* exit{nullptr};    // break target
    std::string       label;            // optional loop label (for break &label)
};

// ─── Codegen ─────────────────────────────────────────────────────────────────

class Codegen {
public:
    explicit Codegen(Driver& driver);
    ~Codegen();

    // Optimization level (0–3); must be set before run().
    void set_opt_level(int level) { opt_level_ = level; }

    // Enable DWARF debug info generation (must be set before run()).
    void set_debug(bool on) { emit_debug_ = on; }

    // Generate IR for the whole program. Returns true on success.
    bool run(const ast::Program& prog, const std::string& module_name = "dux_module");

    // Emit LLVM IR text to a file path ("-" or "" = stdout).
    bool emit_ir(const std::string& path) const;

    // Emit a native object file (.o).
    bool emit_object(const std::string& path) const;

    int error_count() const { return error_count_; }

private:
    Driver&      driver_;
    sema::TypeRegistry types_;

    // LLVM objects
    std::unique_ptr<llvm::LLVMContext> ctx_;
    std::unique_ptr<llvm::Module>      mod_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    // DWARF debug info builder (only active when emit_debug_ == true)
    std::unique_ptr<llvm::DIBuilder>   dibuilder_;
    llvm::DIFile*                      di_file_{nullptr};
    llvm::DICompileUnit*               di_cu_{nullptr};

    // Options
    int  opt_level_{0};
    bool emit_debug_{false};

    // Bookkeeping
    int error_count_{0};
    std::string source_file_;

    // ── Value environment (name → alloca) ────────────────────────────────
    std::vector<std::unordered_map<std::string, llvm::Value*>> env_;

    // Parallel to env_: each scope level holds allocas of str-typed variables
    // so env_pop can emit duxrt_str_release calls for them.
    std::vector<std::vector<llvm::Value*>> str_scopes_;

    // Unified LIFO cleanup stack for both RAII and defer.
    //
    // Each entry is one of:
    //   - Class RAII dtor:  alloca != nullptr, defer_body == nullptr
    //   - Defer block:      alloca == nullptr, defer_body != nullptr
    //   - C cleanup fn:     fn_to_call != nullptr
    //
    // Entries within a scope are processed in reverse-declaration order
    // (LIFO) so that later declarations are destroyed first, matching
    // C++ destructor ordering and Go's defer semantics.
    struct ScopeCleanup {
        llvm::Value*          alloca{nullptr};      // RAII: alloca holding heap ptr
        std::string           class_name;           // RAII class: dtor symbol prefix
        std::string           release_sym;          // RAII rc: runtime release fn name
        const ast::StmtList*  defer_body{nullptr};  // defer: statements to run
        llvm::Function*       fn_to_call{nullptr};  // no-arg cleanup function
    };
    std::vector<std::vector<ScopeCleanup>> cleanup_scopes_;

    // Cache of immortal DuxStr* globals for string literals (keyed by value).
    std::unordered_map<std::string, llvm::GlobalVariable*> str_lit_cache_;

    // Per-class layouts
    std::unordered_map<std::string, ClassLayout> layouts_;

    // Enum type registry: enum name → TypeId in codegen's TypeRegistry
    std::unordered_map<std::string, sema::TypeId> enum_types_;

    // Maps variable name → class name (for member access resolution)
    std::unordered_map<std::string, std::string> var_class_;

    // Typed-list tracking: maps variable name → DUXLIST_ELEM_* kind (1=i32).
    // Set when a 'list' variable is initialised from an all-primitive literal so
    // gen_for_in can emit direct typed-array access instead of duxrt_list_get.
    std::unordered_map<std::string, int> typed_list_vars_;

    // Transient flag: set by gen_list() before returning, consumed by gen_var_decl()
    // to record the elem_kind for the variable being initialised.
    int last_list_elem_kind_{0};

    // Maps alloca → its element type (needed for opaque-pointer loads)
    std::unordered_map<llvm::Value*, llvm::Type*> alloca_type_;

    // Loop stack for break/continue
    std::vector<LoopCtx> loop_stack_;

    // Unwind target stack for invoke-based EH.
    // Non-empty while inside a try body; each entry is the landing-pad block
    // that should receive exceptions from calls in that try scope.
    std::vector<llvm::BasicBlock*> lp_stack_;

    // Stdlib modules imported in this compilation unit (e.g. "math")
    std::unordered_set<std::string> stdlib_imports_;

    // User-defined modules imported via file (e.g. "utils" from utils.dux).
    // Calls like utils.fn(args) are dispatched through mangle("utils","fn").
    std::unordered_set<std::string> user_module_imports_;

    // Current function context
    llvm::Function* current_fn_{nullptr};
    TypeId          current_ret_type_{TypeRegistry::TID_VOID};
    std::string     current_class_;
    std::string     current_namespace_;  // set while generating a namespace body
    std::string     pending_label_;   // label from &label before a loop stmt
    int             lambda_counter_{0};  // counter for unique lambda names
    int             async_counter_{0};   // counter for unique async body/thunk names
    std::unordered_map<std::string, ast::TypeExpr> fn_var_types_; // fn-typed vars/params

    // Async codegen state:
    // Non-null while generating an async body function; holds the LLVM Value
    // for the DuxFuture* first parameter so gen_return can route through it.
    llvm::Value*    current_async_future_{nullptr};

    // ── Type lowering (#14) ──────────────────────────────────────────────
    llvm::Type* lower_type(TypeId tid);
    llvm::Type* lower_type_expr(const ast::TypeExpr& te);
    llvm::Type* ptr_type();   // opaque ptr (LLVM 15+ ptr)

    // ── Class layout building ────────────────────────────────────────────
    void build_layouts(const ast::DeclList& decls);
    void build_class_layout(const ast::ClassDecl& c);
    void build_enum_type(const ast::EnumDecl& e);

    // ── Hoisting pass: declare all functions/methods before bodies ───────
    void declare_functions(const ast::DeclList& decls, const std::string& prefix = "");
    void declare_class_methods(const ast::ClassDecl& c);
    llvm::Function* declare_function(const ast::FunctionDecl& f,
                                     const std::string& mangled);

    // ── Optimization pass (#37) ──────────────────────────────────────────
    void optimize();

    // ── LTO: merge runtime bitcode before optimisation ───────────────────
    // Loads DUXRT_BC_PATH, links runtime IR into mod_, marks linked
    // definitions available_externally so they are not emitted in the
    // output object (avoiding duplicate-symbol conflicts with duxrt.a).
    // No-op and returns true when DUXRT_BC_PATH is empty (LTO disabled).
    bool lto_merge_runtime();

    // ── DWARF debug info (#36) ───────────────────────────────────────────
    void debug_init(const std::string& source_path);
    llvm::DISubprogram* debug_func(const ast::FunctionDecl& f,
                                   llvm::Function* fn,
                                   const std::string& mangled);
    void debug_set_loc(const ast::SourceLoc& loc);

    // ── Stdlib module dispatch (#35) ─────────────────────────────────────
    // Returns non-null Value if callee is a stdlib module call (e.g. math.sqrt)
    llvm::Value* try_stdlib_call(const ast::CallExpr& e);
    void process_import(const ast::ImportDecl& imp);

    // ── Top-level codegen ────────────────────────────────────────────────
    void gen_decl(const ast::Decl& d, const std::string& prefix = "");
    void gen_func(const ast::FunctionDecl& f, const std::string& mangled,
                  TypeId class_type = TypeRegistry::TID_UNKNOWN);
    void gen_class(const ast::ClassDecl& c);
    void gen_enum(const ast::EnumDecl& e);
    void gen_namespace(const ast::NamespaceDecl& ns);
    // Ensure there is a proper i32 @main() entry point the C runtime can call
    void emit_main_wrapper();

    // ── Statement codegen ────────────────────────────────────────────────
    void gen_stmts(const ast::StmtList& stmts);
    void gen_stmt(const ast::Stmt& s);
    void gen_block(const ast::BlockStmt& b);
    void gen_if(const ast::IfStmt& s);
    void gen_while(const ast::WhileStmt& s);
    void gen_do_while(const ast::DoWhileStmt& s);
    void gen_for_in(const ast::ForInStmt& s);
    void gen_for_c(const ast::ForCStmt& s);
    void gen_switch(const ast::SwitchStmt& s);
    void gen_match(const ast::MatchStmt& s);
    void gen_try_catch(const ast::TryCatchStmt& s);
    void gen_throw(const ast::ThrowStmt& s);
    void gen_return(const ast::ReturnStmt& s);
    void gen_var_decl(const ast::VarDeclStmt& s);
    void gen_assert(const ast::AssertStmt& s);
    void gen_delete(const ast::DeleteStmt& s);
    void gen_defer(const ast::DeferStmt& s);

    // ── Expression codegen ───────────────────────────────────────────────
    llvm::Value* gen_expr(const ast::Expr& e);
    llvm::Value* gen_assign(const ast::AssignExpr& e);
    llvm::Value* gen_binary(const ast::BinaryExpr& e);
    llvm::Value* gen_unary(const ast::UnaryExpr& e);
    llvm::Value* gen_call(const ast::CallExpr& e);
    llvm::Value* gen_member(const ast::MemberExpr& e);
    llvm::Value* gen_index(const ast::IndexExpr& e);
    llvm::Value* gen_new(const ast::NewExpr& e);
    llvm::Value* gen_list(const ast::ListExpr& e);
    llvm::Value* gen_dict(const ast::DictExpr& e);
    llvm::Value* gen_lambda(const ast::LambdaExpr& e);
    llvm::Value* gen_await(const ast::AwaitExpr& e);

    // Async helpers
    void         gen_async_func(const ast::FunctionDecl& f, const std::string& mangled);

    // ── Helpers ──────────────────────────────────────────────────────────
    llvm::Value* load_var(const std::string& name, const ast::SourceLoc& loc);
    llvm::Value* lvalue_of(const ast::Expr& e);
    llvm::Value* coerce(llvm::Value* val, TypeId from, TypeId to);
    llvm::Value* coerce_to_llvm_type(llvm::Value* v, llvm::Type* target);
    llvm::Value* to_bool(llvm::Value* val, TypeId t);
    std::string  resolve_class_name(const ast::Expr& obj, TypeId obj_tid) const;

    // Environment helpers
    void   env_push();
    void   env_pop();
    // Pass tid=TID_STR to register this alloca for str release on scope exit.
    // Pass a class TypeId to register for RAII destructor call on scope exit.
    void   env_define(const std::string& name, llvm::Value* alloca,
                      TypeId tid = TypeRegistry::TID_UNKNOWN);
    llvm::Value* env_lookup(const std::string& name) const;

    // Scope cleanup helpers (RAII + defer)
    // Emit all cleanups across all active scopes in LIFO order (gen_return).
    void emit_all_scope_cleanups();
    // Emit one cleanup entry (RAII dtor or inline defer block).
    void emit_scope_cleanup(const ScopeCleanup& c);
    // Convenience: emit a null-guarded dtor + free for a class instance.
    void emit_dtor(llvm::Value* alloca, const std::string& class_name);

    // Create an alloca, register its element type, and define it in the current scope
    llvm::Value* make_alloca(llvm::Type* t, const std::string& name);

    // Mangling
    static std::string mangle(const std::string& cls, const std::string& method);

    // Emit a call or invoke depending on whether we're inside a try block.
    // Use this for all user-visible calls that may throw.
    llvm::Value* emit_call(llvm::FunctionCallee callee,
                           llvm::ArrayRef<llvm::Value*> args,
                           const std::string& name = "");

    // Runtime call helpers (intrinsics / duxrt stubs)
    llvm::Value* rt_malloc(llvm::Value* size);
    llvm::Value* rt_println(llvm::Value* v, TypeId t);
    llvm::Function* get_or_declare_rt(const std::string& name,
                                      llvm::Type* ret,
                                      std::vector<llvm::Type*> params,
                                      bool vararg = false);

    // String reference-counting helpers
    llvm::Value* str_literal(const std::string& s);   // immortal DuxStr* global
    llvm::Value* emit_str_retain(llvm::Value* v);
    void         emit_str_release(llvm::Value* v);
    // Retain v only when it is NOT already a consuming result (CallInst/GlobalVariable).
    llvm::Value* maybe_retain_str(llvm::Value* v);
    // Emit releases for every str alloca across all active scopes (used by gen_return).
    void         emit_all_str_releases();

    // List/dict reference-counting helpers (same consuming convention as maybe_retain_str)
    llvm::Value* maybe_retain_list(llvm::Value* v);
    llvm::Value* maybe_retain_dict(llvm::Value* v);
    // Null-guarded release call (used by emit_scope_cleanup for list/dict RAII).
    void         emit_rc_release(llvm::Value* slot, const std::string& release_sym);

    TypeId type_id_of(const ast::Expr& e) const;

    // Enum payload helpers
    // Returns the max payload arity across all variants of the given enum TypeInfo.
    int  enum_max_payload_arity(const sema::TypeInfo& ti) const;
    // Generate a heap-allocated enum value: { i32 tag, ptr field... }
    llvm::Value* gen_enum_ctor(const sema::TypeInfo& ti,
                               const sema::EnumVariantInfo& vi,
                               const std::vector<ast::ExprPtr>& args);
    // Box a value to ptr-sized slot (same scheme as async env).
    llvm::Value* box_to_ptr(llvm::Value* v);
    // Unbox a ptr slot back to the given LLVM type.
    llvm::Value* unbox_from_ptr(llvm::Value* v, llvm::Type* target);

    void err(const ast::SourceLoc& loc, const std::string& msg);
};

} // namespace dux::codegen
