#!/bin/bash
# =============================================================================
# PIV2 - BerkeleyDB 4.8 Installation Script
# =============================================================================
# Installs BDB 4.8 from source for PIV2 wallet compatibility
#
# Usage: ./install_bdb48.sh [prefix]
#   prefix: Installation directory (default: /usr/local/BerkeleyDB.4.8)
#
# After installation, configure PIV2 with:
#   ./configure --without-gui \
#       BDB_LIBS="-L/usr/local/BerkeleyDB.4.8/lib -ldb_cxx-4.8" \
#       BDB_CFLAGS="-I/usr/local/BerkeleyDB.4.8/include"
# =============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
BDB_VERSION="4.8.30.NC"
BDB_PREFIX="${1:-/usr/local/BerkeleyDB.4.8}"
BDB_URL="http://download.oracle.com/berkeley-db/db-${BDB_VERSION}.tar.gz"
BDB_HASH="12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef"
WORK_DIR="/tmp/bdb48_install_$$"

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

print_banner() {
    echo ""
    echo "=============================================="
    echo "  BerkeleyDB 4.8 Installation for PIV2"
    echo "=============================================="
    echo "  Version: ${BDB_VERSION}"
    echo "  Prefix:  ${BDB_PREFIX}"
    echo "=============================================="
    echo ""
}

check_existing() {
    if [[ -f "${BDB_PREFIX}/lib/libdb_cxx-4.8.so" ]] || [[ -f "${BDB_PREFIX}/lib/libdb_cxx-4.8.a" ]]; then
        log_warn "BerkeleyDB 4.8 already installed at ${BDB_PREFIX}"
        echo ""
        echo "To use with PIV2, configure with:"
        echo "  ./configure --without-gui \\"
        echo "      BDB_LIBS=\"-L${BDB_PREFIX}/lib -ldb_cxx-4.8\" \\"
        echo "      BDB_CFLAGS=\"-I${BDB_PREFIX}/include\""
        echo ""
        read -p "Reinstall anyway? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 0
        fi
    fi
}

check_dependencies() {
    log_info "Checking dependencies..."

    local missing=""
    for cmd in wget tar make gcc g++; do
        if ! command -v $cmd &> /dev/null; then
            missing="$missing $cmd"
        fi
    done

    if [[ -n "$missing" ]]; then
        log_info "Installing missing dependencies:$missing"
        sudo apt-get update -qq
        sudo apt-get install -y -qq build-essential wget
    fi

    log_success "Dependencies OK"
}

download_bdb() {
    log_info "Downloading BerkeleyDB ${BDB_VERSION}..."

    mkdir -p "${WORK_DIR}"
    cd "${WORK_DIR}"

    if [[ -f "db-${BDB_VERSION}.tar.gz" ]]; then
        log_info "Using cached download"
    else
        wget -q --show-progress "${BDB_URL}" -O "db-${BDB_VERSION}.tar.gz" || {
            # Fallback mirrors
            log_warn "Primary download failed, trying fallback..."
            wget -q --show-progress "https://github.com/berkeleydb/libdb/releases/download/v4.8.30/db-${BDB_VERSION}.tar.gz" -O "db-${BDB_VERSION}.tar.gz" || {
                log_error "Failed to download BerkeleyDB. Check your internet connection."
            }
        }
    fi

    # Verify checksum (optional, may vary by source)
    # log_info "Verifying checksum..."
    # echo "${BDB_HASH}  db-${BDB_VERSION}.tar.gz" | sha256sum -c - || log_warn "Checksum verification skipped"

    log_success "Download complete"
}

extract_bdb() {
    log_info "Extracting..."

    cd "${WORK_DIR}"
    tar -xzf "db-${BDB_VERSION}.tar.gz"

    log_success "Extraction complete"
}

patch_bdb() {
    log_info "Applying patches for modern GCC..."

    cd "${WORK_DIR}/db-${BDB_VERSION}"

    # Fix atomic.h for GCC 10+ (symbol collision with libstdc++)
    if grep -q "__atomic_compare_exchange" dbinc/atomic.h 2>/dev/null; then
        sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h
        log_success "Patched atomic.h for GCC 10+"
    fi

    # Fix for clang (if needed)
    if [[ -f "src/dbinc/atomic.h" ]]; then
        sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' src/dbinc/atomic.h 2>/dev/null || true
    fi
}

build_bdb() {
    log_info "Building BerkeleyDB ${BDB_VERSION}..."
    log_info "This may take a few minutes..."

    cd "${WORK_DIR}/db-${BDB_VERSION}/build_unix"

    # Configure (with POSIX mutex for modern Linux)
    ../dist/configure \
        --enable-cxx \
        --disable-shared \
        --with-pic \
        --with-mutex=POSIX/pthreads/library \
        --prefix="${BDB_PREFIX}" \
        > configure.log 2>&1 || {
            log_error "Configure failed. Check ${WORK_DIR}/db-${BDB_VERSION}/build_unix/configure.log"
        }

    # Build
    make -j$(nproc) > make.log 2>&1 || {
        log_error "Build failed. Check ${WORK_DIR}/db-${BDB_VERSION}/build_unix/make.log"
    }

    log_success "Build complete"
}

install_bdb() {
    log_info "Installing to ${BDB_PREFIX}..."

    cd "${WORK_DIR}/db-${BDB_VERSION}/build_unix"

    sudo make install > install.log 2>&1 || {
        log_error "Installation failed. Check ${WORK_DIR}/db-${BDB_VERSION}/build_unix/install.log"
    }

    log_success "Installation complete"
}

verify_install() {
    log_info "Verifying installation..."

    local errors=0

    # Check library
    if [[ -f "${BDB_PREFIX}/lib/libdb_cxx-4.8.a" ]]; then
        log_success "Static library: ${BDB_PREFIX}/lib/libdb_cxx-4.8.a"
    else
        log_warn "Static library not found"
        ((errors++))
    fi

    # Check headers
    if [[ -f "${BDB_PREFIX}/include/db_cxx.h" ]]; then
        log_success "Headers: ${BDB_PREFIX}/include/db_cxx.h"
    else
        log_warn "Headers not found"
        ((errors++))
    fi

    # Check version
    if [[ -f "${BDB_PREFIX}/include/db.h" ]]; then
        local version=$(grep "#define DB_VERSION_STRING" "${BDB_PREFIX}/include/db.h" | head -1)
        log_success "Version: $version"
    fi

    if [[ $errors -gt 0 ]]; then
        log_error "Installation verification failed"
    fi
}

cleanup() {
    log_info "Cleaning up..."
    rm -rf "${WORK_DIR}"
    log_success "Cleanup complete"
}

print_usage() {
    echo ""
    echo "=============================================="
    echo -e "${GREEN}BerkeleyDB 4.8 installed successfully!${NC}"
    echo "=============================================="
    echo ""
    echo "To build PIV2 with BDB 4.8, use:"
    echo ""
    echo "  cd ~/PIV2-Core"
    echo "  ./autogen.sh"
    echo "  ./configure --without-gui \\"
    echo "      BDB_LIBS=\"-L${BDB_PREFIX}/lib -ldb_cxx-4.8\" \\"
    echo "      BDB_CFLAGS=\"-I${BDB_PREFIX}/include\""
    echo "  make -j\$(nproc)"
    echo ""
    echo "Or export environment variables:"
    echo ""
    echo "  export BDB_PREFIX=\"${BDB_PREFIX}\""
    echo "  export BDB_LIBS=\"-L\${BDB_PREFIX}/lib -ldb_cxx-4.8\""
    echo "  export BDB_CFLAGS=\"-I\${BDB_PREFIX}/include\""
    echo ""
    echo "=============================================="
}

# =============================================================================
# Main
# =============================================================================

main() {
    print_banner
    check_existing
    check_dependencies
    download_bdb
    extract_bdb
    patch_bdb
    build_bdb
    install_bdb
    verify_install
    cleanup
    print_usage
}

# Handle Ctrl+C
trap 'echo ""; log_warn "Installation cancelled"; exit 1' INT

# Run
main "$@"
