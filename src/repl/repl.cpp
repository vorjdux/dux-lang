#include "repl.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Line-editing back-end (optional; falls back to std::getline when absent).
//   DUX_HAVE_READLINE : GNU readline  (readline/readline.h)
//   DUX_HAVE_LIBEDIT  : libedit with readline-compat API (editline/readline.h)
#if defined(DUX_HAVE_READLINE)
#  include <readline/readline.h>
#  include <readline/history.h>
#  define DUX_RL 1
#elif defined(DUX_HAVE_LIBEDIT)
#  include <editline/readline.h>
#  define DUX_RL 1
#endif

namespace dux {

// ── history file path ─────────────────────────────────────────────────────────
#ifdef DUX_RL
static std::string history_path() {
    const char* home = getenv("HOME");
    if (!home || home[0] == '\0') return "";
    return std::string(home) + "/.dux_history";
}
#endif

// ── constructor / destructor ──────────────────────────────────────────────────

Repl::Repl(const std::string& dux_binary) : dux_binary_(dux_binary) {
    temp_dir_ = "/tmp/dux_repl_" + std::to_string(static_cast<long>(getpid()));
    mkdir(temp_dir_.c_str(), 0700);
}

Repl::~Repl() {
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
}

// ── internal helpers ──────────────────────────────────────────────────────────

void Repl::print_banner() const {
    std::cout << "Dux REPL -- type :help for commands, :q to quit\n";
}

bool Repl::is_decl(const std::string& line) const {
    if (line.empty()) return false;

    auto starts_with_kw = [&](const std::string& kw) -> bool {
        if (line.size() <= kw.size()) return false;
        if (line.substr(0, kw.size()) != kw) return false;
        char next = line[kw.size()];
        return next == ' ' || next == '<' || next == '\t';
    };

    if (starts_with_kw("class"))     return true;
    if (starts_with_kw("enum"))      return true;
    if (starts_with_kw("interface")) return true;
    if (starts_with_kw("import"))    return true;
    if (starts_with_kw("namespace")) return true;
    if (starts_with_kw("extern"))    return true;

    bool has_paren = line.find('(') != std::string::npos;
    bool has_brace = line.find('{') != std::string::npos;
    if (has_paren && has_brace) {
        if (starts_with_kw("void"))   return true;
        if (starts_with_kw("int"))    return true;
        if (starts_with_kw("str"))    return true;
        if (starts_with_kw("bool"))   return true;
        if (starts_with_kw("long"))   return true;
        if (starts_with_kw("double")) return true;
        if (starts_with_kw("float"))  return true;
        if (starts_with_kw("auto"))   return true;
        if (starts_with_kw("fn"))     return true;
    }

    return false;
}

std::string Repl::build_source(const std::string& stmt) const {
    std::string src;
    // Top-level declarations (functions, classes, …)
    for (const auto& d : context_decls_) {
        src += d;
        src += "\n";
    }
    src += "void main() {\n";
    // Replay all previously accepted statements so variables remain in scope.
    for (const auto& s : context_stmts_) {
        src += "    ";
        src += s;
        src += "\n";
    }
    // New statement goes last.
    src += "    ";
    src += stmt;
    src += "\n}\n";
    return src;
}

int Repl::compile_and_run(const std::string& source) {
    static int seq = 0;
    ++seq;

    std::string src_path = temp_dir_ + "/repl_" + std::to_string(seq) + ".dux";
    std::string bin_path = temp_dir_ + "/repl_" + std::to_string(seq);

    {
        std::ofstream f(src_path);
        if (!f) {
            std::cerr << "dux: repl: cannot write temp source\n";
            return 1;
        }
        f << source;
    }

    std::string compile_cmd = dux_binary_ + " --compile " + src_path
                              + " -o " + bin_path + " 2>&1";
    FILE* pipe = popen(compile_cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "dux: repl: cannot run compiler\n";
        return 1;
    }
    std::string compiler_out;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe))
        compiler_out += buf;
    int compile_rc = pclose(pipe);

    if (compile_rc != 0) {
        std::istringstream iss(compiler_out);
        std::string err_line;
        while (std::getline(iss, err_line)) {
            auto pos = err_line.find(src_path);
            if (pos != std::string::npos)
                err_line.replace(pos, src_path.size(), "<input>");
            std::cout << err_line << "\n";
        }
        return 1;
    }

    int run_rc = system(bin_path.c_str());
    return WIFEXITED(run_rc) ? WEXITSTATUS(run_rc) : 1;
}

// ── main loop ─────────────────────────────────────────────────────────────────

void Repl::run() {
    print_banner();

#ifdef DUX_RL
    // Load history from previous sessions
    std::string hist = history_path();
    if (!hist.empty())
        read_history(hist.c_str());
#endif

    while (true) {
        std::string line;

        // ── read one line ────────────────────────────────────────────────────
#ifdef DUX_RL
        char* raw = readline("dux> ");
        if (!raw) {
            // EOF (Ctrl-D)
            std::cout << "\n";
            break;
        }
        line = raw;
        free(raw);
#else
        std::cout << "dux> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
#endif

        // Strip trailing whitespace
        while (!line.empty() &&
               (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.pop_back();

        if (line.empty()) continue;

        // Add non-empty lines to history (dedup consecutive identical entries).
        // Use a local tracker instead of history_list()/HIST_ENTRY to avoid
        // depending on history API symbols not always present in libedit.
#ifdef DUX_RL
        static std::string last_history_line;
        if (line != last_history_line) {
            add_history(line.c_str());
            last_history_line = line;
        }
#endif

        // ─── REPL commands ──────────────────────────────────────────────────
        if (line == ":q" || line == ":quit" || line == "exit" || line == "quit")
            break;

        if (line == ":help") {
            std::cout <<
                "Commands:\n"
                "  :q / :quit   -- exit the REPL\n"
                "  :clear       -- reset the accumulated declaration context\n"
                "  :context     -- show accumulated declarations\n"
                "  :help        -- show this message\n"
                "\n"
                "Tips:\n"
                "  Define functions/classes first, then call them.\n"
                "  Declarations (class, fn, void/int/str/... name(...) {...}) are\n"
                "  accumulated and available to all subsequent statements.\n"
                "  Statements are accumulated too: variables declared on one\n"
                "  line are in scope on every following line.\n"
                "  Note: accumulated statements are replayed on each new line,\n"
                "  so side-effectful calls (print, file I/O) will run again.\n"
                "  Use :clear to reset all accumulated state.\n"
#ifdef DUX_RL
                "  Up/Down arrows navigate command history.\n"
                "  Left/Right arrows and Ctrl+A/E/K/U edit the current line.\n"
#endif
                ;
            continue;
        }

        if (line == ":clear") {
            context_decls_.clear();
            context_stmts_.clear();
            std::cout << "Context cleared.\n";
            continue;
        }

        if (line == ":context") {
            if (context_decls_.empty() && context_stmts_.empty()) {
                std::cout << "(empty)\n";
            } else {
                for (const auto& d : context_decls_)
                    std::cout << d << "\n";
                if (!context_stmts_.empty()) {
                    std::cout << "void main() {\n";
                    for (const auto& s : context_stmts_)
                        std::cout << "    " << s << "\n";
                    std::cout << "    ...\n}\n";
                }
            }
            continue;
        }

        // ─── Classify and handle input ──────────────────────────────────────
        if (is_decl(line)) {
            std::string test_src;
            for (const auto& d : context_decls_) {
                test_src += d;
                test_src += "\n";
            }
            test_src += line;
            test_src += "\nvoid main() {}\n";

            static int verify_seq = 0;
            ++verify_seq;
            std::string vsrc = temp_dir_ + "/decl_" + std::to_string(verify_seq) + ".dux";
            std::string vbin = temp_dir_ + "/decl_" + std::to_string(verify_seq);
            {
                std::ofstream f(vsrc);
                f << test_src;
            }
            std::string vcmd = dux_binary_ + " --compile " + vsrc + " -o " + vbin + " 2>&1";
            FILE* vpipe = popen(vcmd.c_str(), "r");
            std::string vout;
            char vbuf[512];
            while (fgets(vbuf, sizeof(vbuf), vpipe))
                vout += vbuf;
            int vrc = pclose(vpipe);

            if (vrc != 0) {
                std::istringstream iss(vout);
                std::string err_line;
                while (std::getline(iss, err_line)) {
                    auto pos = err_line.find(vsrc);
                    if (pos != std::string::npos)
                        err_line.replace(pos, vsrc.size(), "<input>");
                    std::cout << err_line << "\n";
                }
            } else {
                context_decls_.push_back(line);
                std::cout << "OK\n";
            }
        } else {
            std::string src = build_source(line);
            int rc = compile_and_run(src);
            // Accumulate on success so variables remain in scope for
            // subsequent statements.  On failure nothing is added, keeping
            // the context clean.
            if (rc == 0)
                context_stmts_.push_back(line);
        }
    }

#ifdef DUX_RL
    // Persist history (keep last 500 lines)
    if (!hist.empty()) {
        stifle_history(500);
        write_history(hist.c_str());
    }
#endif
}

} // namespace dux
