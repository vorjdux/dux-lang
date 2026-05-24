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
// NOTE: legacy::PassManager is still required for machine-code emission in LLVM 17-18.
// TargetMachine::addPassesToEmitFile has no new-PM equivalent yet in these versions.
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
// LTO: link runtime bitcode before optimisation
#include <llvm/Linker/Linker.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/MemoryBuffer.h>
#pragma GCC diagnostic pop

#ifndef DUXRT_BC_PATH
#define DUXRT_BC_PATH ""
#endif

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

    // LTO: merge runtime bitcode so the optimiser can inline across the
    // translation-unit boundary.  Must happen before optimize().
    if (opt_level_ > 0) {
        if (!lto_merge_runtime()) return false;
        optimize();
    }

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
            // User-defined types: enums are i32, classes are heap-allocated ptr
            if (tid >= 0 && types_.info(tid).kind == sema::TypeKind::Enum)
                return llvm::Type::getInt32Ty(*ctx_);
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
        else if (auto* e = dynamic_cast<const ast::EnumDecl*>(dp.get()))
            build_enum_type(*e);
        else if (auto* ns = dynamic_cast<const ast::NamespaceDecl*>(dp.get()))
            build_layouts(ns->decls);
    }
}

void Codegen::build_enum_type(const ast::EnumDecl& e) {
    // Register the enum type in codegen's type registry and record variant tags
    sema::TypeId id = types_.intern(e.name, sema::TypeKind::Enum);
    sema::TypeInfo& ti = types_.info(id);
    ti.variants.clear();
    int32_t tag = 0;
    for (const auto& v : e.variants) {
        sema::EnumVariantInfo vi;
        vi.name = v.name;
        vi.tag  = tag++;
        ti.variants.push_back(vi);
    }
    enum_types_[e.name] = id;
}

void Codegen::build_class_layout(const ast::ClassDecl& c) {
    ClassLayout layout;
    layout.class_name = c.name;

    // Inherit parent class fields first (single-inheritance: first class base).
    // Interfaces have no fields and are not in layouts_, so they are skipped.
    for (const auto& base : c.bases) {
        auto pit = layouts_.find(base.name);
        if (pit == layouts_.end()) continue; // interface or forward-declared class
        if (layout.parent_name.empty()) layout.parent_name = base.name;
        for (const auto& pf : pit->second.fields)
            layout.fields.push_back(pf); // preserve parent indices
        break; // single inheritance: only the first class base
    }

    // Collect this class's own fields, starting after inherited ones.
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* fd = dynamic_cast<const ast::FieldDecl*>(m.decl.get())) {
            if (fd->is_static) continue; // static fields are globals, not struct members
            FieldInfo fi;
            fi.name  = fd->name;
            fi.type  = types_.from_type_expr(fd->type);
            fi.index = static_cast<unsigned>(layout.fields.size()) + 1; // +1 for vtable
            fi.decl  = fd;
            layout.fields.push_back(fi);
        }
    }

    // Build LLVM struct: { ptr vtable, inherited_fields..., own_fields... }
    std::vector<llvm::Type*> field_types;
    field_types.push_back(ptr_type()); // vtable pointer
    for (const auto& f : layout.fields)
        field_types.push_back(lower_type(f.type));

    layout.struct_type = llvm::StructType::create(*ctx_, field_types, c.name);

    // Collect virtual methods (skip static methods — they don't participate in dispatch)
    unsigned slot = 0;
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            if (!f->is_ctor && !f->is_dtor && !f->is_static) {
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
        } else if (auto* ext = dynamic_cast<const ast::ExternDecl*>(dp.get())) {
            std::vector<llvm::Type*> ptypes;
            for (const auto& p : ext->params) ptypes.push_back(lower_type_expr(p.type));
            get_or_declare_rt(ext->name, lower_type_expr(ext->ret), ptypes);
        }
    }
}

// Returns the canonical mangled name for a class method or special member.
// Destructors get a dedicated symbol <Class>___dtor to avoid collision with
// the constructor which also carries the class name.
static std::string mangle_class_member(const std::string& cls,
                                       const ast::FunctionDecl& f) {
    if (f.is_dtor) return cls + "___dtor";
    return cls + "__" + f.name;
}

void Codegen::declare_class_methods(const ast::ClassDecl& c) {
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            std::string mangled = mangle_class_member(c.name, *f);
            // Don't declare trivial (empty-body) destructors — callers will
            // detect the absence of the symbol and use the free-only path.
            if (f->is_dtor && f->body && f->body->body.empty()) continue;
            if (mod_->getFunction(mangled)) continue;

            TypeId ret_tid = types_.from_type_expr(f->return_type);
            llvm::Type* ret_t = lower_type(ret_tid == TR::TID_UNKNOWN
                                          ? TR::TID_VOID : ret_tid);

            if (f->is_static) {
                // Static methods: no 'this' parameter
                std::vector<llvm::Type*> param_types;
                for (const auto& p : f->params)
                    param_types.push_back(lower_type_expr(p.type));
                auto* ft = llvm::FunctionType::get(ret_t, param_types, false);
                auto* fn = Function::Create(ft, Function::ExternalLinkage,
                                            mangled, *mod_);
                unsigned idx = 0;
                for (auto& arg : fn->args()) arg.setName(f->params[idx++].name);
                // Static methods do not participate in vtable dispatch; skip layout update
                continue;
            }

            // For instance methods: prepend 'this' pointer as first parameter
            std::vector<llvm::Type*> param_types;
            param_types.push_back(ptr_type()); // this
            for (const auto& p : f->params)
                param_types.push_back(lower_type_expr(p.type));

            auto* ft = llvm::FunctionType::get(ret_t, param_types, false);
            auto* fn = Function::Create(ft, Function::ExternalLinkage,
                                        mangled, *mod_);
            fn->arg_begin()->setName("this");
            unsigned idx = 1;
            for (auto it = std::next(fn->arg_begin()); it != fn->arg_end(); ++it)
                it->setName(f->params[idx++ - 1].name);

            // Record in layout (non-ctor/dtor instance methods only)
            if (!f->is_ctor && !f->is_dtor && layouts_.count(c.name)) {
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
    else if (auto* c  = dynamic_cast<const ast::ClassDecl*>(&d))     gen_class(*c);
    else if (auto* e  = dynamic_cast<const ast::EnumDecl*>(&d))      gen_enum(*e);
    else if (auto* ns = dynamic_cast<const ast::NamespaceDecl*>(&d)) gen_namespace(*ns);
    else if (auto* ext = dynamic_cast<const ast::ExternDecl*>(&d)) {
        // Declare external C function with its exact name (no namespace prefix)
        std::vector<llvm::Type*> ptypes;
        for (const auto& p : ext->params) ptypes.push_back(lower_type_expr(p.type));
        get_or_declare_rt(ext->name, lower_type_expr(ext->ret), ptypes);
    }
    // ImportDecl: nothing to generate
}

void Codegen::gen_enum(const ast::EnumDecl& /*e*/) {
    // Enum type registration is handled in build_enum_type (build_layouts pass).
    // Enum variant values are emitted as i32 constants inline in gen_member.
    // Nothing to emit here.
}

void Codegen::gen_func(const ast::FunctionDecl& f, const std::string& mangled,
                       TypeId class_type) {
    if (!f.body) return;
    if (f.modifier && (*f.modifier == "get" || *f.modifier == "set")) {
        // get/set modifiers compile as regular methods
    }

    Function* fn = mod_->getFunction(mangled);
    if (!fn) {
        fn = declare_function(f, mangled);
    }
    if (!fn->empty()) return; // already generated

    // Attach personality function so this function can serve as an EH frame.
    if (!fn->hasPersonalityFn()) {
        auto* pft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*ctx_), true);
        auto* personality_fn = llvm::cast<llvm::Constant>(
            mod_->getOrInsertFunction("__gxx_personality_v0", pft).getCallee());
        fn->setPersonalityFn(personality_fn);
    }

    BasicBlock* entry = BasicBlock::Create(*ctx_, "entry", fn);
    builder_->SetInsertPoint(entry);

    debug_func(f, fn, mangled);

    var_class_.clear(); // var→class map is local to each function body
    fn_var_types_.clear(); // fn-type map is local to each function body
    env_push();

    // If method: bind 'this' (not for static methods)
    auto arg_it = fn->arg_begin();
    if (class_type != TR::TID_UNKNOWN && !f.is_static) {
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

    // Register fn-typed params for closure dispatch
    for (const auto& p : f.params)
        if (p.type.name == "__fn") fn_var_types_[p.name] = p.type;

    // Call base class constructors from the initializer list (e.g. Dog(a) : Animal(a))
    if (f.is_ctor && !f.init_list.empty()) {
        Value* this_slot = env_lookup("this");
        if (this_slot) {
            Value* this_ptr = builder_->CreateLoad(ptr_type(), this_slot, "this");
            for (const auto& ie : f.init_list) {
                Function* base_fn = mod_->getFunction(mangle(ie.field, ie.field));
                if (base_fn) {
                    std::vector<Value*> args = {this_ptr};
                    auto pit = std::next(base_fn->arg_begin());
                    for (std::size_t i = 0;
                         i < ie.args.size() && pit != base_fn->arg_end(); ++i, ++pit) {
                        Value* av = gen_expr(*ie.args[i]);
                        av = coerce_to_llvm_type(av, pit->getType());
                        args.push_back(av);
                    }
                    emit_call(base_fn, args);
                }
            }
        }
    }

    // Save context
    auto saved_fn   = current_fn_;
    auto saved_ret  = current_ret_type_;
    auto saved_cls  = current_class_;
    current_fn_     = fn;
    current_ret_type_ = types_.from_type_expr(f.return_type);
    if (current_ret_type_ == TR::TID_UNKNOWN) current_ret_type_ = TR::TID_VOID;

    gen_stmts(f.body->body);

    // In a destructor, emit field destructors in reverse declaration order
    // after user-written dtor body but before the implicit return.
    if (f.is_dtor && !current_class_.empty()) {
        auto lit = layouts_.find(current_class_);
        if (lit != layouts_.end()) {
            const ClassLayout& layout = lit->second;
            Value* this_slot = env_lookup("this");
            if (this_slot && builder_->GetInsertBlock() &&
                    !builder_->GetInsertBlock()->getTerminator()) {
                Value* this_ptr = builder_->CreateLoad(ptr_type(), this_slot, "this");
                for (int i = static_cast<int>(layout.fields.size()) - 1; i >= 0; --i) {
                    const auto& fi = layout.fields[static_cast<std::size_t>(i)];
                    const std::string& fc = types_.name_of(fi.type);
                    if (fc.empty() || fc == "<unknown>" || fc == "<invalid>") continue;
                    if (!mod_->getFunction(fc + "___dtor")) continue;
                    Value* fgep = builder_->CreateStructGEP(
                        layout.struct_type, this_ptr, fi.index, fi.name + ".fgep");
                    Value* fptr = builder_->CreateLoad(ptr_type(), fgep, fi.name + ".fptr");
                    Value* tmp  = make_alloca(ptr_type(), fi.name + ".field.slot");
                    builder_->CreateStore(fptr, tmp);
                    emit_dtor(tmp, fc);
                }
            }
        }
    }

    // Emit implicit return if the block doesn't have a terminator.
    // RAII cleanup (class dtors + str releases) must happen before the
    // return instruction, just as gen_return() does for explicit returns.
    if (!builder_->GetInsertBlock()->getTerminator()) {
        emit_all_scope_cleanups();
        emit_all_str_releases();
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

    // Emit globals for static fields (before generating methods so they're
    // accessible in static method bodies).
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        auto* fd = dynamic_cast<const ast::FieldDecl*>(m.decl.get());
        if (!fd || !fd->is_static) continue;
        TypeId tid = types_.from_type_expr(fd->type);
        if (tid == TR::TID_UNKNOWN) tid = TR::TID_INT;
        llvm::Type* t = lower_type(tid);
        std::string gname = c.name + "._" + fd->name;
        if (mod_->getGlobalVariable(gname, true)) continue; // already emitted

        // Resolve constant initializer (literal values only; runtime inits zero-initialized)
        llvm::Constant* init_const = nullptr;
        if (fd->init) {
            if (auto* il = dynamic_cast<const ast::IntLitExpr*>(fd->init->get()))
                init_const = llvm::ConstantInt::get(t,
                    static_cast<uint64_t>(il->value), /*isSigned=*/true);
            else if (auto* ll = dynamic_cast<const ast::LongLitExpr*>(fd->init->get()))
                init_const = llvm::ConstantInt::get(t,
                    static_cast<uint64_t>(ll->value), /*isSigned=*/true);
            else if (auto* fl = dynamic_cast<const ast::FloatLitExpr*>(fd->init->get()))
                init_const = llvm::ConstantFP::get(t, fl->value);
            else if (auto* bl = dynamic_cast<const ast::BoolLitExpr*>(fd->init->get()))
                init_const = llvm::ConstantInt::get(t, bl->value ? 1 : 0);
        }
        if (!init_const) init_const = llvm::Constant::getNullValue(t);

        auto* gv = new llvm::GlobalVariable(
            *mod_, t, /*isConstant=*/fd->type.is_const,
            llvm::GlobalValue::InternalLinkage, init_const, gname);
        alloca_type_[gv] = t;
    }

    // Generate all methods
    for (const auto& m : c.members) {
        if (!m.decl) continue;
        if (auto* f = dynamic_cast<const ast::FunctionDecl*>(m.decl.get())) {
            // Skip empty destructors — no symbol means env_define uses the
            // trivial free-only path; saves one call per delete.
            if (f->is_dtor && f->body && f->body->body.empty()) continue;
            std::string mangled = mangle_class_member(c.name, *f);
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
    bool returns_int  = user_main->getReturnType()->isIntegerTy(32) ||
                        user_main->getReturnType()->isIntegerTy(64);

    // Declare duxrt_sys_init(i32, ptr) — initialises argc/argv for args module
    auto* i32t  = llvm::Type::getInt32Ty(*ctx_);
    auto* i64t  = llvm::Type::getInt64Ty(*ctx_);
    auto* pt    = ptr_type();
    llvm::FunctionCallee sys_init_callee = mod_->getOrInsertFunction(
        "duxrt_sys_init",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx_), {i32t, pt}, false));

    if (returns_void) {
        // Rename void @<whatever_main> → @dux_main
        user_main->setName("dux_main");

        // Create i32 @main(i32 argc, ptr argv) { sys_init(argc,argv); dux_main(); ret 0 }
        auto* ft  = llvm::FunctionType::get(i32t, {i32t, pt}, false);
        auto* fn  = Function::Create(ft, Function::ExternalLinkage, "main", *mod_);
        auto* bb  = BasicBlock::Create(*ctx_, "entry", fn);
        builder_->SetInsertPoint(bb);
        auto it = fn->arg_begin();
        llvm::Value* argc_v = &*it++;
        llvm::Value* argv_v = &*it;
        builder_->CreateCall(sys_init_callee, {argc_v, argv_v});
        builder_->CreateCall(user_main, {});
        builder_->CreateRet(llvm::ConstantInt::get(i32t, 0));
    } else if (returns_int) {
        // User wrote int main() — rename it and wrap with argc/argv + sys_init
        user_main->setName("dux_main");

        // Create i32 @main(i32 argc, ptr argv) { sys_init(argc,argv); ret dux_main() }
        auto* ft  = llvm::FunctionType::get(i32t, {i32t, pt}, false);
        auto* fn  = Function::Create(ft, Function::ExternalLinkage, "main", *mod_);
        auto* bb  = BasicBlock::Create(*ctx_, "entry", fn);
        builder_->SetInsertPoint(bb);
        auto it = fn->arg_begin();
        llvm::Value* argc_v = &*it++;
        llvm::Value* argv_v = &*it;
        builder_->CreateCall(sys_init_callee, {argc_v, argv_v});
        llvm::Value* ret_v = builder_->CreateCall(user_main, {});
        // Truncate to i32 if user returned i64
        if (user_main->getReturnType()->isIntegerTy(64))
            ret_v = builder_->CreateTrunc(ret_v, i32t, "ret32");
        builder_->CreateRet(ret_v);
    }
    (void)i64t; // suppress unused warning
}

void Codegen::gen_namespace(const ast::NamespaceDecl& ns) {
    if (ns.is_package_decl) return;
    build_layouts(ns.decls);
    declare_functions(ns.decls, ns.name);
    auto saved_ns   = current_namespace_;
    current_namespace_ = ns.name;
    for (const auto& d : ns.decls) gen_decl(*d, ns.name);
    gen_stmts(ns.stmts);
    current_namespace_ = saved_ns;
}

// ─── LTO: merge runtime bitcode ──────────────────────────────────────────────

bool Codegen::lto_merge_runtime() {
    // Empty path means the bitcode was not built (tools absent at configure time).
    std::string_view bc_path = DUXRT_BC_PATH;
    if (bc_path.empty()) return true;   // LTO disabled — not an error

    // Load the runtime bitcode file into a memory buffer.
    auto buf_or_err = llvm::MemoryBuffer::getFile(bc_path);
    if (!buf_or_err) {
        llvm::errs() << "dux: LTO: cannot read " << bc_path
                     << ": " << buf_or_err.getError().message() << '\n';
        return false;
    }

    // Parse bitcode into a module that shares our LLVMContext.
    auto mod_or_err = llvm::parseBitcodeFile(
        buf_or_err.get()->getMemBufferRef(), *ctx_);
    if (!mod_or_err) {
        llvm::handleAllErrors(mod_or_err.takeError(),
            [](const llvm::ErrorInfoBase& ei) {
                llvm::errs() << "dux: LTO: bitcode parse error: "
                             << ei.message() << '\n';
            });
        return false;
    }
    std::unique_ptr<llvm::Module> rt_mod = std::move(*mod_or_err);

    // Record which symbols are defined in the runtime module.  After linking
    // we mark them available_externally so they can be freely inlined but are
    // NOT emitted in the output object file — preventing duplicate-symbol
    // conflicts with the duxrt.a that is still linked by the system linker.
    std::vector<std::string> rt_defs;
    for (auto& fn : *rt_mod) {
        if (!fn.isDeclaration()) {
            rt_defs.push_back(fn.getName().str());
            // The bitcode was compiled with -O0 which stamps optnone+noinline
            // on every function.  Strip those attributes so the LTO optimiser
            // (which runs at the user-requested opt level) can inline freely.
            fn.removeFnAttr(llvm::Attribute::OptimizeNone);
            fn.removeFnAttr(llvm::Attribute::NoInline);
        }
    }

    // Merge: extern declares in mod_ are filled in by runtime definitions.
    if (llvm::Linker::linkModules(*mod_, std::move(rt_mod))) {
        llvm::errs() << "dux: LTO: module link failed\n";
        return false;
    }

    // Mark every linked-in runtime function internal.  Internal linkage:
    //   • lets the inliner inline them freely (no ODR or visibility constraints)
    //   • lets GlobalDCE remove them after inlining (dead internal functions)
    //   • keeps non-inlined copies private to this .o, so the linker does not
    //     see a duplicate-symbol conflict with the same name in duxrt.a
    //
    // Also mark any internal helpers (e.g. static strbuf_grow) that were
    // pulled in by the link; they already have internal linkage but make sure
    // they are reachable via this loop to keep them from being DCE-removed
    // before the inliner runs.
    for (const auto& name : rt_defs) {
        if (auto* fn = mod_->getFunction(name))
            if (!fn->isDeclaration())
                fn->setLinkage(llvm::GlobalValue::InternalLinkage);
    }

    return true;
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
            {"abs_i", {"duxrt_math_abs_i",  TR::TID_LONG,   {TR::TID_LONG}}},
            {"min_i", {"duxrt_math_min_i",  TR::TID_LONG,   {TR::TID_LONG, TR::TID_LONG}}},
            {"max_i", {"duxrt_math_max_i",  TR::TID_LONG,   {TR::TID_LONG, TR::TID_LONG}}},
        }},
        {"str", {
            // ── core ──────────────────────────────────────────────────────────
            {"concat",      {"duxrt_str_concat",      TR::TID_STR,  {TR::TID_STR, TR::TID_STR}}},
            {"from_int",    {"duxrt_str_from_int",    TR::TID_STR,  {TR::TID_LONG}}},
            {"from_double", {"duxrt_str_from_double", TR::TID_STR,  {TR::TID_DOUBLE}}},
            {"length",      {"duxrt_str_length",      TR::TID_LONG, {TR::TID_STR}}},
            {"slice",       {"duxrt_str_slice",       TR::TID_STR,  {TR::TID_STR, TR::TID_LONG, TR::TID_LONG}}},
            {"index",       {"duxrt_str_index",       TR::TID_STR,  {TR::TID_STR, TR::TID_LONG}}},
            // C functions that return int (0/1) — declare as TID_INT (i32) to
            // avoid conflicts with the i32-based string-comparison path in gen_binary.
            {"eq",          {"duxrt_str_eq",          TR::TID_INT,  {TR::TID_STR, TR::TID_STR}}},
            // ── extended ──────────────────────────────────────────────────────
            {"ord",         {"duxrt_str_ord",         TR::TID_LONG, {TR::TID_STR}}},
            {"chr",         {"duxrt_str_chr",         TR::TID_STR,  {TR::TID_LONG}}},
            {"cmp",         {"duxrt_str_cmp",         TR::TID_INT,  {TR::TID_STR, TR::TID_STR}}},
            // contains/starts_with/ends_with return C int (0/1) → TID_INT
            {"contains",    {"duxrt_str_contains",    TR::TID_INT,  {TR::TID_STR, TR::TID_STR}}},
            {"find",        {"duxrt_str_find",        TR::TID_LONG, {TR::TID_STR, TR::TID_STR}}},
            {"rfind",       {"duxrt_str_rfind",       TR::TID_LONG, {TR::TID_STR, TR::TID_STR}}},
            {"starts_with", {"duxrt_str_starts_with", TR::TID_INT,  {TR::TID_STR, TR::TID_STR}}},
            {"ends_with",   {"duxrt_str_ends_with",   TR::TID_INT,  {TR::TID_STR, TR::TID_STR}}},
            {"replace",     {"duxrt_str_replace",     TR::TID_STR,  {TR::TID_STR, TR::TID_STR, TR::TID_STR}}},
            {"replace_all", {"duxrt_str_replace_all", TR::TID_STR,  {TR::TID_STR, TR::TID_STR, TR::TID_STR}}},
            {"to_upper",    {"duxrt_str_to_upper",    TR::TID_STR,  {TR::TID_STR}}},
            {"to_lower",    {"duxrt_str_to_lower",    TR::TID_STR,  {TR::TID_STR}}},
            {"trim",        {"duxrt_str_trim",        TR::TID_STR,  {TR::TID_STR}}},
            {"trim_start",  {"duxrt_str_trim_start",  TR::TID_STR,  {TR::TID_STR}}},
            {"trim_end",    {"duxrt_str_trim_end",    TR::TID_STR,  {TR::TID_STR}}},
            {"repeat",      {"duxrt_str_repeat",      TR::TID_STR,  {TR::TID_STR, TR::TID_LONG}}},
            {"split",       {"duxrt_str_split",       TR::TID_LIST, {TR::TID_STR, TR::TID_STR}}},
            // to_long / to_double are Dux wrappers (not direct C); handled by
            // the fallback path in try_stdlib_call.
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
    } else if (!imp.global_scope) {
        // Namespace import (full, selective, or glob): calls use  ns.fn()  syntax.
        // Register the accessor name (alias if set, else last path component) so
        // try_stdlib_call can route  ns.fn()  →  mangle(ns, fn).
        const std::string ns_name = imp.alias.empty() ? mod : imp.alias;
        user_module_imports_.insert(ns_name);
    }
    // Selective global imports (global_scope=true) inject into global scope —
    // no routing table needed; they resolve as plain function calls.
}

Value* Codegen::try_stdlib_call(const ast::CallExpr& e) {
    auto* mem = dynamic_cast<const ast::MemberExpr*>(e.callee.get());
    if (!mem) return nullptr;
    auto* id = dynamic_cast<const ast::IdentExpr*>(mem->object.get());
    if (!id) return nullptr;

    const std::string& mod = id->name;

    // ── User file-based module: mod.fn(args) → mangle(mod, fn) ──────────
    if (user_module_imports_.count(mod)) {
        std::string mangled = mangle(mod, mem->member);
        Function* fn = mod_->getFunction(mangled);
        if (fn) {
            std::vector<Value*> args;
            auto param_it = fn->arg_begin();
            for (std::size_t i = 0; i < e.args.size() && param_it != fn->arg_end();
                 ++i, ++param_it) {
                Value* v = gen_expr(*e.args[i]);
                v = coerce_to_llvm_type(v, param_it->getType());
                args.push_back(v);
            }
            return builder_->CreateCall(fn, args);
        }
        return llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptr_type()));
    }

    if (!stdlib_imports_.count(mod)) return nullptr;

    auto it_mod = stdlib_table().find(mod);
    if (it_mod == stdlib_table().end()) {
        // Module in stdlib_imports_ but not in table — try Dux-compiled namespace fn
        std::string mangled = mangle(mod, mem->member);
        if (Function* ns_fn = mod_->getFunction(mangled);
            ns_fn && ns_fn->arg_size() == e.args.size()) {
            std::vector<Value*> args;
            auto param_it = ns_fn->arg_begin();
            for (const auto& a : e.args) {
                Value* v = gen_expr(*a);
                if (param_it != ns_fn->arg_end())
                    v = coerce_to_llvm_type(v, (param_it++)->getType());
                args.push_back(v);
            }
            return builder_->CreateCall(ns_fn, args);
        }
        return nullptr;
    }

    auto it_fn = it_mod->second.find(mem->member);
    if (it_fn == it_mod->second.end()) {
        // Function not in the hard-coded table — it may be a Dux-level wrapper
        // compiled from the stdlib .dux file (e.g. str.to_long, str.to_double).
        // Try it as a plain global function (stdlib globals are not namespace-mangled).
        if (Function* fallback_fn = mod_->getFunction(mem->member)) {
            std::vector<Value*> args;
            auto param_it = fallback_fn->arg_begin();
            for (const auto& a : e.args) {
                Value* v = gen_expr(*a);
                if (param_it != fallback_fn->arg_end())
                    v = coerce_to_llvm_type(v, (param_it++)->getType());
                args.push_back(v);
            }
            return builder_->CreateCall(fallback_fn, args);
        }
        return nullptr;
    }

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
    if (auto* mx = dynamic_cast<const ast::MatchStmt*>(&s))    { gen_match(*mx);   return; }
    if (auto* tc = dynamic_cast<const ast::TryCatchStmt*>(&s)) { gen_try_catch(*tc);return;}
    if (auto* r  = dynamic_cast<const ast::ReturnStmt*>(&s))   { gen_return(*r);   return; }
    if (auto* br = dynamic_cast<const ast::BreakStmt*>(&s)) {
        if (loop_stack_.empty()) return;
        if (br->label) {
            for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it)
                if (it->label == *br->label) {
                    emit_all_scope_cleanups();
                    emit_all_str_releases();
                    builder_->CreateBr(it->exit);
                    return;
                }
            err(br->loc, "label '" + *br->label + "' not found for break");
            return;
        }
        emit_all_scope_cleanups();
        emit_all_str_releases();
        builder_->CreateBr(loop_stack_.back().exit);
        return;
    }
    if (auto* co = dynamic_cast<const ast::ContinueStmt*>(&s)) {
        if (loop_stack_.empty()) return;
        if (co->label) {
            for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it)
                if (it->label == *co->label) {
                    emit_all_scope_cleanups();
                    emit_all_str_releases();
                    builder_->CreateBr(it->header);
                    return;
                }
            err(co->loc, "label '" + *co->label + "' not found for continue");
            return;
        }
        emit_all_scope_cleanups();
        emit_all_str_releases();
        builder_->CreateBr(loop_stack_.back().header);
        return;
    }
    if (auto* a = dynamic_cast<const ast::AssertStmt*>(&s))    { gen_assert(*a);   return; }
    if (auto* d = dynamic_cast<const ast::DeleteStmt*>(&s))    { gen_delete(*d);   return; }
    if (auto* df = dynamic_cast<const ast::DeferStmt*>(&s))    { gen_defer(*df);   return; }
    if (auto* th = dynamic_cast<const ast::ThrowStmt*>(&s))    { gen_throw(*th);   return; }
    if (auto* us = dynamic_cast<const ast::UnsafeStmt*>(&s))   { gen_stmts(us->body); return; }
    if (auto* ls = dynamic_cast<const ast::LabeledStmt*>(&s))  {
        // Propagate label into the inner loop/while statement
        pending_label_ = ls->label;
        gen_stmt(*ls->stmt);
        pending_label_.clear();
        return;
    }
    if (auto* us = dynamic_cast<const ast::UnsafeStmt*>(&s))   { gen_stmts(us->body); return; }
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
        // Check if iterable is a list type
        TypeId iter_tid = type_id_of(*s.iterable);
        bool is_list = (iter_tid == TR::TID_LIST || iter_tid == TR::TID_UNKNOWN);

        if (is_list) {
            // List iteration: evaluate iterable, get length, iterate with index
            Value* list_val = gen_expr(*s.iterable);

            auto* len_fn = get_or_declare_rt("duxrt_list_len",
                llvm::Type::getInt64Ty(*ctx_), {ptr_type()});
            Value* len_val = builder_->CreateCall(len_fn, {list_val}, "list.len");

            // Allocate storage for list pointer and length (must survive across BBs)
            Value* list_ptr_alloca = make_alloca(ptr_type(), "list.ptr");
            builder_->CreateStore(list_val, list_ptr_alloca);

            Value* len_alloca = make_alloca(llvm::Type::getInt64Ty(*ctx_), "list.len");
            builder_->CreateStore(len_val, len_alloca);

            Value* idx_alloca = make_alloca(llvm::Type::getInt64Ty(*ctx_), "for.idx");
            builder_->CreateStore(
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx_), 0), idx_alloca);

            builder_->CreateBr(hdr_bb);

            // hdr_bb: check idx < len
            builder_->SetInsertPoint(hdr_bb);
            Value* idx_hdr = builder_->CreateLoad(
                llvm::Type::getInt64Ty(*ctx_), idx_alloca, "idx");
            Value* len_hdr = builder_->CreateLoad(
                llvm::Type::getInt64Ty(*ctx_), len_alloca, "len");
            Value* cond = builder_->CreateICmpSLT(idx_hdr, len_hdr, "for.cond");
            builder_->CreateCondBr(cond, body_bb, exit_bb);

            // body_bb: load element into var_alloca, then gen_stmt
            builder_->SetInsertPoint(body_bb);
            Value* list_v = builder_->CreateLoad(ptr_type(), list_ptr_alloca, "list.v");
            Value* idx_body = builder_->CreateLoad(
                llvm::Type::getInt64Ty(*ctx_), idx_alloca, "idx.body");
            auto* get_fn = get_or_declare_rt("duxrt_list_get",
                ptr_type(), {ptr_type(), llvm::Type::getInt64Ty(*ctx_)});
            Value* elem = builder_->CreateCall(get_fn, {list_v, idx_body}, "elem");
            if (var_t == ptr_type())
                builder_->CreateStore(elem, var_alloca);

            loop_stack_.push_back({incr_bb, exit_bb, pending_label_});
            pending_label_.clear();
            gen_stmt(*s.body);
            loop_stack_.pop_back();
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateBr(incr_bb);

            // incr_bb: idx++
            builder_->SetInsertPoint(incr_bb);
            Value* idx_incr = builder_->CreateLoad(
                llvm::Type::getInt64Ty(*ctx_), idx_alloca, "idx.incr");
            Value* next_idx = builder_->CreateAdd(
                idx_incr,
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx_), 1));
            builder_->CreateStore(next_idx, idx_alloca);
            builder_->CreateBr(hdr_bb);

            builder_->SetInsertPoint(exit_bb);
            env_pop();
            return;
        } else {
            // Non-list iterable: sema reports an error; emit a no-op branch.
            builder_->CreateBr(exit_bb);
            builder_->SetInsertPoint(hdr_bb);
            builder_->CreateBr(exit_bb);
        }
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
    auto* exit_bb = BasicBlock::Create(*ctx_, "sw.end", fn);

    // Evaluate case constant values before creating the switch (constants don't
    // emit instructions, but keep this order to avoid inserting after a terminator)
    std::vector<llvm::ConstantInt*> case_consts;
    case_consts.reserve(s.cases.size());
    for (const auto& c : s.cases) {
        if (c.value) {
            Value* cv = gen_expr(**c.value);
            case_consts.push_back(llvm::dyn_cast<llvm::ConstantInt>(cv));
        } else {
            case_consts.push_back(nullptr);
        }
    }

    Value* sw_val = gen_expr(*s.expr);
    // Ensure integer type for switch
    if (!sw_val->getType()->isIntegerTy())
        sw_val = builder_->CreateFPToSI(sw_val, llvm::Type::getInt64Ty(*ctx_));

    auto* sw_inst = builder_->CreateSwitch(sw_val, exit_bb,
                                           static_cast<unsigned>(s.cases.size()));

    // Create all case entry blocks and register them with the switch instruction
    std::vector<BasicBlock*> case_bbs;
    case_bbs.reserve(s.cases.size());
    for (std::size_t i = 0; i < s.cases.size(); ++i) {
        BasicBlock* case_bb;
        if (s.cases[i].value) {
            case_bb = BasicBlock::Create(*ctx_, "sw.case", fn);
            if (case_consts[i])
                sw_inst->addCase(case_consts[i], case_bb);
        } else {
            case_bb = BasicBlock::Create(*ctx_, "sw.default", fn);
            sw_inst->setDefaultDest(case_bb);
        }
        case_bbs.push_back(case_bb);
    }

    loop_stack_.push_back({nullptr, exit_bb, {}}); // break goes to exit

    // Generate case bodies; unterminated cases fall through to the next case
    for (std::size_t i = 0; i < s.cases.size(); ++i) {
        builder_->SetInsertPoint(case_bbs[i]);
        env_push();
        gen_stmts(s.cases[i].body);
        env_pop();
        if (!builder_->GetInsertBlock()->getTerminator()) {
            BasicBlock* next_bb = (i + 1 < case_bbs.size()) ? case_bbs[i + 1] : exit_bb;
            builder_->CreateBr(next_bb);
        }
    }

    loop_stack_.pop_back();
    builder_->SetInsertPoint(exit_bb);
}

void Codegen::gen_match(const ast::MatchStmt& s) {
    Function* fn = builder_->GetInsertBlock()->getParent();
    Value* match_val = gen_expr(*s.expr);

    // Ensure match value is an integer (enums are i32)
    if (!match_val->getType()->isIntegerTy()) {
        match_val = builder_->CreateFPToSI(match_val,
                                           llvm::Type::getInt32Ty(*ctx_));
    }

    auto* exit_bb = BasicBlock::Create(*ctx_, "match.end", fn);

    // No wildcard: generate an unreachable trap for non-exhaustive match
    auto* trap_bb = BasicBlock::Create(*ctx_, "match.trap", fn);
    BasicBlock* default_bb = nullptr;

    // Collect non-wildcard arms for switch, find wildcard arm
    const ast::MatchArm* wildcard_arm = nullptr;
    std::vector<std::pair<long long, const ast::MatchArm*>> case_arms;

    for (const auto& arm : s.arms) {
        switch (arm.pattern.kind) {
        case ast::MatchPattern::Kind::Wildcard:
            wildcard_arm = &arm;
            break;
        case ast::MatchPattern::Kind::EnumVariant: {
            // Look up the tag value for this variant
            auto it = enum_types_.find(arm.pattern.enum_name);
            if (it != enum_types_.end()) {
                const sema::TypeInfo& ti = types_.info(it->second);
                for (const auto& v : ti.variants) {
                    if (v.name == arm.pattern.variant_name) {
                        case_arms.emplace_back(v.tag, &arm);
                        break;
                    }
                }
            }
            break;
        }
        case ast::MatchPattern::Kind::IntLit:
            case_arms.emplace_back(arm.pattern.int_value, &arm);
            break;
        case ast::MatchPattern::Kind::BoolLit:
            case_arms.emplace_back(arm.pattern.bool_value ? 1LL : 0LL, &arm);
            break;
        }
    }

    // Build wildcard block if any; otherwise fall through to trap
    if (wildcard_arm) {
        default_bb = BasicBlock::Create(*ctx_, "match.wildcard", fn);
    } else {
        default_bb = trap_bb;
    }

    auto* sw = builder_->CreateSwitch(match_val, default_bb,
                                      static_cast<unsigned>(case_arms.size()));

    // Emit case arms
    for (const auto& [tag, arm] : case_arms) {
        auto* case_bb = BasicBlock::Create(*ctx_, "match.arm", fn);
        auto* case_val = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(*ctx_), static_cast<uint64_t>(tag));
        sw->addCase(case_val, case_bb);
        builder_->SetInsertPoint(case_bb);
        env_push();
        gen_stmts(arm->body);
        env_pop();
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(exit_bb);
    }

    // Emit wildcard arm
    if (wildcard_arm) {
        builder_->SetInsertPoint(default_bb);
        env_push();
        gen_stmts(wildcard_arm->body);
        env_pop();
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(exit_bb);
    }

    // Emit trap block (reached when match is not exhaustive)
    builder_->SetInsertPoint(trap_bb);
    {
        auto* trap_fn = llvm::Intrinsic::getDeclaration(mod_.get(), llvm::Intrinsic::trap);
        builder_->CreateCall(trap_fn, {});
        builder_->CreateUnreachable();
    }

    builder_->SetInsertPoint(exit_bb);
}

void Codegen::gen_try_catch(const ast::TryCatchStmt& s) {
    Function* fn = builder_->GetInsertBlock()->getParent();

    auto* try_bb   = BasicBlock::Create(*ctx_, "try.body",  fn);
    auto* lp_bb    = BasicBlock::Create(*ctx_, "try.lp",    fn);
    auto* catch_bb = BasicBlock::Create(*ctx_, "try.catch", fn);
    auto* end_bb   = BasicBlock::Create(*ctx_, "try.end",   fn);

    builder_->CreateBr(try_bb);

    // ── try body ──────────────────────────────────────────────────────────
    // Open the try-body scope manually (not via gen_block) so the scope stays
    // open when we generate the LP cleanup code below.
    builder_->SetInsertPoint(try_bb);
    env_push();
    lp_stack_.push_back(lp_bb);

    auto* try_body_block = dynamic_cast<ast::BlockStmt*>(s.try_body.get());
    gen_stmts(try_body_block->body);

    // Do NOT pop lp_stack_ yet — the LP block cleanup code below calls
    // emit_all_scope_cleanups(), which must emit invoke (not plain call)
    // so that a throw inside a defer/dtor is covered by the landingpad
    // itself (R-3 fix: lp_stack_ stays live until after LP cleanup).

    // Save the normal-path exit block before switching to the LP.
    auto* try_exit_bb = builder_->GetInsertBlock();

    // ── landing pad ───────────────────────────────────────────────────────
    // Generate LP content NOW, while the try-body scope is still in
    // cleanup_scopes_ — so emit_all_scope_cleanups() sees a/b/etc.
    // Catches any C++ exception (catch ptr null = catch-all).
    builder_->SetInsertPoint(lp_bb);
    auto* lp_ty   = llvm::StructType::get(*ctx_,
                        {ptr_type(), llvm::Type::getInt32Ty(*ctx_)});
    auto* lp_inst = builder_->CreateLandingPad(lp_ty, 1, "lp");
    lp_inst->addClause(llvm::ConstantPointerNull::get(
                           llvm::cast<llvm::PointerType>(ptr_type())));

    // RAII + defer cleanup on the unwind path (objects still in scope).
    emit_all_scope_cleanups();  // still have lp in stack so nested throws are covered
    emit_all_str_releases();
    lp_stack_.pop_back();       // NOW pop, after cleanup is done (R-3 fix)

    // __cxa_begin_catch: resolve the thrown object.
    Value* exc_lp_ptr = builder_->CreateExtractValue(lp_inst, {0u}, "exc.lp");
    auto* begin_catch = get_or_declare_rt("__cxa_begin_catch",
                            ptr_type(), {ptr_type()});
    Value* caught_raw = builder_->CreateCall(begin_catch, {exc_lp_ptr}, "caught.raw");
    Value* exc_val    = builder_->CreateLoad(ptr_type(), caught_raw, "exc.val");
    Value* exc_slot   = make_alloca(ptr_type(), "exc.slot");
    builder_->CreateStore(exc_val, exc_slot);
    builder_->CreateBr(catch_bb);

    // ── normal-path cleanup (back on the try-exit block) ─────────────────
    // env_pop fires destructors for objects in the try scope on the
    // non-exception path.  Restore the builder first.
    builder_->SetInsertPoint(try_exit_bb);
    env_pop();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(end_bb);

    // ── catch body ────────────────────────────────────────────────────────
    builder_->SetInsertPoint(catch_bb);
    env_push();
    if (s.catch_var) {
        Value* cv_alloca = make_alloca(ptr_type(), *s.catch_var);
        builder_->CreateStore(
            builder_->CreateLoad(ptr_type(), exc_slot, "exc.v"), cv_alloca);
        env_define(*s.catch_var, cv_alloca);
    }
    // Register __cxa_end_catch as a cleanup so it fires even when the catch
    // body exits via return or rethrow (EH-3 fix).
    llvm::Function* end_catch_fn = get_or_declare_rt(
        "__cxa_end_catch", llvm::Type::getVoidTy(*ctx_), {});
    cleanup_scopes_.back().push_back(
        {nullptr, "", nullptr, end_catch_fn});
    gen_stmt(*s.catch_body);
    env_pop();  // env_pop fires emit_scope_cleanup which calls __cxa_end_catch
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(end_bb);

    builder_->SetInsertPoint(end_bb);
}

void Codegen::gen_return(const ast::ReturnStmt& s) {
    Value* val = nullptr;
    if (s.value) {
        val = gen_expr(**s.value);
        TypeId val_tid = type_id_of(**s.value);
        val = coerce(val, val_tid, current_ret_type_);
        // Retain the return value before releasing scopes so the caller gets
        // a valid owned reference even when returning a local str variable.
        if (current_ret_type_ == TR::TID_STR)
            val = maybe_retain_str(val);
    }
    // Destroy class instances, run defers, and release str variables across all active scopes.
    emit_all_scope_cleanups();
    emit_all_str_releases();
    if (val)
        builder_->CreateRet(val);
    else
        builder_->CreateRetVoid();
}

void Codegen::gen_var_decl(const ast::VarDeclStmt& s) {
    TypeId decl_tid = types_.from_type_expr(s.type);

    for (const auto& [name, init_ptr] : s.decls) {
        TypeId var_tid = decl_tid;

        if (s.is_static) {
            // Static local variable: module-level global with once-only init guard
            // Use __cxa_guard_acquire/__cxa_guard_release for thread-safe initialization.
            if (var_tid == TR::TID_UNKNOWN) var_tid = TR::TID_INT;
            llvm::Type* t = lower_type(var_tid);

            // Create the static global for the variable value
            std::string gname = "__dux_static_" + (current_fn_ ? current_fn_->getName().str() : "") + "_" + name;
            auto* gv = new llvm::GlobalVariable(
                *mod_, t, /*isConstant=*/s.is_const,
                llvm::GlobalValue::InternalLinkage,
                llvm::Constant::getNullValue(t), gname);
            // Register element type so load_var uses the right type (not i64 fallback)
            alloca_type_[gv] = t;

            if (init_ptr) {
                // Create a guard byte (i8) — ABI: non-zero = initialized
                auto* guard_gv = new llvm::GlobalVariable(
                    *mod_, llvm::Type::getInt8Ty(*ctx_), false,
                    llvm::GlobalValue::InternalLinkage,
                    llvm::ConstantInt::get(llvm::Type::getInt8Ty(*ctx_), 0),
                    gname + ".guard");

                Function* fn = builder_->GetInsertBlock()->getParent();
                auto* init_check = BasicBlock::Create(*ctx_, name + ".init.check", fn);
                auto* init_body  = BasicBlock::Create(*ctx_, name + ".init.body",  fn);
                auto* init_done  = BasicBlock::Create(*ctx_, name + ".init.done",  fn);

                // If guard == 0 → not initialized yet
                builder_->CreateBr(init_check);
                builder_->SetInsertPoint(init_check);
                Value* guard_val = builder_->CreateLoad(
                    llvm::Type::getInt8Ty(*ctx_), guard_gv, "guard");
                Value* not_init  = builder_->CreateICmpEQ(guard_val,
                    llvm::ConstantInt::get(llvm::Type::getInt8Ty(*ctx_), 0), "not.init");
                builder_->CreateCondBr(not_init, init_body, init_done);

                builder_->SetInsertPoint(init_body);
                Value* init_val = gen_expr(*init_ptr);
                TypeId init_tid = type_id_of(*init_ptr);
                init_val = coerce(init_val, init_tid, var_tid);
                builder_->CreateStore(init_val, gv);
                builder_->CreateStore(
                    llvm::ConstantInt::get(llvm::Type::getInt8Ty(*ctx_), 1), guard_gv);
                builder_->CreateBr(init_done);

                builder_->SetInsertPoint(init_done);
            }

            // Register as env entry pointing to the global (load/store as usual)
            env_define(name, gv, var_tid);
        } else {
            // Normal (non-static) local variable
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
                if (var_tid == TR::TID_STR)
                    init_val = maybe_retain_str(init_val);
                if (init_val->getType() != t)
                    init_val = coerce(init_val, decl_tid, var_tid);
                builder_->CreateStore(init_val, alloca);
            } else {
                builder_->CreateStore(llvm::Constant::getNullValue(t), alloca);
            }
            env_define(name, alloca, var_tid);
            if (init_ptr) {
                if (auto* lam = dynamic_cast<const ast::LambdaExpr*>(init_ptr.get())) {
                    ast::TypeExpr te;
                    te.name = "__fn";
                    for (const auto& p : lam->params)
                        te.fn_params.push_back(p.type);
                    te.fn_ret = lam->inferred_ret;
                    fn_var_types_[name] = te;
                    // Register closure env for free() on scope exit
                    if (!cleanup_scopes_.empty())
                        cleanup_scopes_.back().push_back({alloca, "__closure", nullptr});
                }
            }
            // Track variable→class for member access resolution
            if (!s.type.name.empty() && layouts_.count(s.type.name))
                var_class_[name] = s.type.name;
        }
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
    TypeId tid = type_id_of(*s.expr);
    if (tid == TR::TID_STR) {
        // Strings are refcounted — must release, not raw free.
        Value* ptr = gen_expr(*s.expr);
        emit_str_release(ptr);
        return;
    }

    // For class instances: call the destructor then free, and null the slot
    // so that the RAII cleanup in env_pop() skips the already-freed object.
    Value* slot = lvalue_of(*s.expr);
    if (slot) {
        Value* ptr = builder_->CreateLoad(ptr_type(), slot, "del.obj");
        // Null guard (safe to delete null)
        Function* fn   = builder_->GetInsertBlock()->getParent();
        auto* call_bb  = BasicBlock::Create(*ctx_, "del.call", fn);
        auto* end_bb   = BasicBlock::Create(*ctx_, "del.end",  fn);
        Value* null_v  = llvm::ConstantPointerNull::get(
                             llvm::cast<llvm::PointerType>(ptr_type()));
        builder_->CreateCondBr(
            builder_->CreateICmpEQ(ptr, null_v, "del.isnull"),
            end_bb, call_bb);

        builder_->SetInsertPoint(call_bb);
        std::string cls_name = resolve_class_name(*s.expr, tid);
        if (!cls_name.empty()) {
            std::string dtor_sym = cls_name + "___dtor";
            if (Function* dtor_fn = mod_->getFunction(dtor_sym))
                builder_->CreateCall(dtor_fn, {ptr});
        }
        auto* free_fn = get_or_declare_rt("free",
            llvm::Type::getVoidTy(*ctx_), {ptr_type()});
        builder_->CreateCall(free_fn, {ptr});
        // Null out the slot to prevent a second dtor call from RAII cleanup.
        builder_->CreateStore(null_v, slot);
        builder_->CreateBr(end_bb);

        builder_->SetInsertPoint(end_bb);
    } else {
        // Fallback: cannot get lvalue; just free without null-guard.
        Value* ptr = gen_expr(*s.expr);
        auto* free_fn = get_or_declare_rt("free",
            llvm::Type::getVoidTy(*ctx_), {ptr_type()});
        builder_->CreateCall(free_fn, {ptr});
    }
}

// ─── Expressions ─────────────────────────────────────────────────────────────

Value* Codegen::gen_expr(const ast::Expr& e) {
    if (auto* p = dynamic_cast<const ast::IntLitExpr*>(&e))
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_),
                                      static_cast<int32_t>(p->value), true);

    if (auto* p = dynamic_cast<const ast::LongLitExpr*>(&e))
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx_), p->value, true);

    if (auto* p = dynamic_cast<const ast::FloatLitExpr*>(&e))
        return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*ctx_), p->value);

    if (auto* p = dynamic_cast<const ast::RealLitExpr*>(&e))
        return llvm::ConstantFP::get(llvm::Type::getFloatTy(*ctx_), p->value);

    if (auto* p = dynamic_cast<const ast::StringLitExpr*>(&e)) {
        return str_literal(p->value);
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
    if (auto* lam = dynamic_cast<const ast::LambdaExpr*>(&e)) return gen_lambda(*lam);

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
            // For str: release old value, then retain new value before storing.
            if (rhs_tid == TR::TID_STR) {
                auto tit = alloca_type_.find(slot);
                if (tit != alloca_type_.end() && tit->second->isPointerTy()) {
                    Value* old_val = builder_->CreateLoad(ptr_type(), slot, "str.old");
                    emit_str_release(old_val);
                }
                rhs = maybe_retain_str(rhs);
            }
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

    // Operator overloading: dispatch to class method if LHS is a user type
    {
        static const std::unordered_map<std::string, std::string> op_methods = {
            {"+",  "operator__add"}, {"-",  "operator__sub"},
            {"*",  "operator__mul"}, {"/",  "operator__div"},
            {"==", "operator__eq"},  {"!=", "operator__ne"},
            {"<",  "operator__lt"},  {"<=", "operator__le"},
            {">",  "operator__gt"},  {">=", "operator__ge"},
        };
        auto it = op_methods.find(op);
        if (it != op_methods.end()) {
            std::string cls = resolve_class_name(*e.left, lt);
            if (!cls.empty()) {
                std::string sym = mangle(cls, it->second);
                if (Function* fn = mod_->getFunction(sym))
                    return emit_call(fn, {L, R});
            }
        }
    }

    // Promote types — also fall back to checking the actual LLVM types for
    // cases where type_id is TID_UNKNOWN (e.g. stdlib member calls like math.sqrt).
    bool is_fp = (lt == TR::TID_DOUBLE || lt == TR::TID_REAL ||
                  rt == TR::TID_DOUBLE || rt == TR::TID_REAL ||
                  L->getType()->isFloatingPointTy() ||
                  R->getType()->isFloatingPointTy());
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

    // Enum equality/inequality — compare discriminant tags, not pointers.
    // Payload enums are represented as ptr to {i32 tag, ptr payload}; simple
    // enums are i32 constants.  Normalise both sides to their i32 tag first.
    auto extract_enum_tag = [&](Value* v, TypeId tid) -> Value* {
        const sema::TypeInfo& ti = types_.info(tid);
        bool is_payload = std::any_of(ti.variants.begin(), ti.variants.end(),
            [](const sema::EnumVariantInfo& vi){ return !vi.payload.empty(); });
        if (is_payload) {
            // ptr → GEP offset 0 → i32 tag
            return builder_->CreateLoad(llvm::Type::getInt32Ty(*ctx_),
                builder_->CreateStructGEP(
                    llvm::StructType::get(*ctx_, {llvm::Type::getInt32Ty(*ctx_), ptr_type()}),
                    v, 0, "enum.tag"),
                "tag");
        }
        // Simple enum is already i32
        return v;
    };
    if ((op == "==" || op == "!=") &&
        lt == rt && lt != TR::TID_UNKNOWN &&
        types_.info(lt).kind == sema::TypeKind::Enum) {
        Value* tagL = extract_enum_tag(L, lt);
        Value* tagR = extract_enum_tag(R, rt);
        if (op == "==") return builder_->CreateICmpEQ(tagL, tagR);
        else            return builder_->CreateICmpNE(tagL, tagR);
    }
    // Mixed enum vs int-literal comparison (e.g. `color == Color.Red`)
    // where one side resolved to i32 and the other to ptr: load tag from ptr.
    if ((op == "==" || op == "!=") && lt != rt) {
        bool lhs_is_enum = lt != TR::TID_UNKNOWN &&
                           types_.info(lt).kind == sema::TypeKind::Enum;
        bool rhs_is_enum = rt != TR::TID_UNKNOWN &&
                           types_.info(rt).kind == sema::TypeKind::Enum;
        if (lhs_is_enum || rhs_is_enum) {
            TypeId etid = lhs_is_enum ? lt : rt;
            Value* tagL = lhs_is_enum ? extract_enum_tag(L, etid) : L;
            Value* tagR = rhs_is_enum ? extract_enum_tag(R, etid) : R;
            // Ensure both are i32
            auto* i32ty = llvm::Type::getInt32Ty(*ctx_);
            if (tagL->getType() != i32ty)
                tagL = builder_->CreateTruncOrBitCast(tagL, i32ty);
            if (tagR->getType() != i32ty)
                tagR = builder_->CreateTruncOrBitCast(tagR, i32ty);
            if (op == "==") return builder_->CreateICmpEQ(tagL, tagR);
            else            return builder_->CreateICmpNE(tagL, tagR);
        }
    }

    // String equality/inequality — use runtime comparison (not pointer equality).
    // duxrt_str_eq is declared as returning i32 (from the stdlib_table TID_INT entry),
    // but handle i1 gracefully in case it was pre-declared differently.
    if ((op == "==" || op == "!=") && (lt == TR::TID_STR || rt == TR::TID_STR)) {
        auto* eq_fn = get_or_declare_rt("duxrt_str_eq",
            llvm::Type::getInt32Ty(*ctx_), {ptr_type(), ptr_type()});
        Value* eq = builder_->CreateCall(eq_fn, {L, R});
        llvm::Type* i1  = llvm::Type::getInt1Ty(*ctx_);
        llvm::Type* i32 = llvm::Type::getInt32Ty(*ctx_);
        if (eq->getType() == i1) {
            // Already a boolean (i1) — just negate for !=
            return (op == "!=") ? builder_->CreateNot(eq) : eq;
        }
        // i32 result — convert to i1
        if (op == "!=")
            eq = builder_->CreateICmpEQ(eq, llvm::ConstantInt::get(i32, 0));
        else
            eq = builder_->CreateICmpNE(eq, llvm::ConstantInt::get(i32, 0));
        return eq;
    }

    // List + list via rt call
    if (op == "+" && (lt == TR::TID_LIST || rt == TR::TID_LIST)) {
        auto* concat_fn = get_or_declare_rt("duxrt_list_concat",
            ptr_type(), {ptr_type(), ptr_type()});
        return builder_->CreateCall(concat_fn, {L, R});
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
                ? str_literal("")
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

        // len() — dispatch to the correct runtime function based on type
        if (name == "len") {
            Value* arg = e.args.empty() ? nullptr : gen_expr(*e.args[0]);
            if (!arg) return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_), 0);
            TypeId arg_tid = e.args.empty() ? TR::TID_UNKNOWN : type_id_of(*e.args[0]);
            if (arg_tid == TR::TID_LIST) {
                auto* fn = get_or_declare_rt("duxrt_list_len",
                    llvm::Type::getInt64Ty(*ctx_), {ptr_type()});
                return builder_->CreateCall(fn, {arg});
            }
            // str and fallback
            auto* fn = get_or_declare_rt("duxrt_str_length",
                llvm::Type::getInt64Ty(*ctx_), {ptr_type()});
            return builder_->CreateCall(fn, {arg});
        }

        // Closure variable call — use fn_var_types_ (TypeId is from sema's registry)
        if (fn_var_types_.count(name)) {
            if (Value* closure_alloca = env_lookup(name)) {
                const ast::TypeExpr& fn_te = fn_var_types_.at(name);
                TypeId ret_id = types_.from_name(fn_te.fn_ret);
                llvm::Type* ret_ty = (ret_id == TR::TID_VOID || ret_id < 0)
                    ? llvm::Type::getVoidTy(*ctx_) : lower_type(ret_id);
                std::vector<llvm::Type*> param_tys;
                param_tys.push_back(ptr_type());
                for (const auto& pt : fn_te.fn_params)
                    param_tys.push_back(lower_type_expr(pt));
                auto* fty = llvm::FunctionType::get(ret_ty, param_tys, false);
                Value* closure_ptr = builder_->CreateLoad(ptr_type(), closure_alloca, name + ".clos");
                std::vector<llvm::Type*> hdr_fields{ptr_type()};
                auto* hdr_ty = llvm::StructType::get(*ctx_, hdr_fields);
                Value* fp_gep = builder_->CreateStructGEP(hdr_ty, closure_ptr, 0, "closure.fp.gep");
                Value* fp = builder_->CreateLoad(ptr_type(), fp_gep, "closure.fp");
                std::vector<Value*> args;
                args.push_back(closure_ptr);
                for (size_t i = 0; i < e.args.size(); ++i) {
                    Value* v = gen_expr(*e.args[i]);
                    if (i < fn_te.fn_params.size())
                        v = coerce_to_llvm_type(v, lower_type_expr(fn_te.fn_params[i]));
                    args.push_back(v);
                }
                return builder_->CreateCall(fty, fp, args,
                    ret_ty->isVoidTy() ? "" : "closure.ret");
            }
        }

        // Look up a declared function — also try current namespace prefix
        // so that intra-namespace calls (e.g. factorial calling itself when
        // compiled as mathutils__factorial) resolve correctly.
        Function* fn = mod_->getFunction(name);
        if (!fn && !current_namespace_.empty())
            fn = mod_->getFunction(mangle(current_namespace_, name));
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
        return emit_call(fn, args);
    }

    // Member call: obj.method(args)
    if (auto* mem = dynamic_cast<const ast::MemberExpr*>(e.callee.get())) {
        // Before evaluating the object expression, check if this is a namespace
        // call (e.g. math.square) by looking for a mangled function in the module.
        if (auto* id = dynamic_cast<const ast::IdentExpr*>(mem->object.get())) {
            std::string ns_mangled = mangle(id->name, mem->member);
            if (Function* ns_fn = mod_->getFunction(ns_mangled)) {
                std::vector<Value*> args;
                auto param_it = ns_fn->arg_begin();
                for (const auto& a : e.args) {
                    Value* v = gen_expr(*a);
                    if (param_it != ns_fn->arg_end())
                        v = coerce_to_llvm_type(v, (param_it++)->getType());
                    args.push_back(v);
                }
                return builder_->CreateCall(ns_fn, args);
            }
        }

        Value* obj = gen_expr(*mem->object);
        TypeId obj_tid = type_id_of(*mem->object);
        std::string cls_name = types_.name_of(obj_tid);

        // Use full class resolution (same fallback as gen_member / lvalue_of)
        cls_name = resolve_class_name(*mem->object, obj_tid);
        std::string mangled = mangle(cls_name, mem->member);
        Function* fn = mod_->getFunction(mangled);

        // Walk the inheritance chain when the method is not defined in the
        // declared class (inherited method dispatch).
        if (!fn) {
            std::string cur = cls_name;
            while (!cur.empty()) {
                auto lit = layouts_.find(cur);
                if (lit == layouts_.end()) break;
                cur = lit->second.parent_name;
                if (cur.empty()) break;
                fn = mod_->getFunction(mangle(cur, mem->member));
                if (fn) break;
            }
        }

        if (fn) {
            std::vector<Value*> args = {obj};
            auto param_it = std::next(fn->arg_begin()); // skip 'this'
            for (const auto& a : e.args) {
                Value* v = gen_expr(*a);
                if (param_it != fn->arg_end())
                    v = coerce_to_llvm_type(v, (param_it++)->getType());
                args.push_back(v);
            }
            return emit_call(fn, args);
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
    // Enum variant access: Color.Red  →  i32 constant
    if (auto* id = dynamic_cast<const ast::IdentExpr*>(e.object.get())) {
        auto enum_it = enum_types_.find(id->name);
        if (enum_it != enum_types_.end()) {
            TypeId enum_tid = enum_it->second;
            const sema::TypeInfo& ti = types_.info(enum_tid);
            for (const auto& v : ti.variants) {
                if (v.name == e.member)
                    return llvm::ConstantInt::get(
                        llvm::Type::getInt32Ty(*ctx_),
                        static_cast<uint64_t>(v.tag));
            }
            driver_.warning(e.loc, "unknown enum variant '" + e.member +
                            "' on '" + id->name + "'");
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_), 0);
        }

        // Static field access: ClassName.field  →  load from @ClassName._field global
        // Only when the identifier resolves to a class name, not a local variable.
        if (!env_lookup(id->name) && layouts_.count(id->name)) {
            std::string gname = id->name + "._" + e.member;
            if (llvm::GlobalVariable* gv = mod_->getGlobalVariable(gname, true)) {
                llvm::Type* t = alloca_type_.count(gv)
                    ? alloca_type_.at(gv) : gv->getValueType();
                return builder_->CreateLoad(t, gv, e.member);
            }
        }
    }

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

    // Operator overloading: dispatch to operator__index for user types
    {
        std::string cls = resolve_class_name(*e.object, obj_tid);
        if (!cls.empty()) {
            std::string sym = mangle(cls, "operator__index");
            if (Function* fn = mod_->getFunction(sym))
                return emit_call(fn, {obj, idx});
        }
    }

    if (obj_tid == TR::TID_LIST) {
        auto* get_fn = get_or_declare_rt("duxrt_list_get",
            ptr_type(), {ptr_type(), llvm::Type::getInt64Ty(*ctx_)});
        Value* idx64 = builder_->CreateSExt(idx, llvm::Type::getInt64Ty(*ctx_));
        return builder_->CreateCall(get_fn, {obj, idx64});
    }
    if (obj_tid == TR::TID_DICT) {
        auto* get_fn = get_or_declare_rt("duxrt_dict_get",
            ptr_type(), {ptr_type(), ptr_type()});
        // Dict keys are char*; extract from DuxStr if the index is a str.
        TypeId idx_tid = type_id_of(*e.index);
        if (idx_tid == TR::TID_STR) {
            auto* cstr_fn = get_or_declare_rt("duxrt_str_cstr", ptr_type(), {ptr_type()});
            idx = builder_->CreateCall(cstr_fn, {idx});
        }
        return builder_->CreateCall(get_fn, {obj, idx});
    }
    // String indexing — returns a new single-char DuxStr* (caller owns)
    if (obj_tid == TR::TID_STR) {
        Value* idx64 = builder_->CreateSExt(idx, llvm::Type::getInt64Ty(*ctx_));
        auto* idx_fn = get_or_declare_rt("duxrt_str_index",
            ptr_type(), {ptr_type(), llvm::Type::getInt64Ty(*ctx_)});
        return builder_->CreateCall(idx_fn, {obj, idx64});
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

    // Call constructor if present (use invoke inside try blocks so constructor
    // exceptions propagate to the surrounding landingpad).
    std::string ctor_name = mangle(cls_name, cls_name);
    Function* ctor_fn = mod_->getFunction(ctor_name);
    if (ctor_fn) {
        std::vector<Value*> args = {raw};
        for (const auto& a : e.args) args.push_back(gen_expr(*a));
        emit_call(ctor_fn, args);
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
    auto* cstr_fn = get_or_declare_rt("duxrt_str_cstr", ptr_type(), {ptr_type()});
    for (const auto& [k, v] : e.pairs) {
        Value* kv = gen_expr(*k);
        Value* vv = gen_expr(*v);
        // Dict keys are stored as char*; extract from DuxStr if needed.
        TypeId k_tid = type_id_of(*k);
        if (k_tid == TR::TID_STR)
            kv = builder_->CreateCall(cstr_fn, {kv});
        else if (!kv->getType()->isPointerTy()) {
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

// ─── Free-variable collection helpers for closures ───────────────────────────

static void collect_fv_expr(const ast::Expr* e,
                             std::unordered_set<std::string>& locals,
                             std::vector<std::string>& result,
                             std::unordered_set<std::string>& seen);

static void collect_fv_stmt(const ast::Stmt* s,
                             std::unordered_set<std::string>& locals,
                             std::vector<std::string>& result,
                             std::unordered_set<std::string>& seen) {
    if (!s) return;
    if (auto* b = dynamic_cast<const ast::BlockStmt*>(s)) {
        std::unordered_set<std::string> inner = locals;
        for (const auto& st : b->body) collect_fv_stmt(st.get(), inner, result, seen);
    } else if (auto* es = dynamic_cast<const ast::ExprStmt*>(s)) {
        collect_fv_expr(es->expr.get(), locals, result, seen);
    } else if (auto* r = dynamic_cast<const ast::ReturnStmt*>(s)) {
        if (r->value) collect_fv_expr(r->value->get(), locals, result, seen);
    } else if (auto* v = dynamic_cast<const ast::VarDeclStmt*>(s)) {
        for (const auto& [name, init] : v->decls) {
            if (init) collect_fv_expr(init.get(), locals, result, seen);
            locals.insert(name);
        }
    } else if (auto* i = dynamic_cast<const ast::IfStmt*>(s)) {
        collect_fv_expr(i->cond.get(), locals, result, seen);
        collect_fv_stmt(i->then_br.get(), locals, result, seen);
        if (i->else_br) collect_fv_stmt(i->else_br.get(), locals, result, seen);
    } else if (auto* w = dynamic_cast<const ast::WhileStmt*>(s)) {
        collect_fv_expr(w->cond.get(), locals, result, seen);
        collect_fv_stmt(w->body.get(), locals, result, seen);
    }
}

static void collect_fv_expr(const ast::Expr* e,
                             std::unordered_set<std::string>& locals,
                             std::vector<std::string>& result,
                             std::unordered_set<std::string>& seen) {
    if (!e) return;
    if (auto* id = dynamic_cast<const ast::IdentExpr*>(e)) {
        if (!locals.count(id->name) && !seen.count(id->name)) {
            result.push_back(id->name);
            seen.insert(id->name);
        }
    } else if (auto* bin = dynamic_cast<const ast::BinaryExpr*>(e)) {
        collect_fv_expr(bin->left.get(), locals, result, seen);
        collect_fv_expr(bin->right.get(), locals, result, seen);
    } else if (auto* un = dynamic_cast<const ast::UnaryExpr*>(e)) {
        collect_fv_expr(un->operand.get(), locals, result, seen);
    } else if (auto* call = dynamic_cast<const ast::CallExpr*>(e)) {
        collect_fv_expr(call->callee.get(), locals, result, seen);
        for (const auto& a : call->args) collect_fv_expr(a.get(), locals, result, seen);
    } else if (auto* mem = dynamic_cast<const ast::MemberExpr*>(e)) {
        collect_fv_expr(mem->object.get(), locals, result, seen);
    } else if (auto* idx = dynamic_cast<const ast::IndexExpr*>(e)) {
        collect_fv_expr(idx->object.get(), locals, result, seen);
        collect_fv_expr(idx->index.get(), locals, result, seen);
    } else if (auto* a = dynamic_cast<const ast::AssignExpr*>(e)) {
        collect_fv_expr(a->target.get(), locals, result, seen);
        collect_fv_expr(a->value.get(), locals, result, seen);
    } else if (auto* n = dynamic_cast<const ast::NewExpr*>(e)) {
        for (const auto& arg : n->args) collect_fv_expr(arg.get(), locals, result, seen);
    }
}

Value* Codegen::gen_lambda(const ast::LambdaExpr& e) {
    // Collect captures: free vars in the body not in param list
    std::unordered_set<std::string> param_set;
    for (const auto& p : e.params) param_set.insert(p.name);
    std::vector<std::string> free_vars;
    std::unordered_set<std::string> seen_fv;
    collect_fv_stmt(e.body.get(), param_set, free_vars, seen_fv);

    // Filter to only actual outer-scope variables
    std::vector<std::string> captures;
    for (const std::string& v : free_vars)
        if (env_lookup(v)) captures.push_back(v);
    const_cast<ast::LambdaExpr&>(e).captures = captures;

    // Closure struct type: {ptr fn_ptr, ptr cap0_ref, ...}
    std::vector<llvm::Type*> env_fields;
    env_fields.push_back(ptr_type());
    for (size_t i = 0; i < captures.size(); ++i) env_fields.push_back(ptr_type());
    llvm::StructType* closure_ty = llvm::StructType::get(*ctx_, env_fields);

    // Determine return type by scanning body for return statements.
    // We cannot use e.type_id here because it references sema's TypeRegistry,
    // which is separate from the codegen TypeRegistry.
    TypeId ret_tid = TR::TID_VOID;
    if (auto* blk = dynamic_cast<const ast::BlockStmt*>(e.body.get())) {
        for (const auto& s : blk->body) {
            if (auto* rs = dynamic_cast<const ast::ReturnStmt*>(s.get())) {
                if (rs->value) {
                    TypeId rt = type_id_of(**rs->value);
                    if (rt >= 0 && rt != TR::TID_VOID) { ret_tid = rt; break; }
                }
            }
        }
    }
    if (ret_tid < 0) ret_tid = TR::TID_VOID;

    // Build lambda function type: (ptr env, param_types...) -> ret
    std::vector<llvm::Type*> lam_params;
    lam_params.push_back(ptr_type());
    for (const auto& p : e.params) lam_params.push_back(lower_type_expr(p.type));
    llvm::Type* ret_llvm = lower_type(ret_tid);
    auto* lam_fty = llvm::FunctionType::get(ret_llvm, lam_params, false);
    std::string lam_name = "__lambda_" + std::to_string(lambda_counter_++);
    auto* lam_fn = Function::Create(lam_fty, Function::InternalLinkage, lam_name, *mod_);

    // Set personality function
    Function* personality_fn = mod_->getFunction("__gxx_personality_v0");
    if (!personality_fn) {
        auto* pft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*ctx_), true);
        personality_fn = Function::Create(pft, Function::ExternalLinkage,
                                          "__gxx_personality_v0", *mod_);
    }
    lam_fn->setPersonalityFn(personality_fn);

    // Save outer codegen state
    auto* outer_bb      = builder_->GetInsertBlock();
    auto  outer_env     = env_;
    auto  outer_str     = str_scopes_;
    auto  outer_cleanup = cleanup_scopes_;
    auto  outer_vcls    = var_class_;
    std::string outer_class  = current_class_;
    TypeId      outer_ret    = current_ret_type_;
    auto        outer_lp     = lp_stack_;
    llvm::Function* outer_fn = current_fn_;

    // Set up lambda state
    current_fn_       = lam_fn;
    current_ret_type_ = ret_tid;
    current_class_    = "";
    lp_stack_.clear();
    env_.clear(); str_scopes_.clear(); cleanup_scopes_.clear(); var_class_.clear();

    auto* lam_entry = BasicBlock::Create(*ctx_, "entry", lam_fn);
    builder_->SetInsertPoint(lam_entry);
    env_push();

    // Bind env arg and load captures
    auto arg_it = lam_fn->arg_begin();
    Value* env_arg = &*arg_it++;
    env_arg->setName("env");

    for (size_t i = 0; i < captures.size(); ++i) {
        Value* fgep = builder_->CreateStructGEP(
            closure_ty, env_arg, (unsigned)(i + 1), captures[i] + ".cap.gep");
        Value* cap_alloca = builder_->CreateLoad(ptr_type(), fgep, captures[i] + ".cap");
        env_.back()[captures[i]] = cap_alloca;
        // Copy alloca_type if we can find the outer alloca
        for (auto it = outer_env.rbegin(); it != outer_env.rend(); ++it) {
            auto jt = it->find(captures[i]);
            if (jt != it->end() && alloca_type_.count(jt->second)) {
                alloca_type_[cap_alloca] = alloca_type_[jt->second];
                break;
            }
        }
    }

    // Bind params
    for (const auto& p : e.params) {
        llvm::Type* pt = lower_type_expr(p.type);
        Value* alloca = make_alloca(pt, p.name);
        builder_->CreateStore(&*arg_it, alloca);
        env_.back()[p.name] = alloca;
        alloca_type_[alloca] = pt;
        ++arg_it;
    }

    // Register fn-typed lambda params for closure dispatch
    for (const auto& p : e.params)
        if (p.type.name == "__fn") fn_var_types_[p.name] = p.type;

    // Generate body
    if (auto* blk = dynamic_cast<const ast::BlockStmt*>(e.body.get()))
        gen_stmts(blk->body);

    // Add terminator if needed
    if (!builder_->GetInsertBlock()->getTerminator()) {
        emit_all_scope_cleanups();
        emit_all_str_releases();
        if (ret_llvm->isVoidTy()) builder_->CreateRetVoid();
        else builder_->CreateRet(llvm::Constant::getNullValue(ret_llvm));
    }
    // Pop lambda scope stacks (without emitting cleanup again)
    if (!cleanup_scopes_.empty()) cleanup_scopes_.pop_back();
    if (!str_scopes_.empty())     str_scopes_.pop_back();
    if (!env_.empty())            env_.pop_back();

    // Restore outer state
    builder_->SetInsertPoint(outer_bb);
    env_       = std::move(outer_env);
    str_scopes_= std::move(outer_str);
    cleanup_scopes_ = std::move(outer_cleanup);
    var_class_ = std::move(outer_vcls);
    current_class_    = outer_class;
    current_ret_type_ = outer_ret;
    lp_stack_  = std::move(outer_lp);
    current_fn_       = outer_fn;

    // Allocate closure struct on heap
    llvm::DataLayout dl(mod_.get());
    uint64_t sz = dl.getTypeAllocSize(closure_ty);
    Value* sz_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx_), sz);
    Value* raw = rt_malloc(sz_val);

    // Store fn pointer at field 0
    Value* fp_field = builder_->CreateStructGEP(closure_ty, raw, 0, "closure.fp.slot");
    builder_->CreateStore(lam_fn, fp_field);

    // Store capture pointers (ptr to each captured alloca)
    for (size_t i = 0; i < captures.size(); ++i) {
        Value* cap_field = builder_->CreateStructGEP(
            closure_ty, raw, (unsigned)(i + 1), captures[i] + ".cap.slot");
        Value* cap_alloca = env_lookup(captures[i]);
        if (cap_alloca) builder_->CreateStore(cap_alloca, cap_field);
    }

    return raw;
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
        // Static field assignment: ClassName.field = val  →  @ClassName._field global
        if (auto* id = dynamic_cast<const ast::IdentExpr*>(mem->object.get())) {
            if (!env_lookup(id->name) && layouts_.count(id->name)) {
                std::string gname = id->name + "._" + mem->member;
                if (llvm::GlobalVariable* gv = mod_->getGlobalVariable(gname, true))
                    return gv;
            }
        }
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
        (void)obj; (void)i;
        if (obj_tid == TR::TID_STR)
            return nullptr; // str characters are not mutable lvalues
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

void Codegen::env_push() {
    env_.emplace_back();
    str_scopes_.emplace_back();
    cleanup_scopes_.emplace_back();
}

void Codegen::env_pop() {
    auto* bb = builder_->GetInsertBlock();
    bool live = bb && !bb->getTerminator();

    // Run RAII dtors and defer blocks in LIFO order.
    if (!cleanup_scopes_.empty()) {
        if (live) {
            auto& scope = cleanup_scopes_.back();
            for (int i = static_cast<int>(scope.size()) - 1; i >= 0; --i)
                emit_scope_cleanup(scope[static_cast<std::size_t>(i)]);
        }
        cleanup_scopes_.pop_back();
    }

    // Emit releases for str variables going out of scope.
    if (!str_scopes_.empty()) {
        if (live) {
            for (Value* alloca : str_scopes_.back()) {
                Value* v = builder_->CreateLoad(ptr_type(), alloca, "str.rel");
                emit_str_release(v);
            }
        }
        str_scopes_.pop_back();
    }
    if (!env_.empty()) env_.pop_back();
}

void Codegen::env_define(const std::string& name, Value* alloca, TypeId tid) {
    if (!env_.empty()) env_.back()[name] = alloca;
    if (tid == TR::TID_STR && !str_scopes_.empty()) {
        str_scopes_.back().push_back(alloca);
        return;
    }
    // Register class instances for RAII cleanup (dtor call if present, then free).
    // Uses layouts_ as the condition so trivial-dtor classes (no ___dtor symbol)
    // are still freed at scope exit via the free-only path in emit_dtor.
    if (tid > TR::TID_NULL && !cleanup_scopes_.empty()) {
        const std::string& cls_name = types_.name_of(tid);
        if (!cls_name.empty() && layouts_.count(cls_name))
            cleanup_scopes_.back().push_back({alloca, cls_name, nullptr});
    }
}

void Codegen::emit_dtor(Value* alloca, const std::string& class_name) {
    auto* bb = builder_->GetInsertBlock();
    if (!bb || bb->getTerminator()) return;

    auto* free_fn = get_or_declare_rt("free", llvm::Type::getVoidTy(*ctx_), {ptr_type()});
    Value* null_v = llvm::ConstantPointerNull::get(
                        llvm::cast<llvm::PointerType>(ptr_type()));

    std::string dtor_sym = class_name + "___dtor";
    Function* dtor_fn = mod_->getFunction(dtor_sym);

    if (!dtor_fn) {
        // Trivial dtor (empty body or none declared): null-guard + free only.
        // The null-guard is kept so that explicit delete + RAII scope exit
        // doesn't double-free (gen_delete stores null after free).
        Value* obj_ptr = builder_->CreateLoad(ptr_type(), alloca, "dtor.obj");
        Function* fn   = bb->getParent();
        auto* call_bb  = BasicBlock::Create(*ctx_, "dtor.call", fn);
        auto* end_bb   = BasicBlock::Create(*ctx_, "dtor.end",  fn);
        Value* is_null = builder_->CreateICmpEQ(obj_ptr, null_v, "dtor.isnull");
        builder_->CreateCondBr(is_null, end_bb, call_bb);
        builder_->SetInsertPoint(call_bb);
        builder_->CreateCall(free_fn, {obj_ptr});
        builder_->CreateStore(null_v, alloca);
        builder_->CreateBr(end_bb);
        builder_->SetInsertPoint(end_bb);
        return;
    }

    // Non-trivial dtor: null-guard + user dtor call + free.
    Value* obj_ptr = builder_->CreateLoad(ptr_type(), alloca, "dtor.obj");
    Function* fn   = bb->getParent();
    auto* call_bb  = BasicBlock::Create(*ctx_, "dtor.call", fn);
    auto* end_bb   = BasicBlock::Create(*ctx_, "dtor.end",  fn);
    Value* is_null = builder_->CreateICmpEQ(obj_ptr, null_v, "dtor.isnull");
    builder_->CreateCondBr(is_null, end_bb, call_bb);

    builder_->SetInsertPoint(call_bb);
    emit_call(dtor_fn, {obj_ptr});
    builder_->CreateCall(free_fn, {obj_ptr});
    // Null out the slot so a second emit_dtor call is a no-op.
    builder_->CreateStore(null_v, alloca);
    builder_->CreateBr(end_bb);

    builder_->SetInsertPoint(end_bb);
}

void Codegen::emit_scope_cleanup(const ScopeCleanup& c) {
    auto* bb = builder_->GetInsertBlock();
    if (!bb || bb->getTerminator()) return;
    if (c.fn_to_call) {
        emit_call(c.fn_to_call, {});
    } else if (c.defer_body) {
        gen_stmts(*c.defer_body);
    } else {
        emit_dtor(c.alloca, c.class_name);
    }
}

void Codegen::emit_all_scope_cleanups() {
    auto* bb = builder_->GetInsertBlock();
    if (!bb || bb->getTerminator()) return;
    for (int i = static_cast<int>(cleanup_scopes_.size()) - 1; i >= 0; --i) {
        auto& scope = cleanup_scopes_[static_cast<std::size_t>(i)];
        for (int j = static_cast<int>(scope.size()) - 1; j >= 0; --j)
            emit_scope_cleanup(scope[static_cast<std::size_t>(j)]);
    }
}

void Codegen::gen_defer(const ast::DeferStmt& s) {
    if (cleanup_scopes_.empty()) return;
    // Register the defer block; it will be emitted in LIFO order at scope exit.
    cleanup_scopes_.back().push_back({nullptr, {}, &s.body});
}

void Codegen::gen_throw(const ast::ThrowStmt& s) {
    Value* exc = gen_expr(*s.expr);

    // Allocate exception storage (sizeof(void*) = 8 bytes on LP64)
    auto* alloc_exc = get_or_declare_rt("__cxa_allocate_exception",
                          ptr_type(), {llvm::Type::getInt64Ty(*ctx_)});
    Value* storage = builder_->CreateCall(alloc_exc,
                         {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*ctx_), 8)},
                         "exc.storage");
    builder_->CreateStore(exc, storage);

    // _ZTIPv = typeinfo for void* from libstdc++; used as a valid non-null
    // type_info for __cxa_throw.  Since landingpads use catch ptr null
    // (catch-all), the type is never matched against.
    llvm::GlobalVariable* tinfo_gv = llvm::cast<llvm::GlobalVariable>(
        mod_->getOrInsertGlobal("_ZTIPv", ptr_type()));
    tinfo_gv->setExternallyInitialized(true);

    Function* cxa_throw = get_or_declare_rt("__cxa_throw",
                              llvm::Type::getVoidTy(*ctx_),
                              {ptr_type(), ptr_type(), ptr_type()});
    cxa_throw->setDoesNotReturn();
    llvm::Value* null_dest =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_type()));
    std::vector<Value*> throw_args = {storage, tinfo_gv, null_dest};

    if (!lp_stack_.empty()) {
        // Inside a try block: use invoke so the EH table covers this site.
        // The "normal" successor is unreachable because __cxa_throw never returns.
        Function* fn = builder_->GetInsertBlock()->getParent();
        auto* unreach_bb = BasicBlock::Create(*ctx_, "throw.cont", fn);
        builder_->CreateInvoke(cxa_throw, unreach_bb, lp_stack_.back(), throw_args);
        builder_->SetInsertPoint(unreach_bb);
    } else {
        builder_->CreateCall(cxa_throw, throw_args);
    }
    builder_->CreateUnreachable();
}

// Emit a call or invoke depending on whether we're inside a try block.
Value* Codegen::emit_call(llvm::FunctionCallee callee,
                          llvm::ArrayRef<Value*> args,
                          const std::string& name) {
    if (lp_stack_.empty())
        return builder_->CreateCall(callee, args, name);

    // Inside a try block: emit invoke so exceptions unwind to the landing pad.
    Function* fn = builder_->GetInsertBlock()->getParent();
    auto* cont_bb = BasicBlock::Create(*ctx_, "invoke.cont", fn);
    auto* inst = builder_->CreateInvoke(callee, cont_bb, lp_stack_.back(), args, name);
    builder_->SetInsertPoint(cont_bb);
    return inst;
}

Value* Codegen::make_alloca(llvm::Type* t, const std::string& name) {
    // Always create allocas in the function entry block so they dominate all
    // uses — including landing pad blocks that may be unreachable via invoke
    // continuations.
    llvm::BasicBlock* cur = builder_->GetInsertBlock();
    Function* fn = cur ? cur->getParent() : nullptr;
    if (fn && !fn->empty()) {
        llvm::BasicBlock& entry = fn->getEntryBlock();
        llvm::IRBuilder<> eb(&entry, entry.begin());
        Value* a = eb.CreateAlloca(t, nullptr, name);
        alloca_type_[a] = t;
        return a;
    }
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

// ─── String reference counting ───────────────────────────────────────────────

Value* Codegen::str_literal(const std::string& s) {
    auto it = str_lit_cache_.find(s);
    if (it != str_lit_cache_.end()) return it->second;

    std::string idx = std::to_string(str_lit_cache_.size());

    // Build one global that mirrors the hybrid DuxStr layout:
    //   @.duxstr.N = private constant { i32, i32, ptr, [len+1 x i8] }
    //                                 { -1,  len, null, c"...\00"    }
    //
    // Offsets on LP64 (matching sizeof(DuxStr) == 16 with int32_t len):
    //   0: i32 refcount   4: i32 len   8: ptr ext=null   16: data[]
    //
    // ext=null tells duxrt_str_cstr to return &s->data[0], so all C
    // runtime functions see the embedded bytes at offset 16 correctly.
    auto* data_arr = llvm::ConstantDataArray::getString(*ctx_, s, /*AddNull=*/true);
    auto* str_ty   = llvm::StructType::get(*ctx_, {
        llvm::Type::getInt32Ty(*ctx_),   // refcount  (offset  0)
        llvm::Type::getInt32Ty(*ctx_),   // len       (offset  4)
        ptr_type(),                       // ext=null  (offset  8)
        data_arr->getType()              // data[]    (offset 16)
    });
    auto* str_init = llvm::ConstantStruct::get(str_ty, {
        llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(*ctx_), -1),
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx_), (uint32_t)s.size()),
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_type())),
        data_arr
    });
    auto* str_gv = new llvm::GlobalVariable(*mod_, str_ty,
        /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
        str_init, ".duxstr." + idx);

    str_lit_cache_[s] = str_gv;
    return str_gv;
}

Value* Codegen::emit_str_retain(Value* v) {
    auto* fn = get_or_declare_rt("duxrt_str_retain", ptr_type(), {ptr_type()});
    return builder_->CreateCall(fn, {v});
}

void Codegen::emit_str_release(Value* v) {
    auto* fn = get_or_declare_rt("duxrt_str_release",
        llvm::Type::getVoidTy(*ctx_), {ptr_type()});
    builder_->CreateCall(fn, {v});
}

Value* Codegen::maybe_retain_str(Value* v) {
    if (!v) return v;
    // Consuming convention: call results (refcount=1) and immortal globals need no retain.
    if (llvm::isa<llvm::CallInst>(v) || llvm::isa<llvm::GlobalVariable>(v)) return v;
    return emit_str_retain(v);
}

void Codegen::emit_all_str_releases() {
    auto* bb = builder_->GetInsertBlock();
    if (!bb || bb->getTerminator()) return;
    for (int i = (int)str_scopes_.size() - 1; i >= 0; --i) {
        for (Value* alloca : str_scopes_[(size_t)i]) {
            Value* v = builder_->CreateLoad(ptr_type(), alloca, "str.rel");
            emit_str_release(v);
        }
    }
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
    if (t == TR::TID_BOOL) {
        // Print "true" or "false".  CreateSExt on i1 would give -1 for true.
        llvm::Type* i1 = llvm::Type::getInt1Ty(*ctx_);
        Value* cond = (v->getType() == i1)
            ? v : builder_->CreateTrunc(v, i1);
        Value* msg = builder_->CreateSelect(cond, str_literal("true"), str_literal("false"));
        auto* sfn = get_or_declare_rt("duxrt_println_str",
            llvm::Type::getVoidTy(*ctx_), {ptr_type()});
        return builder_->CreateCall(sfn, {msg});
    }
    if (t == TR::TID_INT || t == TR::TID_LONG) {
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
