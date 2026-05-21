#pragma once
#include <memory>
#include <string>
#include "ast/ast.hpp"

// Forward-declare the generated parser/lexer types.
// The generated parser.hpp is included only where the full definition is needed.
namespace yy { class location; }

class Driver {
public:
    Driver();
    ~Driver();

    // Parse file (or stdin if empty/"-").  Returns 0 on success.
    int parse(const std::string& filename);

    // Parsed result – valid after a successful parse().
    std::unique_ptr<dux::ast::Program> result;

    // Options
    bool trace_scanning{false};
    bool trace_parsing{false};

    // ─── Called by the lexer ─────────────────────────────────────────────────
    // Convert a bison location into an AST source location.
    dux::ast::SourceLoc make_loc(const yy::location& l) const;

    // Accumulation buffer for string literals being scanned.
    std::string& string_buf() { return string_buf_; }

    const std::string& filename() const { return filename_; }

private:
    std::string filename_;
    std::string string_buf_;
};
