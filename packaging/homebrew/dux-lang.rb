class DuxLang < Formula
  desc "The Dux programming language compiler"
  homepage "https://github.com/vorjdux/dux-lang"
  version "0.1.0"
  license "MIT"

  on_arm do
    url "https://github.com/vorjdux/dux-lang/releases/download/v0.1.0/dux-0.1.0-macos-arm64.tar.gz"
    # Update sha256 after publishing the release:
    # sha256 "<SHA256_OF_MACOS_ARM64_TARBALL>"
  end

  on_intel do
    url "https://github.com/vorjdux/dux-lang/releases/download/v0.1.0/dux-0.1.0-macos-x86_64.tar.gz"
    # Update sha256 after publishing the release:
    # sha256 "<SHA256_OF_MACOS_X86_64_TARBALL>"
  end

  depends_on "cmake" => :build
  depends_on "flex"  => :build
  depends_on "bison" => :build
  depends_on "llvm@18"

  def install
    # Put Homebrew's flex/bison ahead of the system versions and expose
    # llvm@18 tools so CMake's find_package(LLVM) resolves correctly.
    ENV.prepend_path "PATH", "#{Formula["flex"].opt_bin}"
    ENV.prepend_path "PATH", "#{Formula["bison"].opt_bin}"
    ENV.prepend_path "PATH", "#{Formula["llvm@18"].opt_bin}"

    args = std_cmake_args + [
      "-DCMAKE_BUILD_TYPE=Release",
      "-DLLVM_DIR=#{Formula["llvm@18"].opt_lib}/cmake/llvm",
    ]

    system "cmake", "-B", "build", *args
    system "cmake", "--build", "build", "-j#{ENV.make_jobs}"
    system "cmake", "--install", "build"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/dux --version")
  end
end
