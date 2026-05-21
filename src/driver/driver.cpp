#include "driver.hpp"
#include "parser.hpp"   /* generated — full yy::parser definition */
#include <cerrno>
#include <cstring>
#include <iostream>

/* ─── Flex interface ────────────────────────────────────────────────────── */
extern int   yylex_destroy();
extern FILE* yyin;
extern int   yy_flex_debug;

/* yylex is declared in parser.hpp via YY_DECL in dux.l */
yy::parser::symbol_type yylex(Driver& driver, yy::location& loc);

/* ─── Driver ────────────────────────────────────────────────────────────── */
Driver::Driver()  = default;
Driver::~Driver() = default;

int Driver::parse(const std::string& filename) {
    filename_ = filename;

    if (filename_.empty() || filename_ == "-") {
        yyin = stdin;
    } else {
        yyin = std::fopen(filename_.c_str(), "r");
        if (!yyin) {
            std::cerr << "dux: cannot open '" << filename_ << "': "
                      << std::strerror(errno) << '\n';
            return 1;
        }
    }

    yy_flex_debug = trace_scanning ? 1 : 0;

    /* loc is threaded through both parser and yylex via %param */
    yy::location loc;
    loc.initialize(&filename_);

    yy::parser p(*this, loc);
    p.set_debug_level(trace_parsing ? 1 : 0);

    int rc = p.parse();

    if (yyin != stdin) std::fclose(yyin);
    yylex_destroy();
    return rc;
}

dux::ast::SourceLoc Driver::make_loc(const yy::location& l) const {
    dux::ast::SourceLoc sl;
    sl.file = filename_;
    sl.line = l.begin.line;
    sl.col  = l.begin.column;
    return sl;
}
