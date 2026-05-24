#pragma once
#include <string>
#include <vector>

namespace dux {

class Repl {
    std::string              dux_binary_;    // path to this binary (argv[0])
    std::vector<std::string> context_decls_; // accumulated declarations
    std::string              temp_dir_;

public:
    explicit Repl(const std::string& dux_binary);
    ~Repl();
    void run();

private:
    bool        is_decl(const std::string& line) const;
    int         compile_and_run(const std::string& source);
    void        print_banner() const;
    std::string build_source(const std::string& stmt) const;
};

} // namespace dux
