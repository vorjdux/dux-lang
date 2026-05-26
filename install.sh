#!/bin/sh
# Dux Language Installer
# Usage: curl -sSf https://raw.githubusercontent.com/vorjdux/dux-lang/master/install.sh | sh
#
# Environment variables:
#   DUX_VERSION      - version to install (default: latest)
#   DUX_INSTALL_DIR  - installation directory (default: /usr/local/bin or ~/.local/bin)
#
# Flags:
#   --dry-run        - print what would be done, but don't install
#   --help           - print this help message
#   --no-modify-path - skip the PATH update hint
#
# POSIX sh compatible (works with bash, dash, ash, etc.)

set -e

REPO="vorjdux/dux-lang"
BINARY_NAME="dux"
DEFAULT_VERSION="0.1.3"

# ─── Colour output ────────────────────────────────────────────────────────────

_setup_colors() {
    # Disable colours if NO_COLOR is set, or stdout is not a terminal
    if [ -n "${NO_COLOR:-}" ] || [ ! -t 1 ]; then
        RED=""
        GREEN=""
        YELLOW=""
        BOLD=""
        RESET=""
    else
        RED="\033[0;31m"
        GREEN="\033[0;32m"
        YELLOW="\033[0;33m"
        BOLD="\033[1m"
        RESET="\033[0m"
    fi
}

info()    { printf "%b info%b  %s\n"  "${GREEN}"  "${RESET}" "$*"; }
warn()    { printf "%b warn%b  %s\n"  "${YELLOW}" "${RESET}" "$*"; }
error()   { printf "%b error%b %s\n"  "${RED}"    "${RESET}" "$*" >&2; }
bold()    { printf "%b%s%b\n"         "${BOLD}"   "$*" "${RESET}"; }

# ─── Error handling ───────────────────────────────────────────────────────────

_cleanup() {
    if [ -n "${TMP_DIR:-}" ] && [ -d "${TMP_DIR}" ]; then
        rm -rf "${TMP_DIR}"
    fi
}

trap '_cleanup' EXIT INT TERM

die() {
    error "$*"
    exit 1
}

# ─── Argument parsing ─────────────────────────────────────────────────────────

DRY_RUN=0
NO_MODIFY_PATH=0
SHOW_HELP=0

for arg in "$@"; do
    case "$arg" in
        --dry-run)        DRY_RUN=1 ;;
        --no-modify-path) NO_MODIFY_PATH=1 ;;
        --help|-h)        SHOW_HELP=1 ;;
        *)
            error "Unknown argument: $arg"
            error "Run with --help for usage information."
            exit 1
            ;;
    esac
done

_print_help() {
    cat <<EOF
Dux Language Installer

USAGE:
    curl -sSf https://raw.githubusercontent.com/vorjdux/dux-lang/master/install.sh | sh
    sh install.sh [OPTIONS]

OPTIONS:
    --dry-run           Print what would be done without installing
    --no-modify-path    Do not print PATH modification instructions
    --help              Print this help message

ENVIRONMENT:
    DUX_VERSION         Version to install (default: ${DEFAULT_VERSION})
    DUX_INSTALL_DIR     Installation directory
                        (default: /usr/local/bin if writable with sudo, else ~/.local/bin)

EXAMPLES:
    # Install latest release
    curl -sSf https://raw.githubusercontent.com/vorjdux/dux-lang/master/install.sh | sh

    # Install specific version
    DUX_VERSION=0.1.3 curl -sSf https://raw.githubusercontent.com/vorjdux/dux-lang/master/install.sh | sh

    # Dry run
    sh install.sh --dry-run

    # Install to a custom directory
    DUX_INSTALL_DIR=~/.dux/bin sh install.sh
EOF
}

if [ "${SHOW_HELP}" -eq 1 ]; then
    _print_help
    exit 0
fi

# ─── Platform detection ───────────────────────────────────────────────────────

_detect_os() {
    OS="$(uname -s 2>/dev/null || echo unknown)"
    case "${OS}" in
        Linux)   OS="linux" ;;
        Darwin)  OS="macos" ;;
        *)
            die "Unsupported operating system: ${OS}
Dux supports Linux and macOS. For other platforms please build from source:
https://github.com/${REPO}#build-from-source"
            ;;
    esac
}

_detect_arch() {
    ARCH="$(uname -m 2>/dev/null || echo unknown)"
    case "${ARCH}" in
        x86_64)          ARCH="x86_64" ;;
        aarch64|arm64)   ARCH="aarch64" ;;
        *)
            die "Unsupported architecture: ${ARCH}
Dux provides pre-built binaries for x86_64 and aarch64/arm64.
For other architectures please build from source:
https://github.com/${REPO}#build-from-source"
            ;;
    esac
}

# ─── Dependency checks ────────────────────────────────────────────────────────

_require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        die "Required tool not found: $1
Please install it and re-run the installer."
    fi
}

_check_dependencies() {
    _require curl
    _require tar

    # sha256sum (Linux) or shasum (macOS)
    if command -v sha256sum >/dev/null 2>&1; then
        SHA256_CMD="sha256sum"
    elif command -v shasum >/dev/null 2>&1; then
        SHA256_CMD="shasum -a 256"
    else
        die "No SHA-256 utility found (tried sha256sum, shasum).
Please install one and re-run the installer."
    fi
}

# ─── Version resolution ───────────────────────────────────────────────────────

_resolve_version() {
    VERSION="${DUX_VERSION:-}"

    if [ -z "${VERSION}" ]; then
        info "Resolving latest Dux release..."
        LATEST_URL="https://api.github.com/repos/${REPO}/releases/latest"
        VERSION="$(curl -sSf "${LATEST_URL}" 2>/dev/null \
            | grep '"tag_name"' \
            | sed 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/' \
            | head -1)" || true

        if [ -z "${VERSION}" ]; then
            warn "Could not determine latest version from GitHub API."
            warn "Falling back to default version: ${DEFAULT_VERSION}"
            VERSION="${DEFAULT_VERSION}"
        fi
    fi

    # Strip leading 'v' if present
    VERSION="${VERSION#v}"
    info "Installing Dux ${VERSION}"
}

# ─── Install directory ────────────────────────────────────────────────────────

_resolve_install_dir() {
    INSTALL_DIR="${DUX_INSTALL_DIR:-}"

    if [ -z "${INSTALL_DIR}" ]; then
        # Try /usr/local/bin first; fall back to ~/.local/bin
        if [ -w "/usr/local/bin" ] || sudo -n true 2>/dev/null; then
            INSTALL_DIR="/usr/local/bin"
        else
            INSTALL_DIR="${HOME}/.local/bin"
        fi
    fi

    # Expand tilde manually for POSIX portability
    case "${INSTALL_DIR}" in
        "~/"*)  INSTALL_DIR="${HOME}/${INSTALL_DIR#~/}" ;;
        "~")    INSTALL_DIR="${HOME}" ;;
    esac
}

# ─── Download and verify ──────────────────────────────────────────────────────

_download() {
    ARCHIVE_NAME="dux-${VERSION}-${OS}-${ARCH}.tar.gz"
    BASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}"
    ARCHIVE_URL="${BASE_URL}/${ARCHIVE_NAME}"
    SUMS_URL="${BASE_URL}/SHA256SUMS"

    info "Downloading ${ARCHIVE_NAME}..."

    if [ "${DRY_RUN}" -eq 1 ]; then
        info "[dry-run] Would download: ${ARCHIVE_URL}"
        info "[dry-run] Would verify:   ${SUMS_URL}"
        info "[dry-run] Would install to: ${INSTALL_DIR}/${BINARY_NAME}"
        return 0
    fi

    TMP_DIR="$(mktemp -d)"
    ARCHIVE_PATH="${TMP_DIR}/${ARCHIVE_NAME}"
    SUMS_PATH="${TMP_DIR}/SHA256SUMS"

    # Download archive
    if ! curl -sSfL --progress-bar "${ARCHIVE_URL}" -o "${ARCHIVE_PATH}"; then
        die "Failed to download ${ARCHIVE_URL}
Please check:
  - Your internet connection
  - That version v${VERSION} exists: https://github.com/${REPO}/releases"
    fi

    # Download checksum file
    info "Verifying checksum..."
    if ! curl -sSfL "${SUMS_URL}" -o "${SUMS_PATH}"; then
        die "Failed to download checksum file: ${SUMS_URL}"
    fi

    # Verify SHA256
    EXPECTED_SUM="$(grep "${ARCHIVE_NAME}" "${SUMS_PATH}" | cut -d' ' -f1)"
    if [ -z "${EXPECTED_SUM}" ]; then
        die "Checksum not found for ${ARCHIVE_NAME} in SHA256SUMS"
    fi

    ACTUAL_SUM="$(${SHA256_CMD} "${ARCHIVE_PATH}" | cut -d' ' -f1)"

    if [ "${ACTUAL_SUM}" != "${EXPECTED_SUM}" ]; then
        die "Checksum mismatch for ${ARCHIVE_NAME}
  expected: ${EXPECTED_SUM}
  actual:   ${ACTUAL_SUM}
The download may be corrupted. Please try again."
    fi

    info "Checksum verified."
}

# ─── Install binary ───────────────────────────────────────────────────────────

_install() {
    if [ "${DRY_RUN}" -eq 1 ]; then
        return 0
    fi

    info "Extracting archive..."
    tar -xzf "${ARCHIVE_PATH}" -C "${TMP_DIR}"

    # Find the extracted binary (may be in a subdirectory)
    EXTRACTED_BIN="$(find "${TMP_DIR}" -type f -name "${BINARY_NAME}" | head -1)"
    if [ -z "${EXTRACTED_BIN}" ]; then
        die "Binary '${BINARY_NAME}' not found in archive ${ARCHIVE_NAME}"
    fi

    # Create install dir if needed
    if [ ! -d "${INSTALL_DIR}" ]; then
        info "Creating directory: ${INSTALL_DIR}"
        if ! mkdir -p "${INSTALL_DIR}" 2>/dev/null; then
            sudo mkdir -p "${INSTALL_DIR}" || die "Failed to create ${INSTALL_DIR}"
        fi
    fi

    DEST="${INSTALL_DIR}/${BINARY_NAME}"

    info "Installing to ${DEST}..."

    # Try direct copy first, fall back to sudo
    if ! install -m 755 "${EXTRACTED_BIN}" "${DEST}" 2>/dev/null; then
        if ! sudo install -m 755 "${EXTRACTED_BIN}" "${DEST}"; then
            die "Failed to install ${BINARY_NAME} to ${DEST}
Try setting DUX_INSTALL_DIR to a directory you have write access to:
  DUX_INSTALL_DIR=~/.local/bin sh install.sh"
        fi
    fi

    info "Installed: ${DEST}"
}

# ─── PATH check ───────────────────────────────────────────────────────────────

_check_path() {
    if [ "${DRY_RUN}" -eq 1 ] || [ "${NO_MODIFY_PATH}" -eq 1 ]; then
        return 0
    fi

    case ":${PATH}:" in
        *":${INSTALL_DIR}:"*)
            # Already in PATH - nothing to say
            ;;
        *)
            warn "${INSTALL_DIR} is not in your PATH."
            warn "Add it by running one of the following:"
            echo ""
            echo "  For bash (~/.bashrc or ~/.bash_profile):"
            echo "    export PATH=\"${INSTALL_DIR}:\$PATH\""
            echo ""
            echo "  For zsh (~/.zshrc):"
            echo "    export PATH=\"${INSTALL_DIR}:\$PATH\""
            echo ""
            echo "  For fish (~/.config/fish/config.fish):"
            echo "    fish_add_path ${INSTALL_DIR}"
            echo ""
            ;;
    esac
}

# ─── Verify install ───────────────────────────────────────────────────────────

_verify_install() {
    if [ "${DRY_RUN}" -eq 1 ]; then
        return 0
    fi

    DEST="${INSTALL_DIR}/${BINARY_NAME}"
    if [ ! -x "${DEST}" ]; then
        die "Installation appears to have failed: ${DEST} is not executable."
    fi

    INSTALLED_VERSION="$("${DEST}" --version 2>/dev/null | head -1 || echo unknown)"
    info "Installed version: ${INSTALLED_VERSION}"
}

# ─── Main ─────────────────────────────────────────────────────────────────────

main() {
    _setup_colors

    echo ""
    bold "  Dux Language Installer"
    echo ""

    _detect_os
    _detect_arch
    _check_dependencies
    _resolve_version
    _resolve_install_dir

    info "Platform: ${OS} / ${ARCH}"
    info "Install directory: ${INSTALL_DIR}"

    _download
    _install
    _verify_install
    _check_path

    echo ""
    if [ "${DRY_RUN}" -eq 1 ]; then
        bold "  Dry run complete. No files were changed."
    else
        bold "  Dux ${VERSION} installed successfully!"
        echo ""
        echo "  Get started:"
        echo "    dux --help"
        echo "    dux --compile hello.dux -o hello && ./hello"
        echo ""
        echo "  Documentation: https://github.com/${REPO}"
    fi
    echo ""
}

main "$@"
