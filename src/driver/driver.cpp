#include "driver.hpp"
#include "parser.hpp"   /* generated — full yy::parser definition */
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

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

/* ─── Import resolution ─────────────────────────────────────────────────── */

// Known stdlib module names — these are handled by codegen, not file loading.
static const std::unordered_set<std::string> kStdlibModules = {"math", "str", "io"};

// Convert a dotted import path to a filesystem path.
// "utils"           → "<base>/utils.dux"
// "geometry.shapes" → "<base>/geometry/shapes.dux"
static std::string find_import_file(const std::string& import_path,
                                    const std::string& base_dir) {
    // Replace dots (except trailing component) with path separators
    std::string rel = import_path;
    for (char& c : rel)
        if (c == '.') c = '/';
    rel += ".dux";

    fs::path candidate = fs::path(base_dir) / rel;
    std::error_code ec;
    candidate = fs::canonical(candidate, ec);
    if (!ec && fs::exists(candidate))
        return candidate.string();
    return {};
}

// Extract the last dotted component: "geometry.shapes" → "shapes"
static std::string last_component(const std::string& path) {
    auto pos = path.rfind('.');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

void Driver::resolve_imports(dux::ast::Program& prog,
                             const std::string& base_dir,
                             std::unordered_set<std::string>& visited) {
    using namespace dux::ast;

    // Collect import declarations and build the injection list in one pass.
    // We operate on a snapshot of decls so we can append without invalidating.
    std::vector<std::unique_ptr<Decl>> injected;

    for (auto& dp : prog.decls) {
        auto* imp = dynamic_cast<ImportDecl*>(dp.get());
        if (!imp) continue;

        // Leave stdlib imports for codegen to handle.
        const std::string mod = last_component(imp->path);
        if (kStdlibModules.count(mod)) continue;

        // Find the file on disk.
        std::string filepath = find_import_file(imp->path, base_dir);
        if (filepath.empty()) {
            std::cerr << "dux: import: module '" << imp->path
                      << "' not found (looked in '" << base_dir << "')\n";
            continue;
        }

        // Cycle guard.
        if (visited.count(filepath)) continue;
        visited.insert(filepath);

        // Parse the imported file with a fresh Driver instance.
        Driver sub;
        if (sub.parse(filepath) != 0) {
            std::cerr << "dux: import: errors in '" << filepath << "'\n";
            continue;
        }

        // Recursively resolve imports inside the imported file.
        std::string sub_dir = fs::path(filepath).parent_path().string();
        resolve_imports(*sub.result, sub_dir, visited);

        // Separate ImportDecl nodes from real declarations inside the sub-program.
        // stdlib imports (e.g. `import math` inside an imported file) must be
        // hoisted to the top level of the parent prog so codegen's pass 1 sees them.
        std::vector<std::unique_ptr<Decl>> sub_imports;
        std::vector<std::unique_ptr<Decl>> sub_decls;
        for (auto& d : sub.result->decls) {
            if (dynamic_cast<ImportDecl*>(d.get()))
                sub_imports.push_back(std::move(d));
            else
                sub_decls.push_back(std::move(d));
        }
        // Hoist stdlib imports into the parent (non-stdlib ones were already
        // resolved recursively and their declarations injected above).
        for (auto& d : sub_imports)
            injected.push_back(std::move(d));

        if (imp->symbols.empty()) {
            // ── Full import ───────────────────────────────────────────────────
            // Wrap all declarations in a NamespaceDecl so the caller accesses
            // them as  mod.fn(args).  Top-level statements are not re-executed.
            auto ns   = std::make_unique<NamespaceDecl>();
            ns->name  = mod;
            ns->decls = std::move(sub_decls);
            injected.push_back(std::move(ns));
        } else {
            // ── Selective import ──────────────────────────────────────────────
            // All declarations are injected into global scope so that private
            // helpers used by requested symbols compile correctly.
            // The symbols list documents which names the caller relies on.
            for (auto& d : sub_decls)
                injected.push_back(std::move(d));
        }
    }

    // Prepend injected declarations.  ImportDecl nodes (stdlib re-exports) must
    // come before any function declarations that reference their modules, so
    // split into imports-first then everything else.
    std::vector<std::unique_ptr<Decl>> imports_first, decls_rest;
    for (auto& d : injected) {
        if (dynamic_cast<ImportDecl*>(d.get()))
            imports_first.push_back(std::move(d));
        else
            decls_rest.push_back(std::move(d));
    }

    // Insert non-import declarations then imports, both at front, so that
    // after insertion the order is:  [imports_first] [decls_rest] [original]
    for (int i = static_cast<int>(decls_rest.size()) - 1; i >= 0; --i)
        prog.decls.insert(prog.decls.begin(), std::move(decls_rest[static_cast<std::size_t>(i)]));
    for (int i = static_cast<int>(imports_first.size()) - 1; i >= 0; --i)
        prog.decls.insert(prog.decls.begin(), std::move(imports_first[static_cast<std::size_t>(i)]));
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

void Driver::error(const dux::ast::SourceLoc& sl, const std::string& msg) {
    yy::position p;
    p.filename = &filename_;
    p.line     = static_cast<unsigned>(sl.line > 0 ? sl.line : 1);
    p.column   = static_cast<unsigned>(sl.col  > 0 ? sl.col  : 1);
    error(yy::location(p, p), msg);
}

void Driver::warning(const dux::ast::SourceLoc& sl, const std::string& msg) {
    yy::position p;
    p.filename = &filename_;
    p.line     = static_cast<unsigned>(sl.line > 0 ? sl.line : 1);
    p.column   = static_cast<unsigned>(sl.col  > 0 ? sl.col  : 1);
    warning(yy::location(p, p), msg);
}
