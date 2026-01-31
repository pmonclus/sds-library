#!/bin/bash
# build-package.sh - Build SDS distribution packages
#
# Usage:
#   ./packaging/build-package.sh tarball   # Create source tarball for Homebrew
#   ./packaging/build-package.sh homebrew  # Install via Homebrew (requires tarball first)
#   ./packaging/build-package.sh debian    # Build Debian package (requires debuild)
#   ./packaging/build-package.sh all       # Build everything

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
VERSION="0.3.0"

cd "$REPO_ROOT"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# ============== Create Source Tarball ==============
create_tarball() {
    log_info "Creating source tarball..."
    
    TARBALL_NAME="sds-source-$VERSION"
    TARBALL_PATH="$HOME/$TARBALL_NAME.tar.gz"
    
    # Create a clean copy
    TEMP_DIR=$(mktemp -d)
    mkdir -p "$TEMP_DIR/$TARBALL_NAME"
    
    # Copy source files (excluding build artifacts and unnecessary files)
    rsync -a --exclude='.git' \
             --exclude='build' \
             --exclude='*.o' \
             --exclude='*.a' \
             --exclude='*.so' \
             --exclude='*.dylib' \
             --exclude='__pycache__' \
             --exclude='*.pyc' \
             --exclude='.pytest_cache' \
             --exclude='*.egg-info' \
             --exclude='.venv' \
             --exclude='venv' \
             . "$TEMP_DIR/$TARBALL_NAME/"
    
    # Create tarball
    cd "$TEMP_DIR"
    tar -czf "$TARBALL_PATH" "$TARBALL_NAME"
    
    # Cleanup
    rm -rf "$TEMP_DIR"
    
    log_info "Created: $TARBALL_PATH"
    echo ""
    echo "Next steps for Homebrew:"
    echo "  brew install --build-from-source $REPO_ROOT/packaging/homebrew/sds.rb"
}

# ============== Install via Homebrew ==============
install_homebrew() {
    log_info "Installing via Homebrew..."
    
    TARBALL_PATH="$HOME/sds-source-$VERSION.tar.gz"
    
    if [ ! -f "$TARBALL_PATH" ]; then
        log_error "Tarball not found: $TARBALL_PATH"
        log_error "Run './packaging/build-package.sh tarball' first"
        exit 1
    fi
    
    # Uninstall previous version if exists
    if brew list sds &>/dev/null; then
        log_info "Uninstalling previous version..."
        brew uninstall sds
    fi
    
    # Install from local formula
    brew install --build-from-source "$SCRIPT_DIR/homebrew/sds.rb"
    
    log_info "Homebrew installation complete!"
    echo ""
    echo "Verify installation:"
    echo "  sds-codegen --help"
    echo "  python3 -c 'import sds; print(sds)'"
    echo "  ls \$(brew --prefix)/share/sds/"
}

# ============== Build Debian Package ==============
build_debian() {
    log_info "Building Debian package..."
    
    if ! command -v debuild &>/dev/null; then
        log_error "debuild not found. Install with: sudo apt-get install devscripts"
        exit 1
    fi
    
    # Copy debian directory to repo root (debuild expects it there)
    if [ -d "$REPO_ROOT/debian" ]; then
        log_warn "Removing existing debian/ directory"
        rm -rf "$REPO_ROOT/debian"
    fi
    
    cp -r "$SCRIPT_DIR/debian" "$REPO_ROOT/"
    
    cd "$REPO_ROOT"
    
    # Build the package
    debuild -us -uc -b
    
    # Move package to packaging directory
    mv ../*.deb "$SCRIPT_DIR/" 2>/dev/null || true
    mv ../*.changes "$SCRIPT_DIR/" 2>/dev/null || true
    mv ../*.buildinfo "$SCRIPT_DIR/" 2>/dev/null || true
    
    # Cleanup
    rm -rf "$REPO_ROOT/debian"
    
    log_info "Debian package built!"
    echo ""
    echo "Install with:"
    echo "  sudo dpkg -i $SCRIPT_DIR/sds_${VERSION}*.deb"
}

# ============== Quick Local Install (for testing) ==============
quick_install() {
    log_info "Quick local install for testing..."
    
    # Build C library
    log_info "Building C library..."
    cmake -B build -DSDS_BUILD_TESTS=OFF -DSDS_BUILD_EXAMPLES=OFF
    cmake --build build
    
    # Install Python SDS bindings (with CFFI compilation)
    log_info "Installing Python SDS bindings..."
    cd "$REPO_ROOT/python"
    pip3 install -e . --user
    cd "$REPO_ROOT"
    
    # Install codegen package
    log_info "Installing codegen..."
    pip3 install -e . --user
    
    # Make sds-codegen available
    log_info "Setting up sds-codegen..."
    mkdir -p ~/.local/bin
    cp "$SCRIPT_DIR/common/sds-codegen" ~/.local/bin/
    
    log_info "Quick install complete!"
    echo ""
    echo "Make sure ~/.local/bin is in your PATH:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
    echo "Test with:"
    echo "  sds-codegen --help"
    echo "  python3 -c 'from sds import SdsNode; print(\"SDS OK\")'"
}

# ============== Show Usage ==============
show_usage() {
    echo "SDS Package Builder"
    echo ""
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  tarball    Create source tarball for Homebrew"
    echo "  homebrew   Install via Homebrew (run tarball first)"
    echo "  debian     Build Debian package"
    echo "  quick      Quick local install for testing"
    echo "  all        Build all packages"
    echo ""
}

# ============== Main ==============
case "${1:-}" in
    tarball)
        create_tarball
        ;;
    homebrew)
        install_homebrew
        ;;
    debian)
        build_debian
        ;;
    quick)
        quick_install
        ;;
    all)
        create_tarball
        # Only try homebrew on macOS
        if [[ "$OSTYPE" == "darwin"* ]]; then
            install_homebrew
        else
            log_info "Skipping Homebrew (not on macOS)"
        fi
        # Only try debian on Linux
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            build_debian
        else
            log_info "Skipping Debian package (not on Linux)"
        fi
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
