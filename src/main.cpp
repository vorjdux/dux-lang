#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "ast/printer.hpp"
#include "driver/driver.hpp"
#include "sema/sema.hpp"
#include "codegen/codegen.hpp"

#ifndef DUX_VERSION
#define DUX_VERSION "0.1.0-dev"
#endif

// Paths baked in at build time by CMake
#ifndef DUXRT_LIB_PATH
#define DUXRT_LIB_PATH ""
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
    std::string cmd = "cc "
        + obj_path + " "
        + DUXRT_LIB_PATH + " "
        "-lm -o " + out_path;
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "dux: linker failed (exit " << rc << ")\n";
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
