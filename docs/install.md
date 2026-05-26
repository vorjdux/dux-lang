# Installing Dux

## Quick Install

The fastest way to install Dux on Linux or macOS is the official installer script.
It auto-detects your operating system and architecture, downloads the right
pre-built binary, verifies its SHA-256 checksum, and installs `dux` to
`/usr/local/bin` (or `~/.local/bin` if you prefer a user-level install).

```bash
curl -sSf https://raw.githubusercontent.com/vorjdux/dux-lang/master/install.sh | sh
```

After installation, verify it worked:

```bash
dux --version
# dux 0.1.3 (x86_64-linux)
```

### Installer options

You can control the installer with environment variables and flags:

```bash
# Install a specific version
DUX_VERSION=0.1.3 curl -sSf https://raw.githubusercontent.com/vorjdux/dux-lang/master/install.sh | sh

# Install to a custom directory (no sudo required)
DUX_INSTALL_DIR=~/.local/bin curl -sSf https://raw.githubusercontent.com/vorjdux/dux-lang/master/install.sh | sh

# Dry run - see what would happen without making changes
sh install.sh --dry-run

# Skip PATH modification instructions
sh install.sh --no-modify-path
```

---

## Package Managers

### Ubuntu / Debian

Download the `.deb` package from the
[GitHub Releases page](https://github.com/vorjdux/dux-lang/releases/latest):

```bash
wget https://github.com/vorjdux/dux-lang/releases/download/v0.1.3/dux-lang_0.1.3_amd64.deb
sudo dpkg -i dux-lang_0.1.3_amd64.deb
```

If `dpkg` reports missing dependencies, resolve them with:

```bash
sudo apt-get install -f
```

To remove:

```bash
sudo dpkg -r dux-lang
```

### RHEL / Fedora

Download the `.rpm` package from the
[GitHub Releases page](https://github.com/vorjdux/dux-lang/releases/latest):

```bash
# Fedora (dnf)
sudo dnf localinstall dux-lang-0.1.3-1.x86_64.rpm

# RHEL / CentOS (rpm)
sudo rpm -i dux-lang-0.1.3-1.x86_64.rpm
```

To remove:

```bash
sudo dnf remove dux-lang
# or
sudo rpm -e dux-lang
```

### macOS (Homebrew)

> **Note:** The Homebrew formula is planned once the tap is published.
> Check [github.com/vorjdux/dux-lang](https://github.com/vorjdux/dux-lang) for
> current status.

Once the tap is available:

```bash
brew tap vorjdux/dux-lang
brew install dux-lang
```

To remove:

```bash
brew uninstall dux-lang
```

### Pre-built binary (all platforms)

Download the appropriate tarball from the
[GitHub Releases page](https://github.com/vorjdux/dux-lang/releases/latest),
extract it, and move the binary to a directory on your `PATH`.

| Platform | Archive |
|----------|---------|
| Linux x86_64 | `dux-0.1.3-linux-x86_64.tar.gz` |
| Linux ARM64 (aarch64) | `dux-0.1.3-linux-aarch64.tar.gz` |
| macOS Apple Silicon (ARM64) | `dux-0.1.3-macos-aarch64.tar.gz` |
| macOS Intel (x86_64) | `dux-0.1.3-macos-x86_64.tar.gz` |

```bash
# Example: Linux x86_64
wget https://github.com/vorjdux/dux-lang/releases/download/v0.1.3/dux-0.1.3-linux-x86_64.tar.gz

# Verify checksum
wget https://github.com/vorjdux/dux-lang/releases/download/v0.1.3/SHA256SUMS
sha256sum --check --ignore-missing SHA256SUMS

# Extract and install
tar -xzf dux-0.1.3-linux-x86_64.tar.gz
sudo mv dux /usr/local/bin/
sudo chmod +x /usr/local/bin/dux
```

---

## Build from Source

Building from source requires a C++23-capable compiler, LLVM, Flex, Bison, and CMake.

### Requirements

| Tool | Minimum version | Notes |
|------|----------------|-------|
| CMake | 3.25 | Build system |
| GCC | 13 | C++23 support (`-std=c++23`) |
| Clang | 16 | Alternative to GCC; also enables LTO |
| Flex | 2.6 | Lexer generator |
| Bison | 3.8 | Parser generator |
| LLVM | 17 (18 recommended) | Compiler backend |
| OpenSSL | 1.1 or 3.x | Required by the TLS runtime module |

> **LTO:** If `clang` and `llvm-link` are available, the build automatically
> compiles the runtime to LLVM bitcode (`duxrt.bc`) and enables link-time
> optimisation at `-O1` and above.  This is optional - without it the compiler
> still works and only LTO is skipped.

### Ubuntu / Debian

```bash
# Install build dependencies
sudo apt update
sudo apt install -y \
    cmake flex bison \
    g++-13 \
    llvm-18-dev clang-18 \
    libssl-dev

# Clone the repository
git clone https://github.com/vorjdux/dux-lang.git
cd dux-lang

# Configure and build (Release)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install system-wide
sudo cmake --install build
```

### Fedora / RHEL

```bash
# Install build dependencies
sudo dnf install -y \
    cmake flex bison \
    gcc-c++ \
    llvm-devel clang \
    openssl-devel

# Clone and build
git clone https://github.com/vorjdux/dux-lang.git
cd dux-lang
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

### macOS

```bash
# Install dependencies via Homebrew
brew install cmake flex bison llvm openssl

# Make Homebrew LLVM take precedence over Apple's stub
export PATH="$(brew --prefix llvm)/bin:$PATH"
export LDFLAGS="-L$(brew --prefix llvm)/lib"
export CPPFLAGS="-I$(brew --prefix llvm)/include"

# Clone and build
git clone https://github.com/vorjdux/dux-lang.git
cd dux-lang
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm"
cmake --build build -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build
```

### Generic Unix

On any POSIX system with LLVM 17+ installed:

```bash
git clone https://github.com/vorjdux/dux-lang.git
cd dux-lang

cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm

cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
cmake --install build --prefix /usr/local
```

### Debug build

The Debug configuration enables AddressSanitizer and UndefinedBehaviourSanitizer:

```bash
cmake -B build-dbg -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg -j$(nproc)
```

### Running the test suite

```bash
# Build and run all tests
cmake --build build
ctest --test-dir build --output-on-failure

# Run with verbose output
ctest --test-dir build -V

# Run a specific test
ctest --test-dir build -R run_ok_hello_world
```

The test suite includes ~200 tests across these categories:

| Category | Description |
|----------|-------------|
| `example_*` | Smoke-parse all files in `examples/` |
| `parse_ok_*` | Files that must parse successfully |
| `parse_fail_*` | Files that must produce parse errors |
| `sema_ok_*` | Files that must pass semantic analysis |
| `sema_fail_*` | Files that must fail semantic analysis |
| `codegen_ok_*` | Files that must produce valid LLVM IR |
| `run_ok_*` | Files that compile, run, and match `.expected` output |
| Runtime unit tests | C-level tests for `duxrt` internals |

---

## Verifying the Installation

After installation, confirm the compiler is accessible and functional:

```bash
dux --version
```

Expected output (version and platform may differ):

```
dux 0.1.3 (x86_64-linux)
```

```bash
dux --help
```

Expected output:

```
Usage: dux [options] [file]

Options:
  --compile       Compile to a runnable native executable
  --emit-ir       Emit LLVM IR (to -o path or stdout)
  --emit-obj      Emit native object file (requires -o path)
  --check         Run semantic analysis and report errors
  --dump-ast      Print the parsed AST to stdout
  --repl          Start the interactive REPL
  -o <path>       Output path
  -O0/-O1/-O2/-O3 Optimisation level (default -O0)
  -g              Emit DWARF debug information
  --version       Print version and exit
```

Compile and run a minimal program:

```bash
cat > hello.dux << 'EOF'
void main() {
    println("Hello from Dux!")
}
EOF

dux --compile hello.dux -o hello
./hello
# Hello from Dux!
```

---

## Uninstalling

### Installed via dpkg (Ubuntu/Debian)

```bash
sudo dpkg -r dux-lang
```

### Installed via rpm (RHEL/Fedora)

```bash
sudo rpm -e dux-lang
# or
sudo dnf remove dux-lang
```

### Installed via Homebrew (macOS)

```bash
brew uninstall dux-lang
```

### Installed via the install script or manual tarball

Remove the binary from wherever it was placed:

```bash
# Default system-wide install
sudo rm /usr/local/bin/dux

# User install
rm ~/.local/bin/dux
```

If you also installed via `cmake --install`, remove the installed files:

```bash
sudo rm /usr/local/bin/dux
sudo rm /usr/local/lib/libduxrt.a
sudo rm -rf /usr/local/include/duxrt
sudo rm -rf /usr/local/share/dux
```
