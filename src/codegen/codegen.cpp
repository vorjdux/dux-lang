#include "codegen/codegen.hpp"
#include "driver/driver.hpp"

// Suppress LLVM header warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/IR/LegacyPassManager.h>
// New pass manager (optimization pipeline)
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#pragma GCC diagnostic pop

#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace dux::codegen {

using TR = sema::TypeRegistry;
using llvm::Value;
using llvm::Type;
using llvm::Function;
using llvm::BasicBlock;

// ─── Constructor / Destructor ────────────────────────────────────────────────

Codegen::Codegen(Driver& driver) : driver_(driver) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    ctx_        = std::make_unique<llvm::LLVMContext>();
    mod_        = std::make_unique<llvm::Module>("dux", *ctx_);
    builder_    = std::make_unique<llvm::IRBuilder<>>(*ctx_);
    source_file_ = driver_.filename();
}

Codegen::~Codegen() = default;

// ─── Entry point ─────────────────────────────────────────────────────────────

bool Codegen::run(const ast::Program& prog, const std::string& module_name) {
    mod_->setModuleIdentifier(module_name);
    mod_->setSourceFileName(module_name);
    mod_->setTargetTriple(llvm::sys::getDefaultTargetTriple());

    // DWARF: initialise debug info builder
    if (emit_debug_) debug_init(source_file_);

    env_push(); // global env

    // Pass 1: process imports (stdlib + file)
    for (const auto& d : prog.decls)
        if (auto* imp = dynamic_cast<const ast::ImportDecl*>(d.get()))
            process_import(*imp);

    // Pass 2: build struct layouts for all classes
    build_layouts(prog.decls);

    // Pass 3: declare all top-level functions (forward declare)
    declare_functions(prog.decls);

    // Pass 4: generate bodies
    for (const auto& d : prog.decls) gen_decl(*d);
    gen_stmts(prog.stmts);

    env_pop();

    // Finalise DWARF metadata
    if (emit_debug_ && dibuilder_) dibuilder_->finalize();

    // Wrap void @main() → i32 @main()
    emit_main_wrapper();

    // Run optimisation passes
    if (opt_level_ > 0) optimize();

    // Verify the module
    std::string err_str;
    llvm::raw_string_ostream es(err_str);
    if (llvm::verifyModule(*mod_, &es)) {
        ast::SourceLoc sl;
        err(sl, "LLVM module verification failed: " + es.str());
        return false;
    }
    return error_count_ == 0;
}

// ─── Emit ────────────────────────────────────────────────────────────────────

bool Codegen::emit_ir(const std::string& path) const {
    if (path.empty() || path == "-" || path == "/dev/null") {
        if (path != "/dev/null") mod_->print(llvm::outs(), nullptr);
        return true;
    }
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
    if (ec) return false;
    mod_->print(out, nullptr);
    return true;
}

bool Codegen::emit_object(const std::string& path) const {
    std::string triple = llvm::sys::getDefaultTargetTriple();
    std::string err_str;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err_str);
    if (!target) return false;

    llvm::TargetOptions opts;
    auto tm = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(triple, "generic", "", opts,
                                    llvm::Reloc::PIC_));
    mod_->setDataLayout(tm->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
    if (ec) return false;

    llvm::legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, out, nullptr,
                                llvm::CodeGenFileType::ObjectFile))
        return false;
    pm.run(*mod_);
    return true;
}

// ─── Diagnostics ─────────────────────────────────────────────────────────────

void Codegen::err(const ast::SourceLoc& loc, const std::string& msg) {
    driver_.error(loc, msg);
    ++error_count_;
}

// ─── Type lowering (#14) ─────────────────────────────────────────────────────

llvm::Type* Codegen::ptr_type() {
    return llvm::PointerType::getUnqual(*ctx_);
}

llvm::Type* Codegen::lower_type(TypeId tid) {
    switch (tid) {
        case TR::TID_VOID:   return llvm::Type::getVoidTy(*ctx_);
        case TR::TID_BOOL:   return llvm::Type::getInt1Ty(*ctx_);
        case TR::TID_INT:    return llvm::Type::getInt32Ty(*ctx_);
        case TR::TID_LONG:   return llvm::Type::getInt64Ty(*ctx_);
        case TR::TID_REAL:   return llvm::Type::getFloatTy(*ctx_);
        case TR::TID_DOUBLE: return llvm::Type::getDoubleTy(*ctx_);
        case TR::TID_STR:    return ptr_type();   // DuxStr*
        case TR::TID_LIST:   return ptr_type();   // DuxList*
        case TR::TID_DICT:   return ptr_type();   // DuxDict*
        case TR::TID_TUPLE:  return ptr_type();
        case TR::TID_OBJECT: return ptr_type();
        case TR::TID_NULL:   return ptr_type();
        default:
            return ptr_type(); // user-defined class types are heap-allocated
    }
}

llvm::Type* Codegen::lower_type_expr(const ast::TypeExpr& te) {
    TypeId tid = types_.from_type_expr(te);
    return lower_type(tid == TR::TID_UNKNOWN ? TR::TID_OBJECT : tid);
}

// ─── Class layout building ────────────────────────────────────────────────────

void Codegen::build_layouts(const ast::DeclList& decls) {
    for (const auto& dp : decls) {
        if (auto* c = dynamic_cast<const ast::ClassDecl*>(dp.get()))
            build_class_layout(*c);
        else if (auto* ns = dynamic_cast<const ast::NamespaceDecl*>(dp.get()))
            build_layouts(ns->decls);
    }
}

void Codegen::build_class_layout(const ast::ClassDecl& c) {
    ClassLayout layout;
    layout.class_name = c.name;

    // Collect fields
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* fd = dynamic_cast<const ast::FieldDecl*>(m.decl.get())) {
            FieldInfo fi;
            fi.name  = fd->name;
            fi.type  = types_.from_type_expr(fd->type);
            fi.index = static_cast<unsigned>(layout.fields.size()) + 1; // +1 for vtable
            fi.decl  = fd;
            layout.fields.push_back(fi);
        }
    }

    // Build LLVM struct: { ptr vtable, field0, field1, ... }
    std::vector<llvm::Type*> field_types;
    field_types.push_back(ptr_type()); // vtable pointer
    for (const auto& f : layout.fields)
        field_types.push_back(lower_type(f.type));

    layout.struct_type = llvm::StructType::create(*ctx_, field_types, c.name);

    // Collect virtual methods
    unsigned slot = 0;
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            if (!f->is_ctor && !f->is_dtor) {
                MethodInfo mi;
                mi.name        = f->name;
                mi.vtable_slot = slot++;
                layout.methods.push_back(mi);
            }
        }
    }

    layouts_[c.name] = std::move(layout);

    // Register class type in sema type registry
    types_.intern(c.name, sema::TypeKind::Class);
}

// ─── Function declaration (forward declare) ──────────────────────────────────

std::string Codegen::mangle(const std::string& cls, const std::string& method) {
    return cls.empty() ? method : cls + "__" + method;
}

Function* Codegen::declare_function(const ast::FunctionDecl& f,
                                    const std::string& mangled) {
    // Build LLVM function type
    std::vector<llvm::Type*> param_types;
    for (const auto& p : f.params)
        param_types.push_back(lower_type_expr(p.type));

    TypeId ret_tid = types_.from_type_expr(f.return_type);
    llvm::Type* ret_type = lower_type(ret_tid == TR::TID_UNKNOWN
                                     ? TR::TID_VOID : ret_tid);

    auto* ft = llvm::FunctionType::get(ret_type, param_types, false);
    auto* fn = Function::Create(ft, Function::ExternalLinkage, mangled, *mod_);

    // Name parameters
    unsigned idx = 0;
    for (auto& arg : fn->args())
        arg.setName(f.params[idx++].name);

    return fn;
}

void Codegen::declare_functions(const ast::DeclList& decls,
                                const std::string& prefix) {
    for (const auto& dp : decls) {
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(dp.get())) {
            if (!f->is_ctor && !f->is_dtor)
                declare_function(*f, mangle(prefix, f->name));
        } else if (auto* c = dynamic_cast<const ast::ClassDecl*>(dp.get())) {
            declare_class_methods(*c);
        } else if (auto* ns = dynamic_cast<const ast::NamespaceDecl*>(dp.get())) {
            declare_functions(ns->decls, ns->name);
        }
    }
}

void Codegen::declare_class_methods(const ast::ClassDecl& c) {
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            std::string mangled = mangle(c.name, f->name);
            if (mod_->getFunction(mangled)) continue;

            // For methods: prepend 'this' pointer as first parameter
            std::vector<llvm::Type*> param_types;
            param_types.push_back(ptr_type()); // this
            for (const auto& p : f->params)
                param_types.push_back(lower_type_expr(p.type));

            TypeId ret_tid = types_.from_type_expr(f->return_type);
            llvm::Type* ret_t = lower_type(ret_tid == TR::TID_UNKNOWN
                                          ? TR::TID_VOID : ret_tid);

            auto* ft = llvm::FunctionType::get(ret_t, param_types, false);
            auto* fn = Function::Create(ft, Function::ExternalLinkage,
                                        mangled, *mod_);
            fn->arg_begin()->setName("this");
            unsigned idx = 1;
            for (auto it = std::next(fn->arg_begin()); it != fn->arg_end(); ++it)
                it->setName(f->params[idx++ - 1].name);

            // Record in layout
            if (layouts_.count(c.name)) {
                for (auto& mi : layouts_[c.name].methods) {
                    if (mi.name == f->name) { mi.fn = fn; break; }
                }
            }
        }
    }
}

// ─── Top-level codegen ───────────────────────────────────────────────────────

void Codegen::gen_decl(const ast::Decl& d, const std::string& prefix) {
    if (auto* f  = dynamic_cast<const ast::FunctionDecl*>(&d)) {
        if (!f->is_ctor && !f->is_dtor)
            gen_func(*f, mangle(prefix, f->name));
    }
    else if (auto* c  = dynamic_cast<const ast::ClassDecl*>(&d))   gen_class(*c);
    else if (auto* ns = dynamic_cast<const ast::NamespaceDecl*>(&d)) gen_namespace(*ns);
    // ImportDecl: nothing to generate
}

void Codegen::gen_func(const ast::FunctionDecl& f, const std::string& mangled,
                       TypeId class_type) {
    if (!f.body) return;

    Function* fn = mod_->getFunction(mangled);
    if (!fn) {
        fn = declare_function(f, mangled);
    }
    if (!fn->empty()) return; // already generated

    BasicBlock* entry = BasicBlock::Create(*ctx_, "entry", fn);
    builder_->SetInsertPoint(entry);

    debug_func(f, fn, mangled);

    var_class_.clear(); // var→class map is local to each function body
    env_push();

    // If method: bind 'this'
    auto arg_it = fn->arg_begin();
    if (class_type != TR::TID_UNKNOWN) {
        // First arg is 'this'
        Value* this_alloca = make_alloca(ptr_type(), "this.addr");
        builder_->CreateStore(&*arg_it, this_alloca);
        env_define("this", this_alloca);
        ++arg_it;
    }

    // Allocate params
    for (; arg_it != fn->arg_end(); ++arg_it) {
        std::string pname = std::string(arg_it->getName()) + ".addr";
        Value* alloca = make_alloca(arg_it->getType(), pname);
        builder_->CreateStore(&*arg_it, alloca);
        env_define(std::string(arg_it->getName()), alloca);
    }

    // Save context
    auto saved_fn   = current_fn_;
    auto saved_ret  = current_ret_type_;
    auto saved_cls  = current_class_;
    current_fn_     = fn;
    current_ret_type_ = types_.from_type_expr(f.return_type);
    if (current_ret_type_ == TR::TID_UNKNOWN) current_ret_type_ = TR::TID_VOID;

    gen_stmts(f.body->body);

    // Emit implicit return if the block doesn't have a terminator
    if (!builder_->GetInsertBlock()->getTerminator()) {
        if (current_ret_type_ == TR::TID_VOID)
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(llvm::Constant::getNullValue(
                lower_type(current_ret_type_)));
    }

    env_pop();
    current_fn_  = saved_fn;
    current_ret_type_ = saved_ret;
    current_class_ = saved_cls;
}

void Codegen::gen_class(const ast::ClassDecl& c) {
    ClassLayout& layout = layouts_[c.name];

    // Emit vtable as a global array of function pointers
    std::vector<llvm::Constant*> vtable_entries;
    for (const auto& mi : layout.methods) {
        if (mi.fn)
            vtable_entries.push_back(
                llvm::ConstantExpr::getBitCast(mi.fn, ptr_type()));
        else
            vtable_entries.push_back(llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptr_type())));
    }

    if (!vtable_entries.empty()) {
        auto* arr_ty = llvm::ArrayType::get(ptr_type(),
                                            vtable_entries.size());
        auto* vtable_init = llvm::ConstantArray::get(arr_ty, vtable_entries);
        auto* vtable_gv   = new llvm::GlobalVariable(
            *mod_, arr_ty, /*isConstant=*/true,
            llvm::GlobalValue::InternalLinkage, vtable_init,
            c.name + "__vtable");
        layout.vtable_global = vtable_gv;
    }

    auto saved_cls  = current_class_;
    current_class_  = c.name;
    TypeId cls_type = types_.from_name(c.name);

    // Generate all methods
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            std::string mangled = mangle(c.name, f->name);
            gen_func(*f, mangled, cls_type);
        }
    }

    current_class_ = saved_cls;
}

void Codegen::emit_main_wrapper() {
    // Find the user's main: first try plain @main, then namespace-qualified *__main
    Function* user_main = mod_->getFunction("main");
    if (!user_main) {
        for (auto& f : *mod_) {
            if (f.getName().ends_with("__main") && f.arg_size() == 0) {
                user_main = &f;
                break;
            }
        }
    }
    if (!user_main) return;

    bool returns_void = user_main->getReturnType()->isVoidTy();
    if (!returns_void) return; // already returns int, nothing to do

    // Rename void @<whatever_main> → @dux_main
    user_main->setName("dux_main");

    // Create i32 @main() { call void @dux_main(); ret i32 0 }
    auto* ft  = llvm::FunctionType::get(llvm::Type::getInt32Ty(*ctx_), false);
    auto* fn  = Function::Create(ft, Function::ExternalLinkage, "main", *mod_);
    auto* bb  = BasicBlock::Create(*ctx_, "entry", fn);
    builder_->SetInsertPoint(bb);
    builder_->CreateCall(user_main, {});
    builder_->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_), 0));
}

void Codegen::gen_namespace(const ast::NamespaceDecl& ns) {
    build_layouts(ns.decls);
    declare_functions(ns.decls, ns.name);
    for (const auto& d : ns.decls) gen_decl(*d, ns.name);
    gen_stmts(ns.stmts);
}

// ─── Optimization pipeline (#37) ─────────────────────────────────────────────

void Codegen::optimize() {
    llvm::PassBuilder pb;
    llvm::LoopAnalysisManager     lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager    cgam;
    llvm::ModuleAnalysisManager   mam;

    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::OptimizationLevel lvl = llvm::OptimizationLevel::O0;
    switch (opt_level_) {
        case 1: lvl = llvm::OptimizationLevel::O1; break;
        case 2: lvl = llvm::OptimizationLevel::O2; break;
        case 3: lvl = llvm::OptimizationLevel::O3; break;
        default: break;
    }

    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(lvl);
    mpm.run(*mod_, mam);
}

// ─── DWARF debug info (#36) ───────────────────────────────────────────────────

void Codegen::debug_init(const std::string& source_path) {
    dibuilder_ = std::make_unique<llvm::DIBuilder>(*mod_);

    std::string dir  = ".";
    std::string file = source_path;
    if (!source_path.empty()) {
        std::filesystem::path p(source_path);
        dir  = p.parent_path().empty() ? "." : p.parent_path().string();
        file = p.filename().string();
    }

    di_file_ = dibuilder_->createFile(file, dir);
    di_cu_   = dibuilder_->createCompileUnit(
        llvm::dwarf::DW_LANG_C,   // closest to Dux semantics
        di_file_,
        "dux " DUX_VERSION,
        /*isOptimized=*/opt_level_ > 0,
        /*Flags=*/"",
        /*RuntimeVersion=*/0);
}

llvm::DISubprogram* Codegen::debug_func(const ast::FunctionDecl& f,
                                         llvm::Function* fn,
                                         const std::string& mangled) {
    if (!emit_debug_ || !dibuilder_ || !di_file_) return nullptr;

    // Build a simple subroutine type (no parameter types for now)
    auto* sub_type = dibuilder_->createSubroutineType(
        dibuilder_->getOrCreateTypeArray({}));

    unsigned line = f.loc.line > 0 ? (unsigned)f.loc.line : 1;
    auto* sp = dibuilder_->createFunction(
        di_cu_, f.name, mangled, di_file_,
        line, sub_type,
        line,
        llvm::DINode::FlagPrototyped,
        llvm::DISubprogram::SPFlagDefinition);

    fn->setSubprogram(sp);
    return sp;
}

void Codegen::debug_set_loc(const ast::SourceLoc& loc) {
    if (!emit_debug_ || !builder_->GetInsertBlock()) return;
    auto* sp = builder_->GetInsertBlock()->getParent()
                   ? builder_->GetInsertBlock()->getParent()->getSubprogram()
                   : nullptr;
    if (!sp) return;
    unsigned line = loc.line > 0 ? (unsigned)loc.line : 1;
    unsigned col  = loc.col  > 0 ? (unsigned)loc.col  : 0;
    builder_->SetCurrentDebugLocation(
        llvm::DILocation::get(*ctx_, line, col, sp));
}

// ─── Module / import system (#35) ────────────────────────────────────────────

// Stdlib dispatch table: module → { fn_name → (rt_sym, ret_tid, {param_tids}) }
struct StdlibFn {
    std::string rt_sym;   // duxrt_* symbol name
    TypeId      ret;
    std::vector<TypeId> params;
};

using StdlibMod = std::unordered_map<std::string, StdlibFn>;

static const std::unordered_map<std::string, StdlibMod>& stdlib_table() {
    static const std::unordered_map<std::string, StdlibMod> tbl = {
        {"math", {
            {"sqrt",  {"duxrt_math_sqrt",   TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"pow",   {"duxrt_math_pow",    TR::TID_DOUBLE, {TR::TID_DOUBLE, TR::TID_DOUBLE}}},
            {"floor", {"duxrt_math_floor",  TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"ceil",  {"duxrt_math_ceil",   TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"abs",   {"duxrt_math_abs_d",  TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"log",   {"duxrt_math_log",    TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"log2",  {"duxrt_math_log2",   TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"sin",   {"duxrt_math_sin",    TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"cos",   {"duxrt_math_cos",    TR::TID_DOUBLE, {TR::TID_DOUBLE}}},
            {"min",   {"duxrt_math_min_d",  TR::TID_DOUBLE, {TR::TID_DOUBLE, TR::TID_DOUBLE}}},
            {"max",   {"duxrt_math_max_d",  TR::TID_DOUBLE, {TR::TID_DOUBLE, TR::TID_DOUBLE}}},
        }},
        {"str", {
            {"concat",      {"duxrt_str_concat",      TR::TID_STR, {TR::TID_STR, TR::TID_STR}}},
            {"from_int",    {"duxrt_str_from_int",    TR::TID_STR, {TR::TID_LONG}}},
            {"from_double", {"duxrt_str_from_double", TR::TID_STR, {TR::TID_DOUBLE}}},
            {"length",      {"duxrt_str_length",      TR::TID_LONG, {TR::TID_STR}}},
            {"slice",       {"duxrt_str_slice",       TR::TID_STR, {TR::TID_STR, TR::TID_LONG, TR::TID_LONG}}},
        }},
        {"io", {
            {"println",  {"duxrt_println_str",  TR::TID_VOID, {TR::TID_STR}}},
            {"print",    {"duxrt_print_str",    TR::TID_VOID, {TR::TID_STR}}},
            {"readline", {"duxrt_readline",     TR::TID_STR,  {}}},
        }},
    };
    return tbl;
}

void Codegen::process_import(const ast::ImportDecl& imp) {
    if (imp.path.empty()) return;
    // path is a dotted string like "math" or "dux.math"; use the last component
    const std::string& full = imp.path;
    std::string mod = full.substr(full.rfind('.') == std::string::npos ? 0 : full.rfind('.') + 1);

    if (stdlib_table().count(mod)) {
        stdlib_imports_.insert(mod);
        // Pre-declare all stdlib functions so they appear in IR
        const auto& fns = stdlib_table().at(mod);
        for (const auto& [fn_name, sf] : fns) {
            std::vector<llvm::Type*> ptypes;
            for (auto tid : sf.params) ptypes.push_back(lower_type(tid));
            get_or_declare_rt(sf.rt_sym, lower_type(sf.ret), ptypes);
        }
    }
    // File-based imports would be handled here in a future pass
}

Value* Codegen::try_stdlib_call(const ast::CallExpr& e) {
    auto* mem = dynamic_cast<const ast::MemberExpr*>(e.callee.get());
    if (!mem) return nullptr;
    auto* id = dynamic_cast<const ast::IdentExpr*>(mem->object.get());
    if (!id) return nullptr;

    const std::string& mod = id->name;
    if (!stdlib_imports_.count(mod)) return nullptr;

    auto it_mod = stdlib_table().find(mod);
    if (it_mod == stdlib_table().end()) return nullptr;

    auto it_fn = it_mod->second.find(mem->member);
    if (it_fn == it_mod->second.end()) return nullptr;

    const StdlibFn& sf = it_fn->second;
    Function* fn = mod_->getFunction(sf.rt_sym);
    if (!fn) return nullptr;

    std::vector<Value*> args;
    auto param_it = fn->arg_begin();
    for (std::size_t i = 0; i < e.args.size() && param_it != fn->arg_end(); ++i, ++param_it) {
        Value* v = gen_expr(*e.args[i]);
        // Coerce to expected type
        llvm::Type* expected = param_it->getType();
        if (v->getType() != expected) {
            if (expected->isDoubleTy() && v->getType()->isIntegerTy())
                v = builder_->CreateSIToFP(v, expected);
            else if (expected->isIntegerTy() && v->getType()->isDoubleTy())
                v = builder_->CreateFPToSI(v, expected);
        }
        args.push_back(v);
    }
    return builder_->CreateCall(fn, args);
}

// ─── Statements ──────────────────────────────────────────────────────────────

void Codegen::gen_stmts(const ast::StmtList& stmts) {
    for (const auto& sp : stmts) {
        if (!builder_->GetInsertBlock()->getTerminator())
            gen_stmt(*sp);
    }
}

void Codegen::gen_stmt(const ast::Stmt& s) {
    debug_set_loc(s.loc);
    if (auto* b  = dynamic_cast<const ast::BlockStmt*>(&s))    { gen_block(*b);    return; }
    if (auto* e  = dynamic_cast<const ast::ExprStmt*>(&s))     { gen_expr(*e->expr); return; }
    if (auto* v  = dynamic_cast<const ast::VarDeclStmt*>(&s))  { gen_var_decl(*v); return; }
    if (auto* i  = dynamic_cast<const ast::IfStmt*>(&s))       { gen_if(*i);       return; }
    if (auto* w  = dynamic_cast<const ast::WhileStmt*>(&s))    { gen_while(*w);    return; }
    if (auto* dw = dynamic_cast<const ast::DoWhileStmt*>(&s))  { gen_do_while(*dw);return; }
    if (auto* fi = dynamic_cast<const ast::ForInStmt*>(&s))    { gen_for_in(*fi);  return; }
    if (auto* fc = dynamic_cast<const ast::ForCStmt*>(&s))     { gen_for_c(*fc);   return; }
    if (auto* sw = dynamic_cast<const ast::SwitchStmt*>(&s))   { gen_switch(*sw);  return; }
    if (auto* tc = dynamic_cast<const ast::TryCatchStmt*>(&s)) { gen_try_catch(*tc);return;}
    if (auto* r  = dynamic_cast<const ast::ReturnStmt*>(&s))   { gen_return(*r);   return; }
    if (auto* br = dynamic_cast<const ast::BreakStmt*>(&s)) {
        if (loop_stack_.empty()) return;
        if (br->label) {
            for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it)
                if (it->label == *br->label) { builder_->CreateBr(it->exit); return; }
        }
        builder_->CreateBr(loop_stack_.back().exit);
        return;
    }
    if (auto* co = dynamic_cast<const ast::ContinueStmt*>(&s)) {
        if (loop_stack_.empty()) return;
        if (co->label) {
            for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it)
                if (it->label == *co->label) { builder_->CreateBr(it->header); return; }
        }
        builder_->CreateBr(loop_stack_.back().header);
        return;
    }
    if (auto* a = dynamic_cast<const ast::AssertStmt*>(&s))    { gen_assert(*a);   return; }
    if (auto* d = dynamic_cast<const ast::DeleteStmt*>(&s))    { gen_delete(*d);   return; }
    if (auto* ls = dynamic_cast<const ast::LabeledStmt*>(&s))  {
        // Propagate label into the inner loop/while statement
        pending_label_ = ls->label;
        gen_stmt(*ls->stmt);
        pending_label_.clear();
        return;
    }
}

void Codegen::gen_block(const ast::BlockStmt& b) {
    env_push();
    gen_stmts(b.body);
    env_pop();
}

void Codegen::gen_if(const ast::IfStmt& s) {
    Value* cond_val = gen_expr(*s.cond);
    cond_val = to_bool(cond_val, type_id_of(*s.cond));

    Function* fn   = builder_->GetInsertBlock()->getParent();
    auto* then_bb  = BasicBlock::Create(*ctx_, "if.then", fn);
    auto* else_bb  = BasicBlock::Create(*ctx_, "if.else", fn);
    auto* merge_bb = BasicBlock::Create(*ctx_, "if.end",  fn);

    builder_->CreateCondBr(cond_val, then_bb, else_bb);

    builder_->SetInsertPoint(then_bb);
    gen_stmt(*s.then_br);
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(merge_bb);

    builder_->SetInsertPoint(else_bb);
    if (s.else_br) gen_stmt(*s.else_br);
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(merge_bb);

    builder_->SetInsertPoint(merge_bb);
}

void Codegen::gen_while(const ast::WhileStmt& s) {
    Function* fn   = builder_->GetInsertBlock()->getParent();
    auto* hdr_bb   = BasicBlock::Create(*ctx_, "while.cond", fn);
    auto* body_bb  = BasicBlock::Create(*ctx_, "while.body", fn);
    auto* exit_bb  = BasicBlock::Create(*ctx_, "while.end",  fn);

    builder_->CreateBr(hdr_bb);
    builder_->SetInsertPoint(hdr_bb);
    Value* cond = gen_expr(*s.cond);
    cond = to_bool(cond, type_id_of(*s.cond));
    builder_->CreateCondBr(cond, body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);
    std::string lbl = s.label ? *s.label : pending_label_;
    pending_label_.clear();
    loop_stack_.push_back({hdr_bb, exit_bb, lbl});
    gen_stmt(*s.body);
    loop_stack_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(hdr_bb);

    builder_->SetInsertPoint(exit_bb);
}

void Codegen::gen_do_while(const ast::DoWhileStmt& s) {
    Function* fn  = builder_->GetInsertBlock()->getParent();
    auto* body_bb = BasicBlock::Create(*ctx_, "do.body", fn);
    auto* cond_bb = BasicBlock::Create(*ctx_, "do.cond", fn);
    auto* exit_bb = BasicBlock::Create(*ctx_, "do.end",  fn);

    builder_->CreateBr(body_bb);
    builder_->SetInsertPoint(body_bb);
    loop_stack_.push_back({cond_bb, exit_bb, pending_label_});
    pending_label_.clear();
    gen_stmt(*s.body);
    loop_stack_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(cond_bb);

    builder_->SetInsertPoint(cond_bb);
    Value* cond = gen_expr(*s.cond);
    cond = to_bool(cond, type_id_of(*s.cond));
    builder_->CreateCondBr(cond, body_bb, exit_bb);

    builder_->SetInsertPoint(exit_bb);
}

void Codegen::gen_for_in(const ast::ForInStmt& s) {
    // Lower range expression: for int i in lo..<hi  →  int i = lo; while i < hi { ... i++ }
    // We detect BinaryExpr with "..<" or "..=" operators.
    Function* fn = builder_->GetInsertBlock()->getParent();

    auto* hdr_bb  = BasicBlock::Create(*ctx_, "for.cond",  fn);
    auto* body_bb = BasicBlock::Create(*ctx_, "for.body",  fn);
    auto* incr_bb = BasicBlock::Create(*ctx_, "for.incr",  fn);
    auto* exit_bb = BasicBlock::Create(*ctx_, "for.end",   fn);

    env_push();
    TypeId var_tid = types_.from_type_expr(s.var_type);
    if (var_tid == TR::TID_UNKNOWN) var_tid = TR::TID_INT;
    llvm::Type* var_t = lower_type(var_tid);

    // Allocate loop variable
    Value* var_alloca = make_alloca(var_t, s.var_name);
    env_define(s.var_name, var_alloca);

    bool is_range = false;
    bool inclusive = false;
    Value* lo_val = nullptr;
    Value* hi_val = nullptr;

    if (auto* bin = dynamic_cast<const ast::BinaryExpr*>(s.iterable.get())) {
        if (bin->op == "..<" || bin->op == "..=") {
            is_range  = true;
            inclusive = (bin->op == "..=");
            lo_val = gen_expr(*bin->left);
            hi_val = gen_expr(*bin->right);
        }
    }
    // range(n) → 0..<n
    if (!is_range) {
        if (auto* call = dynamic_cast<const ast::CallExpr*>(s.iterable.get())) {
            if (auto* id = dynamic_cast<const ast::IdentExpr*>(call->callee.get())) {
                if (id->name == "range" && call->args.size() == 1) {
                    is_range = true;
                    inclusive = false;
                    lo_val = llvm::ConstantInt::get(var_t, 0);
                    hi_val = gen_expr(*call->args[0]);
                }
            }
        }
    }

    if (is_range) {
        // i = lo
        if (lo_val->getType() != var_t)
            lo_val = builder_->CreateIntCast(lo_val, var_t, true);
        builder_->CreateStore(lo_val, var_alloca);
        builder_->CreateBr(hdr_bb);

        // cond: i < hi (exclusive) or i <= hi (inclusive)
        builder_->SetInsertPoint(hdr_bb);
        Value* i = builder_->CreateLoad(var_t, var_alloca, "i");
        if (hi_val->getType() != var_t)
            hi_val = builder_->CreateIntCast(hi_val, var_t, true);
        Value* cond = inclusive
            ? builder_->CreateICmpSLE(i, hi_val, "for.cond")
            : builder_->CreateICmpSLT(i, hi_val, "for.cond");
        builder_->CreateCondBr(cond, body_bb, exit_bb);
    } else {
        // Non-range iterable: skip body (not yet supported)
        builder_->CreateBr(exit_bb);
        builder_->SetInsertPoint(hdr_bb);
        builder_->CreateBr(exit_bb);
    }

    builder_->SetInsertPoint(body_bb);
    loop_stack_.push_back({incr_bb, exit_bb, pending_label_});
    pending_label_.clear();
    gen_stmt(*s.body);
    loop_stack_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(incr_bb);

    // Increment
    builder_->SetInsertPoint(incr_bb);
    if (is_range) {
        Value* i = builder_->CreateLoad(var_t, var_alloca);
        Value* one = llvm::ConstantInt::get(var_t, 1);
        builder_->CreateStore(builder_->CreateAdd(i, one), var_alloca);
    }
    builder_->CreateBr(hdr_bb);

    builder_->SetInsertPoint(exit_bb);
    env_pop();
}

void Codegen::gen_for_c(const ast::ForCStmt& s) {
    Function* fn = builder_->GetInsertBlock()->getParent();
    auto* hdr_bb  = BasicBlock::Create(*ctx_, "forc.cond", fn);
    auto* body_bb = BasicBlock::Create(*ctx_, "forc.body", fn);
    auto* incr_bb = BasicBlock::Create(*ctx_, "forc.incr", fn);
    auto* exit_bb = BasicBlock::Create(*ctx_, "forc.end",  fn);

    env_push();
    TypeId var_tid = types_.from_type_expr(s.var_type);
    if (var_tid == TR::TID_UNKNOWN) var_tid = TR::TID_INT;
    llvm::Type* var_t = lower_type(var_tid);

    Value* var_alloca = make_alloca(var_t, s.var_name);
    env_define(s.var_name, var_alloca);

    Value* init = gen_expr(*s.init);
    if (init && init->getType() != var_t)
        init = builder_->CreateIntCast(init, var_t, true);
    builder_->CreateStore(init, var_alloca);
    builder_->CreateBr(hdr_bb);

    builder_->SetInsertPoint(hdr_bb);
    Value* cond = gen_expr(*s.cond);
    cond = to_bool(cond, type_id_of(*s.cond));
    builder_->CreateCondBr(cond, body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);
    loop_stack_.push_back({incr_bb, exit_bb, pending_label_});
    pending_label_.clear();
    gen_stmt(*s.body);
    loop_stack_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(incr_bb);

    builder_->SetInsertPoint(incr_bb);
    gen_expr(*s.incr);
    builder_->CreateBr(hdr_bb);

    builder_->SetInsertPoint(exit_bb);
    env_pop();
}

void Codegen::gen_switch(const ast::SwitchStmt& s) {
    Function* fn  = builder_->GetInsertBlock()->getParent();
    Value* sw_val = gen_expr(*s.expr);
    auto* exit_bb = BasicBlock::Create(*ctx_, "sw.end", fn);

    // Ensure integer type for switch
    if (!sw_val->getType()->isIntegerTy())
        sw_val = builder_->CreateFPToSI(sw_val, llvm::Type::getInt64Ty(*ctx_));

    auto* sw_inst = builder_->CreateSwitch(sw_val, exit_bb,
                                           static_cast<unsigned>(s.cases.size()));

    loop_stack_.push_back({nullptr, exit_bb, {}}); // break goes to exit

    for (const auto& c : s.cases) {
        BasicBlock* case_bb;
        if (c.value) {
            case_bb = BasicBlock::Create(*ctx_, "sw.case", fn);
            Value* case_val = gen_expr(**c.value);
            if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(case_val))
                sw_inst->addCase(ci, case_bb);
        } else {
            case_bb = BasicBlock::Create(*ctx_, "sw.default", fn);
            sw_inst->setDefaultDest(case_bb);
        }

        builder_->SetInsertPoint(case_bb);
        env_push();
        gen_stmts(c.body);
        env_pop();
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(exit_bb);
    }

    loop_stack_.pop_back();
    builder_->SetInsertPoint(exit_bb);
}

void Codegen::gen_try_catch(const ast::TryCatchStmt& s) {
    // Simplified: generate try body in current block; catch body in a separate block.
    // Full EH (invoke/landingpad) requires personality function — deferred to when
    // duxrt exception ABI is ready (#32). For now: emit both blocks sequentially.
    Function* fn   = builder_->GetInsertBlock()->getParent();
    auto* try_bb   = BasicBlock::Create(*ctx_, "try.body",  fn);
    auto* catch_bb = BasicBlock::Create(*ctx_, "try.catch", fn);
    auto* end_bb   = BasicBlock::Create(*ctx_, "try.end",   fn);

    builder_->CreateBr(try_bb);

    builder_->SetInsertPoint(try_bb);
    gen_stmt(*s.try_body);
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(end_bb); // on success, skip catch

    builder_->SetInsertPoint(catch_bb);
    env_push();
    if (s.catch_var && !s.catch_all) {
        // Allocate catch variable as null ptr placeholder
        Value* ex_alloca = make_alloca(ptr_type(), *s.catch_var);
        builder_->CreateStore(llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptr_type())), ex_alloca);
        env_define(*s.catch_var, ex_alloca);
    }
    gen_stmt(*s.catch_body);
    env_pop();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(end_bb);

    builder_->SetInsertPoint(end_bb);
}

void Codegen::gen_return(const ast::ReturnStmt& s) {
    if (s.value) {
        Value* val = gen_expr(**s.value);
        TypeId val_tid = type_id_of(**s.value);
        val = coerce(val, val_tid, current_ret_type_);
        builder_->CreateRet(val);
    } else {
        builder_->CreateRetVoid();
    }
}

void Codegen::gen_var_decl(const ast::VarDeclStmt& s) {
    TypeId decl_tid = types_.from_type_expr(s.type);

    for (const auto& [name, init_ptr] : s.decls) {
        TypeId var_tid = decl_tid;
        Value* init_val = nullptr;

        if (init_ptr) {
            init_val = gen_expr(*init_ptr);
            TypeId init_tid = type_id_of(*init_ptr);
            if (var_tid == TR::TID_UNKNOWN) var_tid = init_tid;
            init_val = coerce(init_val, init_tid, var_tid);
        }
        if (var_tid == TR::TID_UNKNOWN) var_tid = TR::TID_INT;

        llvm::Type* t = lower_type(var_tid);
        Value* alloca = make_alloca(t, name);
        if (init_val) {
            if (init_val->getType() != t)
                init_val = coerce(init_val, decl_tid, var_tid);
            builder_->CreateStore(init_val, alloca);
        } else {
            builder_->CreateStore(llvm::Constant::getNullValue(t), alloca);
        }
        env_define(name, alloca);
        // Track variable→class for member access resolution
        if (!s.type.name.empty() && layouts_.count(s.type.name))
            var_class_[name] = s.type.name;
    }
}

void Codegen::gen_assert(const ast::AssertStmt& s) {
    // In debug: if cond is false, call abort().
    // In NDEBUG: fold away.
    Function* fn   = builder_->GetInsertBlock()->getParent();
    Value* cond    = gen_expr(*s.cond);
    cond = to_bool(cond, type_id_of(*s.cond));

    auto* ok_bb   = BasicBlock::Create(*ctx_, "assert.ok",   fn);
    auto* fail_bb = BasicBlock::Create(*ctx_, "assert.fail", fn);
    builder_->CreateCondBr(cond, ok_bb, fail_bb);

    builder_->SetInsertPoint(fail_bb);
    // Call abort() on failure
    auto* abort_fn = get_or_declare_rt("abort",
        llvm::Type::getVoidTy(*ctx_), {});
    builder_->CreateCall(abort_fn, {});
    builder_->CreateUnreachable();

    builder_->SetInsertPoint(ok_bb);
}

void Codegen::gen_delete(const ast::DeleteStmt& s) {
    Value* ptr = gen_expr(*s.expr);
    // Call duxrt_free (or just free)
    auto* free_fn = get_or_declare_rt("free",
        llvm::Type::getVoidTy(*ctx_), {ptr_type()});
    builder_->CreateCall(free_fn, {ptr});
}

// ─── Expressions ─────────────────────────────────────────────────────────────

Value* Codegen::gen_expr(const ast::Expr& e) {
    if (auto* p = dynamic_cast<const ast::IntLitExpr*>(&e))
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_),
                                      static_cast<int32_t>(p->value), true);

    if (auto* p = dynamic_cast<const ast::FloatLitExpr*>(&e))
        return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*ctx_), p->value);

    if (auto* p = dynamic_cast<const ast::StringLitExpr*>(&e)) {
        // Emit a global constant string and return a ptr to it
        return builder_->CreateGlobalStringPtr(p->value, ".str");
    }

    if (auto* p = dynamic_cast<const ast::BoolLitExpr*>(&e))
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*ctx_),
                                      p->value ? 1 : 0);

    if (dynamic_cast<const ast::NullLitExpr*>(&e))
        return llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptr_type()));

    if (auto* p = dynamic_cast<const ast::IdentExpr*>(&e))
        return load_var(p->name, p->loc);

    if (dynamic_cast<const ast::ThisExpr*>(&e))
        return load_var("this", e.loc);

    if (dynamic_cast<const ast::SuperExpr*>(&e))
        return load_var("this", e.loc); // super same ptr, different dispatch

    if (auto* p = dynamic_cast<const ast::BinaryExpr*>(&e))  return gen_binary(*p);
    if (auto* p = dynamic_cast<const ast::UnaryExpr*>(&e))   return gen_unary(*p);
    if (auto* p = dynamic_cast<const ast::AssignExpr*>(&e))  return gen_assign(*p);
    if (auto* p = dynamic_cast<const ast::CallExpr*>(&e))    return gen_call(*p);
    if (auto* p = dynamic_cast<const ast::MemberExpr*>(&e))  return gen_member(*p);
    if (auto* p = dynamic_cast<const ast::IndexExpr*>(&e))   return gen_index(*p);
    if (auto* p = dynamic_cast<const ast::NewExpr*>(&e))     return gen_new(*p);
    if (auto* p = dynamic_cast<const ast::ListExpr*>(&e))    return gen_list(*p);
    if (auto* p = dynamic_cast<const ast::DictExpr*>(&e))    return gen_dict(*p);

    return llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_type()));
}

Value* Codegen::gen_assign(const ast::AssignExpr& e) {
    // Get lvalue slot
    Value* slot = lvalue_of(*e.target);
    Value* rhs  = gen_expr(*e.value);
    TypeId rhs_tid = type_id_of(*e.value);

    if (!slot) {
        // Implicit var: create alloca in current scope
        llvm::Type* implicit_t = rhs ? rhs->getType() : ptr_type();
        auto* alloca = make_alloca(implicit_t, "");
        if (auto* id = dynamic_cast<const ast::IdentExpr*>(e.target.get()))
            env_define(id->name, alloca);
        slot = alloca;
    }

    if (rhs) {
        if (e.op == "=") {
            builder_->CreateStore(rhs, slot);
        } else {
            // Compound assignment
            TypeId lhs_tid = type_id_of(*e.target);
            llvm::Type* load_t = rhs->getType(); // use rhs type as guide
            // Try to infer from lhs
            if (lhs_tid != TR::TID_UNKNOWN) load_t = lower_type(lhs_tid);
            Value* cur = builder_->CreateLoad(load_t, slot);
            // Promote integer types to match the lhs
            if (cur->getType()->isIntegerTy() && rhs->getType()->isIntegerTy() &&
                cur->getType() != rhs->getType())
                rhs = builder_->CreateIntCast(rhs, cur->getType(), true);
            Value* result = nullptr;
            bool is_fp = load_t->isFloatingPointTy();
            if      (e.op == "+=") result = is_fp ? builder_->CreateFAdd(cur, rhs)
                                                   : builder_->CreateAdd(cur, rhs);
            else if (e.op == "-=") result = is_fp ? builder_->CreateFSub(cur, rhs)
                                                   : builder_->CreateSub(cur, rhs);
            else if (e.op == "*=") result = is_fp ? builder_->CreateFMul(cur, rhs)
                                                   : builder_->CreateMul(cur, rhs);
            else if (e.op == "/=") result = is_fp ? builder_->CreateFDiv(cur, rhs)
                                                   : builder_->CreateSDiv(cur, rhs);
            else if (e.op == "%=") result = builder_->CreateSRem(cur, rhs);
            else                   result = rhs;
            builder_->CreateStore(result, slot);
            return result;
        }
    }
    (void)rhs_tid;
    return rhs;
}

Value* Codegen::gen_binary(const ast::BinaryExpr& e) {
    const std::string& op = e.op;

    // Logical short-circuit for && and ||
    if (op == "&&" || op == "||") {
        Function* fn  = builder_->GetInsertBlock()->getParent();
        auto* rhs_bb  = BasicBlock::Create(*ctx_, "logic.rhs",  fn);
        auto* end_bb  = BasicBlock::Create(*ctx_, "logic.end",  fn);

        Value* lhs = gen_expr(*e.left);
        lhs = to_bool(lhs, type_id_of(*e.left));
        BasicBlock* lhs_bb = builder_->GetInsertBlock();

        if (op == "&&") builder_->CreateCondBr(lhs, rhs_bb, end_bb);
        else            builder_->CreateCondBr(lhs, end_bb, rhs_bb);

        builder_->SetInsertPoint(rhs_bb);
        Value* rhs = gen_expr(*e.right);
        rhs = to_bool(rhs, type_id_of(*e.right));
        BasicBlock* rhs_end_bb = builder_->GetInsertBlock();
        builder_->CreateBr(end_bb);

        builder_->SetInsertPoint(end_bb);
        auto* phi = builder_->CreatePHI(llvm::Type::getInt1Ty(*ctx_), 2);
        phi->addIncoming(op == "&&" ? llvm::ConstantInt::getFalse(*ctx_) : lhs,
                         lhs_bb);
        phi->addIncoming(rhs, rhs_end_bb);
        return phi;
    }

    Value* L = gen_expr(*e.left);
    Value* R = gen_expr(*e.right);
    TypeId lt = type_id_of(*e.left);
    TypeId rt = type_id_of(*e.right);

    // Promote types
    bool is_fp = (lt == TR::TID_DOUBLE || lt == TR::TID_REAL ||
                  rt == TR::TID_DOUBLE || rt == TR::TID_REAL);
    if (is_fp) {
        if (L->getType()->isIntegerTy())
            L = builder_->CreateSIToFP(L, llvm::Type::getDoubleTy(*ctx_));
        if (R->getType()->isIntegerTy())
            R = builder_->CreateSIToFP(R, llvm::Type::getDoubleTy(*ctx_));
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy() &&
               L->getType() != R->getType()) {
        // Integer promotion: widen the narrower operand
        unsigned lw = L->getType()->getIntegerBitWidth();
        unsigned rw = R->getType()->getIntegerBitWidth();
        if (lw < rw) L = builder_->CreateSExt(L, R->getType());
        else         R = builder_->CreateSExt(R, L->getType());
    }

    // String + string via rt call
    if (op == "+" && (lt == TR::TID_STR || rt == TR::TID_STR)) {
        auto* strcat_fn = get_or_declare_rt("duxrt_str_concat",
            ptr_type(), {ptr_type(), ptr_type()});
        return builder_->CreateCall(strcat_fn, {L, R});
    }

    // Arithmetic
    if (op == "+")  return is_fp ? builder_->CreateFAdd(L, R) : builder_->CreateAdd(L, R);
    if (op == "-")  return is_fp ? builder_->CreateFSub(L, R) : builder_->CreateSub(L, R);
    if (op == "*")  return is_fp ? builder_->CreateFMul(L, R) : builder_->CreateMul(L, R);
    if (op == "/")  return is_fp ? builder_->CreateFDiv(L, R) : builder_->CreateSDiv(L, R);
    if (op == "%")  return is_fp ? builder_->CreateFRem(L, R) : builder_->CreateSRem(L, R);

    // Comparisons
    if (is_fp) {
        if (op == "==") return builder_->CreateFCmpOEQ(L, R);
        if (op == "!=") return builder_->CreateFCmpONE(L, R);
        if (op == "<")  return builder_->CreateFCmpOLT(L, R);
        if (op == ">")  return builder_->CreateFCmpOGT(L, R);
        if (op == "<=") return builder_->CreateFCmpOLE(L, R);
        if (op == ">=") return builder_->CreateFCmpOGE(L, R);
    } else {
        if (op == "==") return builder_->CreateICmpEQ(L, R);
        if (op == "!=") return builder_->CreateICmpNE(L, R);
        if (op == "<")  return builder_->CreateICmpSLT(L, R);
        if (op == ">")  return builder_->CreateICmpSGT(L, R);
        if (op == "<=") return builder_->CreateICmpSLE(L, R);
        if (op == ">=") return builder_->CreateICmpSGE(L, R);
    }

    // Range operators: return a struct {lo, hi} — simplified as lo value for now
    if (op == "..<" || op == "..=") return L; // iterator protocol in M4

    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_), 0);
}

Value* Codegen::gen_unary(const ast::UnaryExpr& e) {
    const std::string& op = e.op;

    if (op == "++" || op == "--") {
        Value* slot = lvalue_of(*e.operand);
        TypeId tid  = type_id_of(*e.operand);
        llvm::Type* t = lower_type(tid == TR::TID_UNKNOWN ? TR::TID_INT : tid);
        Value* cur  = builder_->CreateLoad(t, slot);
        Value* one  = t->isFloatingPointTy()
            ? (Value*)llvm::ConstantFP::get(t, 1.0)
            : (Value*)llvm::ConstantInt::get(t, 1);
        Value* next = (op == "++")
            ? (t->isFloatingPointTy() ? builder_->CreateFAdd(cur, one)
                                      : builder_->CreateAdd(cur, one))
            : (t->isFloatingPointTy() ? builder_->CreateFSub(cur, one)
                                      : builder_->CreateSub(cur, one));
        builder_->CreateStore(next, slot);
        return e.prefix ? next : cur;
    }

    Value* v = gen_expr(*e.operand);
    TypeId tid = type_id_of(*e.operand);

    if (op == "-") {
        if (v->getType()->isFloatingPointTy()) return builder_->CreateFNeg(v);
        return builder_->CreateNeg(v);
    }
    if (op == "!" || op == "not") {
        Value* b = to_bool(v, tid);
        return builder_->CreateNot(b);
    }
    if (op == "~") {
        return builder_->CreateNot(v);
    }
    if (op == "+") return v;

    return v;
}

Value* Codegen::gen_call(const ast::CallExpr& e) {
    // Stdlib module call (e.g. math.sqrt, str.concat)
    if (Value* v = try_stdlib_call(e)) return v;

    // Built-in println / print
    if (auto* id = dynamic_cast<const ast::IdentExpr*>(e.callee.get())) {
        const std::string& name = id->name;

        if (name == "println" || name == "print") {
            Value* arg = e.args.empty()
                ? (Value*)builder_->CreateGlobalStringPtr("", ".empty")
                : gen_expr(*e.args[0]);
            TypeId t = e.args.empty() ? TR::TID_STR : type_id_of(*e.args[0]);
            return rt_println(arg, t);
        }

        // assert built-in — handled as stmt but might appear as expr
        if (name == "assert") {
            if (!e.args.empty()) {
                Value* cond = gen_expr(*e.args[0]);
                cond = to_bool(cond, type_id_of(*e.args[0]));
                Function* fn   = builder_->GetInsertBlock()->getParent();
                auto* ok_bb    = BasicBlock::Create(*ctx_, "assert.ok",   fn);
                auto* fail_bb  = BasicBlock::Create(*ctx_, "assert.fail", fn);
                builder_->CreateCondBr(cond, ok_bb, fail_bb);
                builder_->SetInsertPoint(fail_bb);
                auto* abort_fn = get_or_declare_rt("abort",
                    llvm::Type::getVoidTy(*ctx_), {});
                builder_->CreateCall(abort_fn, {});
                builder_->CreateUnreachable();
                builder_->SetInsertPoint(ok_bb);
            }
            return llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptr_type()));
        }

        // len() — call duxrt_list_len or strlen
        if (name == "len") {
            Value* arg = e.args.empty() ? nullptr : gen_expr(*e.args[0]);
            if (!arg) return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_), 0);
            auto* len_fn = get_or_declare_rt("duxrt_len",
                llvm::Type::getInt64Ty(*ctx_), {ptr_type()});
            return builder_->CreateCall(len_fn, {arg});
        }

        // Look up a declared function
        Function* fn = mod_->getFunction(name);
        if (!fn) {
            return llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptr_type()));
        }
        std::vector<Value*> args;
        auto param_it = fn->arg_begin();
        for (const auto& a : e.args) {
            Value* v = gen_expr(*a);
            if (param_it != fn->arg_end())
                v = coerce_to_llvm_type(v, (param_it++)->getType());
            args.push_back(v);
        }
        return builder_->CreateCall(fn, args);
    }

    // Member call: obj.method(args)
    if (auto* mem = dynamic_cast<const ast::MemberExpr*>(e.callee.get())) {
        Value* obj = gen_expr(*mem->object);
        TypeId obj_tid = type_id_of(*mem->object);
        std::string cls_name = types_.name_of(obj_tid);

        // Use full class resolution (same fallback as gen_member / lvalue_of)
        cls_name = resolve_class_name(*mem->object, obj_tid);
        std::string mangled = mangle(cls_name, mem->member);
        Function* fn = mod_->getFunction(mangled);
        if (fn) {
            std::vector<Value*> args = {obj};
            auto param_it = std::next(fn->arg_begin()); // skip 'this'
            for (const auto& a : e.args) {
                Value* v = gen_expr(*a);
                if (param_it != fn->arg_end())
                    v = coerce_to_llvm_type(v, (param_it++)->getType());
                args.push_back(v);
            }
            return builder_->CreateCall(fn, args);
        }
        // Fallback: return null
        return llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptr_type()));
    }

    // Other callee
    gen_expr(*e.callee);
    return llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_type()));
}

Value* Codegen::gen_member(const ast::MemberExpr& e) {
    Value* obj = gen_expr(*e.object);
    std::string cls_name = resolve_class_name(*e.object, type_id_of(*e.object));

    auto layout_it = layouts_.find(cls_name);
    if (layout_it != layouts_.end()) {
        const ClassLayout& layout = layout_it->second;
        for (const auto& f : layout.fields) {
            if (f.name == e.member) {
                Value* gep = builder_->CreateStructGEP(
                    layout.struct_type, obj, f.index, e.member);
                return builder_->CreateLoad(lower_type(f.type), gep, e.member);
            }
        }
        for (const auto& mi : layout.methods) {
            if (mi.name == e.member && mi.fn)
                return mi.fn;
        }
    }
    // Unknown member — return null
    return llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_type()));
}

Value* Codegen::gen_index(const ast::IndexExpr& e) {
    Value* obj = gen_expr(*e.object);
    Value* idx = gen_expr(*e.index);
    TypeId obj_tid = type_id_of(*e.object);

    if (obj_tid == TR::TID_LIST) {
        auto* get_fn = get_or_declare_rt("duxrt_list_get",
            ptr_type(), {ptr_type(), llvm::Type::getInt64Ty(*ctx_)});
        Value* idx64 = builder_->CreateSExt(idx, llvm::Type::getInt64Ty(*ctx_));
        return builder_->CreateCall(get_fn, {obj, idx64});
    }
    if (obj_tid == TR::TID_DICT) {
        auto* get_fn = get_or_declare_rt("duxrt_dict_get",
            ptr_type(), {ptr_type(), ptr_type()});
        return builder_->CreateCall(get_fn, {obj, idx});
    }
    // String indexing
    if (obj_tid == TR::TID_STR) {
        Value* idx64 = builder_->CreateSExt(idx, llvm::Type::getInt64Ty(*ctx_));
        return builder_->CreateGEP(llvm::Type::getInt8Ty(*ctx_), obj,
                                   {idx64}, "str.idx");
    }
    return llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_type()));
}

Value* Codegen::gen_new(const ast::NewExpr& e) {
    std::string cls_name = e.type.name;
    if (!layouts_.count(cls_name))
        return llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptr_type()));

    ClassLayout& layout = layouts_[cls_name];
    llvm::StructType* st = layout.struct_type;

    // Allocate: malloc(sizeof(T))
    llvm::DataLayout dl(mod_.get());
    uint64_t sz = dl.getTypeAllocSize(st);
    Value* size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx_), sz);
    Value* raw = rt_malloc(size_val);

    // Store vtable pointer at index 0
    if (layout.vtable_global) {
        Value* vtable_gep = builder_->CreateStructGEP(st, raw, 0, "vtable.ptr");
        builder_->CreateStore(layout.vtable_global, vtable_gep);
    }

    // Apply field default initializers before the constructor runs
    for (const auto& fi : layout.fields) {
        if (fi.decl && fi.decl->init) {
            Value* gep = builder_->CreateStructGEP(st, raw, fi.index, fi.name + ".init");
            Value* init_val = gen_expr(*fi.decl->init.value());
            init_val = coerce_to_llvm_type(init_val, lower_type(fi.type));
            builder_->CreateStore(init_val, gep);
        }
    }

    // Call constructor if present
    std::string ctor_name = mangle(cls_name, cls_name);
    Function* ctor_fn = mod_->getFunction(ctor_name);
    if (ctor_fn) {
        std::vector<Value*> args = {raw};
        for (const auto& a : e.args) args.push_back(gen_expr(*a));
        builder_->CreateCall(ctor_fn, args);
    }

    return raw;
}

Value* Codegen::gen_list(const ast::ListExpr& e) {
    auto* new_fn = get_or_declare_rt("duxrt_list_new", ptr_type(), {});
    Value* list  = builder_->CreateCall(new_fn, {});
    auto* push_fn = get_or_declare_rt("duxrt_list_push",
        llvm::Type::getVoidTy(*ctx_), {ptr_type(), ptr_type()});
    for (const auto& elem : e.elements) {
        Value* v = gen_expr(*elem);
        // box primitive to ptr (simplified: store on heap via alloca then ptr)
        if (!v->getType()->isPointerTy()) {
            Value* box = make_alloca(v->getType(), "box");
            builder_->CreateStore(v, box);
            v = box;
        }
        builder_->CreateCall(push_fn, {list, v});
    }
    return list;
}

Value* Codegen::gen_dict(const ast::DictExpr& e) {
    auto* new_fn = get_or_declare_rt("duxrt_dict_new", ptr_type(), {});
    Value* dict  = builder_->CreateCall(new_fn, {});
    auto* set_fn = get_or_declare_rt("duxrt_dict_set",
        llvm::Type::getVoidTy(*ctx_), {ptr_type(), ptr_type(), ptr_type()});
    for (const auto& [k, v] : e.pairs) {
        Value* kv = gen_expr(*k);
        Value* vv = gen_expr(*v);
        if (!kv->getType()->isPointerTy()) {
            Value* box = make_alloca(kv->getType(), "kbox");
            builder_->CreateStore(kv, box);
            kv = box;
        }
        if (!vv->getType()->isPointerTy()) {
            Value* box = make_alloca(vv->getType(), "vbox");
            builder_->CreateStore(vv, box);
            vv = box;
        }
        builder_->CreateCall(set_fn, {dict, kv, vv});
    }
    return dict;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

Value* Codegen::load_var(const std::string& name, const ast::SourceLoc& loc) {
    Value* slot = env_lookup(name);
    if (!slot) {
        // Inside a method: implicit 'this->field' access
        if (!current_class_.empty() && layouts_.count(current_class_)) {
            const ClassLayout& layout = layouts_.at(current_class_);
            for (const auto& f : layout.fields) {
                if (f.name == name) {
                    Value* this_slot = env_lookup("this");
                    if (this_slot) {
                        Value* this_ptr = builder_->CreateLoad(ptr_type(), this_slot, "this");
                        Value* gep = builder_->CreateStructGEP(
                            layout.struct_type, this_ptr, f.index, name);
                        return builder_->CreateLoad(lower_type(f.type), gep, name);
                    }
                }
            }
        }
        if (Function* fn = mod_->getFunction(name)) return fn;
        err(loc, "codegen: undefined variable '" + name + "'");
        return llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptr_type()));
    }
    // Use the recorded alloca type for a correct opaque-pointer load
    auto it = alloca_type_.find(slot);
    llvm::Type* t = (it != alloca_type_.end())
        ? it->second
        : llvm::Type::getInt64Ty(*ctx_); // fallback
    return builder_->CreateLoad(t, slot, name);
}

Value* Codegen::lvalue_of(const ast::Expr& e) {
    if (auto* id = dynamic_cast<const ast::IdentExpr*>(&e)) {
        Value* slot = env_lookup(id->name);
        if (slot) return slot;
        // Implicit this->field in a method
        if (!current_class_.empty() && layouts_.count(current_class_)) {
            const ClassLayout& layout = layouts_.at(current_class_);
            for (const auto& f : layout.fields) {
                if (f.name == id->name) {
                    Value* this_slot = env_lookup("this");
                    if (this_slot) {
                        Value* this_ptr = builder_->CreateLoad(ptr_type(), this_slot, "this");
                        return builder_->CreateStructGEP(
                            layout.struct_type, this_ptr, f.index, id->name);
                    }
                }
            }
        }
        return nullptr;
    }
    if (auto* mem = dynamic_cast<const ast::MemberExpr*>(&e)) {
        Value* obj = gen_expr(*mem->object);
        std::string cls_name = resolve_class_name(*mem->object, type_id_of(*mem->object));
        auto it = layouts_.find(cls_name);
        if (it != layouts_.end()) {
            for (const auto& f : it->second.fields)
                if (f.name == mem->member)
                    return builder_->CreateStructGEP(
                        it->second.struct_type, obj, f.index);
        }
        return nullptr;
    }
    if (auto* idx = dynamic_cast<const ast::IndexExpr*>(&e)) {
        Value* obj = gen_expr(*idx->object);
        Value* i   = gen_expr(*idx->index);
        TypeId obj_tid = type_id_of(*idx->object);
        if (obj_tid == TR::TID_STR)
            return builder_->CreateGEP(llvm::Type::getInt8Ty(*ctx_), obj,
                                       {builder_->CreateSExt(i, llvm::Type::getInt64Ty(*ctx_))});
    }
    return nullptr;
}

Value* Codegen::coerce(Value* val, TypeId from, TypeId to) {
    if (!val || from == to || to == TR::TID_UNKNOWN) return val;

    // int → double
    if (to == TR::TID_DOUBLE || to == TR::TID_REAL) {
        if (val->getType()->isIntegerTy())
            return builder_->CreateSIToFP(val, llvm::Type::getDoubleTy(*ctx_));
    }
    // double → int
    if ((to == TR::TID_INT || to == TR::TID_LONG) && val->getType()->isFloatingPointTy())
        return builder_->CreateFPToSI(val, lower_type(to));
    // int widening
    if ((to == TR::TID_LONG) && val->getType() == llvm::Type::getInt32Ty(*ctx_))
        return builder_->CreateSExt(val, llvm::Type::getInt64Ty(*ctx_));
    if ((to == TR::TID_INT) && val->getType() == llvm::Type::getInt64Ty(*ctx_))
        return builder_->CreateTrunc(val, llvm::Type::getInt32Ty(*ctx_));
    // bool → int
    if (types_.is_integral(to) && val->getType() == llvm::Type::getInt1Ty(*ctx_))
        return builder_->CreateZExt(val, lower_type(to));
    return val;
}

Value* Codegen::coerce_to_llvm_type(Value* v, llvm::Type* pt) {
    if (!pt || !v || v->getType() == pt) return v;
    if (pt->isIntegerTy() && v->getType()->isIntegerTy())
        return builder_->CreateIntCast(v, pt, true);
    if (pt->isFloatingPointTy() && v->getType()->isIntegerTy())
        return builder_->CreateSIToFP(v, pt);
    if (pt->isIntegerTy() && v->getType()->isFloatingPointTy())
        return builder_->CreateFPToSI(v, pt);
    return v;
}

std::string Codegen::resolve_class_name(const ast::Expr& obj, TypeId obj_tid) const {
    std::string cls = types_.name_of(obj_tid);
    if (!layouts_.count(cls)) {
        if (dynamic_cast<const ast::ThisExpr*>(&obj) ||
            dynamic_cast<const ast::SuperExpr*>(&obj))
            cls = current_class_;
        if (!layouts_.count(cls)) {
            if (auto* id = dynamic_cast<const ast::IdentExpr*>(&obj)) {
                auto it = var_class_.find(id->name);
                cls = (it != var_class_.end()) ? it->second : "";
            }
        }
    }
    return cls;
}

Value* Codegen::to_bool(Value* val, TypeId t) {
    if (!val) return llvm::ConstantInt::getFalse(*ctx_);
    if (val->getType() == llvm::Type::getInt1Ty(*ctx_)) return val;
    if (val->getType()->isIntegerTy())
        return builder_->CreateICmpNE(val,
            llvm::ConstantInt::get(val->getType(), 0));
    if (val->getType()->isFloatingPointTy())
        return builder_->CreateFCmpONE(val,
            llvm::ConstantFP::get(val->getType(), 0.0));
    if (val->getType()->isPointerTy())
        return builder_->CreateICmpNE(val,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(val->getType())));
    (void)t;
    return llvm::ConstantInt::getFalse(*ctx_);
}

TypeId Codegen::type_id_of(const ast::Expr& e) const {
    if (e.type_id >= 0) return e.type_id;
    return TR::TID_UNKNOWN;
}

// ─── Environment ─────────────────────────────────────────────────────────────

void Codegen::env_push() { env_.emplace_back(); }
void Codegen::env_pop()  { if (!env_.empty()) env_.pop_back(); }

void Codegen::env_define(const std::string& name, Value* alloca) {
    if (!env_.empty()) env_.back()[name] = alloca;
}

Value* Codegen::make_alloca(llvm::Type* t, const std::string& name) {
    Value* a = builder_->CreateAlloca(t, nullptr, name);
    alloca_type_[a] = t;
    return a;
}

Value* Codegen::env_lookup(const std::string& name) const {
    for (int i = static_cast<int>(env_.size()) - 1; i >= 0; --i) {
        auto it = env_[static_cast<std::size_t>(i)].find(name);
        if (it != env_[static_cast<std::size_t>(i)].end()) return it->second;
    }
    return nullptr;
}

// ─── Runtime helpers ─────────────────────────────────────────────────────────

Function* Codegen::get_or_declare_rt(const std::string& name,
                                     llvm::Type* ret,
                                     std::vector<llvm::Type*> params,
                                     bool vararg) {
    if (auto* fn = mod_->getFunction(name)) return fn;
    auto* ft = llvm::FunctionType::get(ret, params, vararg);
    return Function::Create(ft, Function::ExternalLinkage, name, *mod_);
}

Value* Codegen::rt_malloc(Value* size) {
    auto* malloc_fn = get_or_declare_rt("malloc", ptr_type(),
                                        {llvm::Type::getInt64Ty(*ctx_)});
    return builder_->CreateCall(malloc_fn, {size});
}

Value* Codegen::rt_println(Value* v, TypeId t) {
    // Infer type from LLVM value when TypeId is unknown (e.g. member call returns)
    if (t == TR::TID_UNKNOWN) {
        if (v->getType()->isIntegerTy()) t = TR::TID_INT;
        else if (v->getType()->isFloatingPointTy()) t = TR::TID_DOUBLE;
        else t = TR::TID_STR;
    }
    if (t == TR::TID_INT || t == TR::TID_LONG || t == TR::TID_BOOL) {
        auto* fn = get_or_declare_rt("duxrt_println_int",
            llvm::Type::getVoidTy(*ctx_), {llvm::Type::getInt64Ty(*ctx_)});
        Value* v64 = v->getType() == llvm::Type::getInt64Ty(*ctx_)
            ? v : builder_->CreateSExt(v, llvm::Type::getInt64Ty(*ctx_));
        return builder_->CreateCall(fn, {v64});
    }
    if (t == TR::TID_DOUBLE || t == TR::TID_REAL) {
        auto* fn = get_or_declare_rt("duxrt_println_double",
            llvm::Type::getVoidTy(*ctx_), {llvm::Type::getDoubleTy(*ctx_)});
        Value* vd = v->getType() == llvm::Type::getDoubleTy(*ctx_)
            ? v : builder_->CreateFPExt(v, llvm::Type::getDoubleTy(*ctx_));
        return builder_->CreateCall(fn, {vd});
    }
    // str / object / unknown
    auto* fn = get_or_declare_rt("duxrt_println_str",
        llvm::Type::getVoidTy(*ctx_), {ptr_type()});
    if (!v->getType()->isPointerTy()) {
        Value* box = make_alloca(v->getType(), "box");
        builder_->CreateStore(v, box);
        v = box;
    }
    return builder_->CreateCall(fn, {v});
}

} // namespace dux::codegen
