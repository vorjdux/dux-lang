Name:           dux-lang
Version:        0.1.3
Release:        1%{?dist}
Summary:        The Dux programming language compiler

License:        MIT
URL:            https://github.com/vorjdux/dux-lang
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.25
BuildRequires:  flex
BuildRequires:  bison >= 3.8
BuildRequires:  gcc-c++
BuildRequires:  llvm18-devel
BuildRequires:  clang18
BuildRequires:  zstd-devel
BuildRequires:  openssl-devel

Requires:       glibc >= 2.17

%description
Dux is a statically typed, compiled programming language that targets LLVM IR.
It features a clean C-like syntax with modern language constructs including
generics, closures, algebraic data types, pattern matching, RAII-based resource
management, and an async/await concurrency model. The Dux compiler produces
native binaries via LLVM and ships with a runtime library (duxrt) and a
standard library covering I/O, networking, threading, and data structures.

%prep
%autosetup

%build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix}
%cmake_build

%install
%cmake_install

%check
%ctest

%files
%license LICENSE
%doc README.md
%{_bindir}/dux
%{_libdir}/libduxrt.a
%{_includedir}/duxrt/
%{_datadir}/dux/stdlib/

%changelog
* Mon May 26 2026 Dux Lang <vorj.dux@gmail.com> - 0.1.3-1
- Fix thread_local globals orphaned in codegen (pre-declare before function bodies)
- Fix socket portability: add set_reuseaddr/set_reuseport with platform constants
- Fix macOS build: missing signal.h, FAM Clang warning, -lc++ vs -lstdc++
- Fix macOS Release: -march=native now opt-in; bake OpenSSL paths at cmake time
- Eliminate all compiler warnings across GCC 13, Clang 18, and Apple Clang

* Tue May 26 2026 Dux Lang <vorj.dux@gmail.com> - 0.1.2-1
- Fix CI on all four platforms (macOS, Ubuntu GCC/Clang, Fedora)
- Resolve merge conflicts with master; bump spec to version 0.2
- Replace em dashes with plain hyphens in documentation

* Mon May 26 2025 Dux Lang <vorj.dux@gmail.com> - 0.1.0-1
- Initial release of the Dux programming language compiler
