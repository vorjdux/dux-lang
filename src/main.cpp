#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

#include "ast/printer.hpp"
#include "driver/driver.hpp"

namespace {

struct Options {
    std::string  input;        // filename or "" for stdin
    bool         dump_ast{false};
    bool         trace_lex{false};
    bool         trace_parse{false};
    bool         help{false};
};

void usage(std::string_view prog) {
    std::cerr <<
        "Usage: " << prog << " [options] [file]\n"
        "\n"
        "Options:\n"
        "  --dump-ast      Print the parsed AST to stdout\n"
        "  --trace-lex     Enable flex debug output\n"
        "  --trace-parse   Enable bison debug output\n"
        "  -h, --help      Show this message\n"
        "\n"
        "If no file is given, reads from stdin.\n";
}

Options parse_args(std::span<char*> args) {
    Options opts;
    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view a = args[i];
        if (a == "--dump-ast")    { opts.dump_ast    = true; }
        else if (a == "--trace-lex")   { opts.trace_lex   = true; }
        else if (a == "--trace-parse") { opts.trace_parse = true; }
        else if (a == "-h" || a == "--help") { opts.help = true; }
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

    if (opts.help) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    Driver driver;
    driver.trace_scanning = opts.trace_lex;
    driver.trace_parsing  = opts.trace_parse;

    int rc = driver.parse(opts.input);
    if (rc != 0) return rc;

    if (opts.dump_ast && driver.result) {
        dux::ast::Printer printer(std::cout);
        printer.print(*driver.result);
    }

    return EXIT_SUCCESS;
}
