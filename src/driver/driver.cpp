#include "driver.hpp"
#include "parser.hpp"   /* generated — full yy::parser definition */
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>

/* ─── Flex interface ────────────────────────────────────────────────────── */
extern int   yylex_destroy();
extern FILE* yyin;
extern int   yy_flex_debug;

yy::parser::symbol_type yylex(Driver& driver, yy::location& loc);

/* ─── Driver ────────────────────────────────────────────────────────────── */
Driver::Driver()  = default;
Driver::~Driver() = default;

int Driver::parse(const std::string& filename) {
    filename_     = filename;
    error_count_  = 0;
    source_lines_.clear();

    if (filename_.empty() || filename_ == "-") {
        yyin = stdin;
    } else {
        // Pre-load every source line so error() can show context.
        if (std::ifstream src(filename_); src) {
            std::string line;
            while (std::getline(src, line))
                source_lines_.push_back(std::move(line));
        }

        yyin = std::fopen(filename_.c_str(), "r");
        if (!yyin) {
            std::cerr << "dux: cannot open '" << filename_ << "': "
                      << std::strerror(errno) << '\n';
            return 1;
        }
    }

    yy_flex_debug = trace_scanning ? 1 : 0;

    yy::location loc;
    loc.initialize(&filename_);

    yy::parser p(*this, loc);
    p.set_debug_level(trace_parsing ? 1 : 0);

    int rc = 0;
    try {
        rc = p.parse();
    } catch (const DiagnosticAbort&) {
        rc = 1;
    }

    if (error_count_ > 0) rc = 1;

    if (yyin != stdin) std::fclose(yyin);
    yylex_destroy();
    return rc;
}

/* ─── Diagnostics ───────────────────────────────────────────────────────── */

void Driver::emit_diagnostic(const yy::location& l,
                              const char*          severity,
                              const std::string&   msg) const {
    const std::string& fname = filename_.empty() ? "<stdin>" : filename_;
    std::cerr << fname << ':' << l.begin.line << ':' << l.begin.column
              << ": " << severity << ": " << msg << '\n';

    const int ln = l.begin.line;
    if (ln >= 1 && ln <= static_cast<int>(source_lines_.size())) {
        const auto& src = source_lines_[static_cast<std::size_t>(ln - 1)];
        std::cerr << "    " << src << '\n';

        // Caret under the start of the offending token.
        const int col = std::max(1, static_cast<int>(l.begin.column));
        std::cerr << std::string(4 + static_cast<std::size_t>(col - 1), ' ') << '^';

        // Tilde underline for multi-column spans on the same line.
        if (l.begin.line == l.end.line) {
            const int span = static_cast<int>(l.end.column) - col;
            if (span > 1)
                std::cerr << std::string(static_cast<std::size_t>(span - 1), '~');
        }
        std::cerr << '\n';
    }
}

void Driver::error(const yy::location& l, const std::string& msg) {
    emit_diagnostic(l, "error", msg);
    if (++error_count_ >= kMaxErrors) {
        std::cerr << (filename_.empty() ? "<stdin>" : filename_)
                  << ": fatal: too many errors, aborting\n";
        throw DiagnosticAbort{};
    }
}

void Driver::warning(const yy::location& l, const std::string& msg) {
    emit_diagnostic(l, "warning", msg);
}

dux::ast::SourceLoc Driver::make_loc(const yy::location& l) const {
    dux::ast::SourceLoc sl;
    sl.file = filename_;
    sl.line = l.begin.line;
    sl.col  = l.begin.column;
    return sl;
}
