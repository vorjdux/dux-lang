#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

#include "ast/printer.hpp"
#include "driver/driver.hpp"

#ifndef DUX_VERSION
#define DUX_VERSION "0.1.0-dev"
#endif

namespace {

struct Options {
    std::string  input;
    bool         dump_ast{false};
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
        else if (a == "--trace-lex")           { opts.trace_lex   = true; }
        else if (a == "--trace-parse")         { opts.trace_parse = true; }
        else if (a == "--version")             { opts.version     = true; }
        else if (a == "-h" || a == "--help")   { opts.help        = true; }
        else if (a.starts_with('-')) {
            std::cerr << "dux: unknown option: " << a << '\n';
            opts.help = true;
        } else {
            opts.input = std::string(a);
        }
    }
    return opts;
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

    if (opts.dump_ast && driver.result && driver.error_count() == 0) {
        dux::ast::Printer printer(std::cout);
        printer.print(*driver.result);
    }

    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
