#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <unordered_set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#  include <mach-o/dyld.h>   // _NSGetExecutablePath
#endif

#include "ast/printer.hpp"
#include "driver/driver.hpp"
#include "sema/generics.hpp"
#include "sema/sema.hpp"
#include "codegen/codegen.hpp"
#include "repl/repl.hpp"

#ifndef DUX_VERSION
#define DUX_VERSION "0.1.0-dev"
#endif

// Paths and build metadata baked in at build time by CMake
#ifndef DUXRT_LIB_PATH
#define DUXRT_LIB_PATH ""
#endif
#ifndef DUXRT_BC_PATH
#define DUXRT_BC_PATH ""
#endif
#ifndef DUXRT_INCLUDE_DIR
#define DUXRT_INCLUDE_DIR ""
#endif
#ifndef DUXSTDLIB_DIR
#define DUXSTDLIB_DIR ""
#endif
#ifndef DUX_BUILD_TYPE
#define DUX_BUILD_TYPE "unknown"
#endif
#ifndef DUX_LLVM_VERSION
#define DUX_LLVM_VERSION "unknown"
#endif
#ifndef DUX_COMPILER_ID
#define DUX_COMPILER_ID "unknown"
#endif
#ifndef DUX_COMPILER_VERSION
#define DUX_COMPILER_VERSION ""
#endif

namespace {

// ── Runtime resource path resolution ──────────────────────────────────────────
// Installed layout:
//   <prefix>/bin/dux               ← this binary
//   <prefix>/lib/libduxrt.a        ← runtime static library
//   <prefix>/share/dux/stdlib/     ← standard library .dux sources
//
// Compile-time paths (DUXRT_LIB_PATH, DUXSTDLIB_DIR) are the fallback for
// developer / CI builds where the install layout does not apply.

static std::filesystem::path exe_real_path() {
    std::error_code ec;
#ifdef __linux__
    auto p = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec) return p;
#elif defined(__APPLE__)
    uint32_t sz = 0;
    _NSGetExecutablePath(nullptr, &sz);
    std::string buf(sz, '\0');
    if (_NSGetExecutablePath(buf.data(), &sz) == 0) {
        auto p = std::filesystem::canonical(buf, ec);
        if (!ec) return p;
    }
#endif
    return {};
}

// <prefix>/bin/dux → <prefix>   (returns empty on failure)
static std::filesystem::path install_prefix() {
    auto exe = exe_real_path();
    if (exe.empty()) return {};
    return exe.parent_path().parent_path();
}

// libduxrt.a: prefer <prefix>/lib; fall back to compile-time DUXRT_LIB_PATH.
static std::string resolve_duxrt_lib() {
    auto prefix = install_prefix();
    if (!prefix.empty()) {
        auto c = prefix / "lib" / "libduxrt.a";
        if (std::filesystem::exists(c)) return c.string();
    }
    return DUXRT_LIB_PATH;
}

// stdlib .dux sources: prefer <prefix>/share/dux/stdlib; fall back to
// compile-time DUXSTDLIB_DIR.
static std::string resolve_stdlib_dir() {
    auto prefix = install_prefix();
    if (!prefix.empty()) {
        auto c = prefix / "share" / "dux" / "stdlib";
        if (std::filesystem::exists(c)) return c.string();
    }
    return DUXSTDLIB_DIR;
}

struct Options {
    std::string  input;
    std::string  output;
    bool         dump_ast{false};
    bool         check{false};
    bool         emit_ir{false};
    bool         emit_obj{false};
    bool         compile{false};   // emit obj + link → executable
    bool         debug_info{false}; // -g: emit DWARF
    int          opt_level{0};      // -O0/-O1/-O2/-O3
    bool         trace_lex{false};
    bool         trace_parse{false};
    bool         repl{false};
    bool         help{false};
    bool         version{false};
};

void print_build_info() {
    std::cout <<
        "dux " DUX_VERSION " (" DUX_TARGET_TRIPLE ")\n"
        "  Build   : " DUX_BUILD_TYPE "\n"
        "  LLVM    : " DUX_LLVM_VERSION "\n"
        "  Compiler: " DUX_COMPILER_ID " " DUX_COMPILER_VERSION "\n";
}

void usage(std::string_view prog) {
    std::cerr <<
        "Usage: " << prog << " [options] [file]\n"
        "\n"
        "Options:\n"
        "  --dump-ast      Print the parsed AST to stdout\n"
        "  --check         Run semantic analysis and report errors\n"
        "  --emit-ir       Emit LLVM IR (to -o path or stdout)\n"
        "  --emit-obj      Emit native object file (requires -o path)\n"
        "  --compile       Compile to a runnable native executable\n"
        "  --repl          Start an interactive REPL session\n"
        "  -o <path>       Output path\n"
        "  -O0/-O1/-O2/-O3 Optimisation level (default -O0)\n"
        "  -g              Emit DWARF debug information\n"
        "  --trace-lex     Enable flex debug output\n"
        "  --trace-parse   Enable bison debug output\n"
        "  --version       Print version and exit\n"
        "  -h, --help      Show this message\n"
        "\n"
        "Examples:\n"
        "  " << prog << " hello.dux --compile      Compile to executable\n"
        "  " << prog << " hello.dux --check         Type-check only\n"
        "  " << prog << " hello.dux --emit-ir       Print LLVM IR\n"
        "  " << prog << " --repl                    Interactive REPL\n"
        "\n"
        "Reads from stdin when no file is given (non-interactive only).\n";
}

Options parse_args(std::span<char*> args) {
    Options opts;
    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view a = args[i];
        if      (a == "--dump-ast")            { opts.dump_ast    = true; }
        else if (a == "--check")               { opts.check       = true; }
        else if (a == "--emit-ir")             { opts.emit_ir     = true; }
        else if (a == "--emit-obj")            { opts.emit_obj    = true; }
        else if (a == "--compile")             { opts.compile     = true; }
        else if (a == "--repl")                { opts.repl        = true; }
        else if (a == "-g")                    { opts.debug_info  = true; }
        else if (a == "-O0")                   { opts.opt_level   = 0; }
        else if (a == "-O1")                   { opts.opt_level   = 1; }
        else if (a == "-O2")                   { opts.opt_level   = 2; }
        else if (a == "-O3")                   { opts.opt_level   = 3; }
        else if (a == "--trace-lex")           { opts.trace_lex   = true; }
        else if (a == "--trace-parse")         { opts.trace_parse = true; }
        else if (a == "--version")             { opts.version     = true; }
        else if (a == "-h" || a == "--help")   { opts.help        = true; }
        else if (a == "-o") {
            if (i + 1 < args.size()) opts.output = args[++i];
        } else if (a.starts_with("-o")) {
            opts.output = std::string(a.substr(2));
        } else if (a.starts_with('-')) {
            std::cerr << "dux: unknown option: " << a << '\n';
            opts.help = true;
        } else {
            opts.input = std::string(a);
        }
    }
    return opts;
}

// Invoke system linker to link object file into executable.
// duxrt_lib: runtime-resolved path to libduxrt.a (may differ from compile-time
// DUXRT_LIB_PATH when the binary is installed on another machine).
bool link_executable(const std::string& obj_path, const std::string& out_path,
                     const std::string& duxrt_lib) {
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("dux: fork");
        return false;
    }
    if (pid == 0) {
        // child process: exec cc directly (no shell involved)
        //
        // C++ runtime library selection:
        //   macOS  — Apple Clang ships libc++, not libstdc++.  macOS 10.15+
        //            removed libstdc++ entirely; -lstdc++ would fail at link time.
        //   Linux  — GCC / Clang both ship libstdc++ by default; use that.
        //
        // -lpthread:  no-op on macOS (pthreads are part of libSystem) but harmless.
        //
        // OpenSSL: use the absolute paths baked in at build time when they still
        // exist on this machine (developer / CI builds).  For installed binaries the
        // baked-in CI paths won't exist, so fall back to the system -lssl/-lcrypto.
#ifdef __APPLE__
        const char* cxx_rt = "-lc++";      // Apple Clang / libc++
#else
        const char* cxx_rt = "-lstdc++";   // GCC / Clang on Linux / libstdc++
#endif
        const std::string baked_ssl    = DUXRT_OPENSSL_SSL;
        const std::string baked_crypto = DUXRT_OPENSSL_CRYPTO;
        std::string ssl_link =
            (!baked_ssl.empty()    && std::filesystem::exists(baked_ssl))
            ? baked_ssl    : "-lssl";
        std::string crypto_link =
            (!baked_crypto.empty() && std::filesystem::exists(baked_crypto))
            ? baked_crypto : "-lcrypto";

        const char* argv[] = {
            "cc",
            obj_path.c_str(),
            duxrt_lib.c_str(),
            "-lm",
            cxx_rt,
            "-lpthread",
            ssl_link.c_str(),
            crypto_link.c_str(),
            "-o", out_path.c_str(),
            nullptr
        };
        execvp("cc", const_cast<char* const*>(argv));
        std::perror("dux: execvp cc");
        _exit(1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "dux: linker failed\n";
        return false;
    }
    return true;
}

// Recursively load .dux stdlib files for each import in `prog`, prepending
// their declarations to prog->decls so codegen sees them first.
// `loaded` tracks which module paths have already been loaded (avoids cycles).
// `stdlib_dir` is the runtime-resolved standard library directory.
void merge_stdlib_imports(dux::ast::Program* prog,
                          std::unordered_set<std::string>& loaded,
                          const std::string& stdlib_dir) {
    if (stdlib_dir.empty()) return;

    // Collect import paths present in this program's decls (snapshot to avoid
    // iterator invalidation as we prepend).
    std::vector<std::string> import_paths;
    for (const auto& dp : prog->decls) {
        if (auto* imp = dynamic_cast<const dux::ast::ImportDecl*>(dp.get())) {
            if (!imp->path.empty())
                import_paths.push_back(imp->path);
        }
    }

    for (const std::string& path : import_paths) {
        if (loaded.count(path)) continue;
        loaded.insert(path);

        // Convert dotted path to file path: "io.path" → "io/path.dux"
        std::string rel = path;
        for (char& c : rel)
            if (c == '.') c = '/';
        std::filesystem::path fpath = std::filesystem::path(stdlib_dir) / (rel + ".dux");
        if (!std::filesystem::exists(fpath)) continue;

        Driver sub;
        sub.stdlib_dir = stdlib_dir;   // propagate resolved dir to recursive loads
        if (sub.parse(fpath.string()) != 0 || !sub.result) continue;

        // Recursively handle imports inside the stdlib file first
        merge_stdlib_imports(sub.result.get(), loaded, stdlib_dir);

        // Prepend the stdlib file's decls to the main program
        dux::ast::DeclList prefix;
        prefix.reserve(sub.result->decls.size());
        for (auto& d : sub.result->decls)
            prefix.push_back(std::move(d));
        prefix.reserve(prefix.size() + prog->decls.size());
        for (auto& d : prog->decls)
            prefix.push_back(std::move(d));
        prog->decls = std::move(prefix);
    }
}

} // namespace

int main(int argc, char** argv) {
    auto opts = parse_args(std::span(argv, static_cast<std::size_t>(argc)));

    if (opts.version) {
        print_build_info();
        return EXIT_SUCCESS;
    }

    if (opts.help) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (opts.repl) {
        dux::Repl repl(argv[0]);
        repl.run();
        return EXIT_SUCCESS;
    }

    // If no input file was given and stdin is an interactive terminal, the user
    // almost certainly forgot to pass a file or flag — show help instead of
    // silently blocking waiting for input.  Piped / redirected stdin still works
    // (e.g. `echo 'print("hi")' | dux` or `dux < script.dux`).
    if (opts.input.empty() && isatty(STDIN_FILENO)) {
        print_build_info();
        std::cout << "\n";
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Resolve resource paths once: prefer installed layout, fall back to build tree.
    const std::string duxrt_lib  = resolve_duxrt_lib();
    const std::string stdlib_dir = resolve_stdlib_dir();

    Driver driver;
    driver.trace_scanning = opts.trace_lex;
    driver.trace_parsing  = opts.trace_parse;
    driver.stdlib_dir     = stdlib_dir;   // used by Driver::resolve_imports

    int rc = driver.parse(opts.input);
    if (rc != 0) return EXIT_FAILURE;

    if (!driver.result) return EXIT_FAILURE;

    // Load stdlib .dux files for any imports in the program
    {
        std::unordered_set<std::string> loaded;
        merge_stdlib_imports(driver.result.get(), loaded, stdlib_dir);
    }

    // Expand generic class/function instantiations before sema
    dux::sema::expand_generics(*driver.result, driver);

    // Always run sema when codegen / compile is requested
    bool need_sema = opts.check || opts.emit_ir || opts.emit_obj || opts.compile;
    if (need_sema) {
        dux::sema::Sema sema(driver);
        sema.run(*driver.result);
        if (sema.error_count() > 0) rc = 1;
    }

    if (rc != 0) return EXIT_FAILURE;

    if (opts.emit_ir || opts.emit_obj || opts.compile) {
        std::string mod_name = "dux_module";
        if (!opts.input.empty()) {
            std::filesystem::path p(opts.input);
            mod_name = p.stem().string();
        }

        dux::codegen::Codegen cg(driver);
        cg.set_opt_level(opts.opt_level);
        cg.set_debug(opts.debug_info);
        if (!cg.run(*driver.result, mod_name)) return EXIT_FAILURE;

        if (opts.emit_ir) {
            std::string out = opts.output.empty() ? "-" : opts.output;
            if (!cg.emit_ir(out)) return EXIT_FAILURE;
        }

        if (opts.emit_obj) {
            if (opts.output.empty()) {
                std::cerr << "dux: --emit-obj requires -o <path>\n";
                return EXIT_FAILURE;
            }
            if (!cg.emit_object(opts.output)) return EXIT_FAILURE;
        }

        if (opts.compile) {
            // Derive output name from input stem if not given
            std::string exe_out = opts.output;
            if (exe_out.empty() && !opts.input.empty()) {
                std::filesystem::path p(opts.input);
                exe_out = p.stem().string();
            }
            if (exe_out.empty()) exe_out = "a.out";

            // Emit object to a temp file, then link
            std::filesystem::path tmp_obj = std::filesystem::temp_directory_path()
                / (mod_name + ".o");
            if (!cg.emit_object(tmp_obj.string())) return EXIT_FAILURE;
            if (!link_executable(tmp_obj.string(), exe_out, duxrt_lib)) return EXIT_FAILURE;
            std::filesystem::remove(tmp_obj);
        }
    }

    if (opts.dump_ast && driver.error_count() == 0) {
        dux::ast::Printer printer(std::cout);
        printer.print(*driver.result);
    }

    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
