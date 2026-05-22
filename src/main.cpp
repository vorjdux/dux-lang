#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <unordered_set>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include "ast/printer.hpp"
#include "driver/driver.hpp"
#include "sema/generics.hpp"
#include "sema/sema.hpp"
#include "codegen/codegen.hpp"

#ifndef DUX_VERSION
#define DUX_VERSION "0.1.0-dev"
#endif

// Paths baked in at build time by CMake
#ifndef DUXRT_LIB_PATH
#define DUXRT_LIB_PATH ""
#endif
#ifndef DUXRT_BC_PATH
#define DUXRT_BC_PATH ""
#endif
#ifndef DUXRT_INCLUDE_DIR
#define DUXRT_INCLUDE_DIR ""
#endif

namespace {

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
    bool         help{false};
    bool         version{false};
};

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
        "  -o <path>       Output path\n"
        "  -O0/-O1/-O2/-O3 Optimisation level (default -O0)\n"
        "  -g              Emit DWARF debug information\n"
        "  --trace-lex     Enable flex debug output\n"
        "  --trace-parse   Enable bison debug output\n"
        "  --version       Print version and exit\n"
        "  -h, --help      Show this message\n"
        "\n"
        "If no file is given, reads from stdin.\n";
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
bool link_executable(const std::string& obj_path, const std::string& out_path) {
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("dux: fork");
        return false;
    }
    if (pid == 0) {
        // child process: exec cc directly (no shell involved)
        const char* argv[] = {
            "cc",
            obj_path.c_str(),
            DUXRT_LIB_PATH,
            "-lm",
            "-lstdc++",  // C++ EH ABI (__gxx_personality_v0, __cxa_*)
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

} // namespace

int main(int argc, char** argv) {
    auto opts = parse_args(std::span(argv, static_cast<std::size_t>(argc)));

    if (opts.version) {
        std::cout << "dux " DUX_VERSION "\n";
        return EXIT_SUCCESS;
    }

    if (opts.help) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    Driver driver;
    driver.trace_scanning = opts.trace_lex;
    driver.trace_parsing  = opts.trace_parse;

    int rc = driver.parse(opts.input);
    if (rc != 0) return EXIT_FAILURE;

    if (!driver.result) return EXIT_FAILURE;

    // Resolve file-based imports before sema so injected declarations are visible.
    {
        std::filesystem::path p(opts.input.empty() ? "." : opts.input);
        std::string base_dir = p.has_parent_path()
                             ? p.parent_path().string() : ".";
        std::unordered_set<std::string> visited;
        if (!opts.input.empty() && opts.input != "-") {
            std::error_code ec;
            auto canon = std::filesystem::canonical(opts.input, ec);
            if (!ec) visited.insert(canon.string());
        }
        driver.resolve_imports(*driver.result, base_dir, visited);
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
            if (!link_executable(tmp_obj.string(), exe_out)) return EXIT_FAILURE;
            std::filesystem::remove(tmp_obj);
        }
    }

    if (opts.dump_ast && driver.error_count() == 0) {
        dux::ast::Printer printer(std::cout);
        printer.print(*driver.result);
    }

    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
