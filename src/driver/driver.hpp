#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ast/ast.hpp"

// Forward-declare bison location so we don't pull in the generated header here.
namespace yy { class location; }

// Thrown when the error limit is reached; caught in Driver::parse().
struct DiagnosticAbort {};

class Driver {
public:
    Driver();
    ~Driver();

    // Parse file (or stdin when filename is empty or "-").
    // Returns 0 on success, non-zero on failure.
    int parse(const std::string& filename);

    // Parsed result — valid after a successful parse().
    std::unique_ptr<dux::ast::Program> result;

    // Options settable by the CLI.
    bool trace_scanning{false};
    bool trace_parsing{false};

    // ─── Diagnostics ─────────────────────────────────────────────────────────
    // Emit a clang-style error with source context. Throws DiagnosticAbort
    // once kMaxErrors have been accumulated.
    void error(const yy::location& l, const std::string& msg);

    // Emit a warning (never throws, never increments error count).
    void warning(const yy::location& l, const std::string& msg);

    int  error_count() const { return error_count_; }

    // ─── Called by the lexer ─────────────────────────────────────────────────
    dux::ast::SourceLoc make_loc(const yy::location& l) const;

    // String accumulation buffer for multi-character literals.
    std::string& string_buf() { return string_buf_; }

    const std::string& filename() const { return filename_; }

private:
    static constexpr int kMaxErrors = 20;

    std::string              filename_;
    std::string              string_buf_;
    std::vector<std::string> source_lines_;   // one entry per source line (1-based index)
    int                      error_count_{0};

    void emit_diagnostic(const yy::location& l,
                         const char*          severity,
                         const std::string&   msg) const;
};
