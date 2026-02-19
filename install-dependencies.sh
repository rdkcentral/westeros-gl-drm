#!/bin/bash

#
# Install dependencies script for westeros-gl-drm component
# This script installs all prerequisites required before building the project
# All external files are fetched into ./build/ directory to keep the repo clean
#

set -e  # Exit on error

# Color definitions for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored messages
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if running on Linux
check_platform() {
    if [[ "$OSTYPE" != "linux-gnu"* ]]; then
        print_error "This script must be run on a Linux system"
        exit 1
    fi
    print_info "Platform check: Linux detected"
}

# Function to detect package manager
detect_package_manager() {
    if command -v apt-get &> /dev/null; then
        PKG_MANAGER="apt"
        UPDATE_CMD="sudo apt-get update"
        INSTALL_CMD="sudo apt-get install -y"
    elif command -v dnf &> /dev/null; then
        PKG_MANAGER="dnf"
        UPDATE_CMD="sudo dnf check-update || true"
        INSTALL_CMD="sudo dnf install -y"
    elif command -v yum &> /dev/null; then
        PKG_MANAGER="yum"
        UPDATE_CMD="sudo yum check-update || true"
        INSTALL_CMD="sudo yum install -y"
    elif command -v pacman &> /dev/null; then
        PKG_MANAGER="pacman"
        UPDATE_CMD="sudo pacman -Sy"
        INSTALL_CMD="sudo pacman -S --noconfirm"
    else
        print_error "No supported package manager found (apt, dnf, yum, pacman)"
        exit 1
    fi
    print_info "Detected package manager: $PKG_MANAGER"
}

# Function to install build dependencies
install_dependencies() {
    print_info "Installing build dependencies..."
    
    # Update package lists
    print_info "Updating package lists..."
    eval $UPDATE_CMD
    
    # Common build tools
    print_info "Installing build essentials..."
    case $PKG_MANAGER in
        apt)
            $INSTALL_CMD \
                build-essential \
                autoconf \
                automake \
                libtool \
                pkg-config \
                m4 \
                gcc \
                g++ \
                make \
                git
            ;;
        dnf|yum)
            $INSTALL_CMD \
                gcc \
                gcc-c++ \
                autoconf \
                automake \
                libtool \
                pkgconfig \
                m4 \
                make \
                git
            ;;
        pacman)
            $INSTALL_CMD \
                base-devel \
                autoconf \
                automake \
                libtool \
                pkgconfig \
                m4 \
                git
            ;;
    esac
    
    # Install library dependencies
    print_info "Installing library dependencies..."
    case $PKG_MANAGER in
        apt)
            $INSTALL_CMD \
                libglib2.0-dev \
                libwayland-dev \
                wayland-protocols \
                libgbm-dev \
                libdrm-dev \
                libegl1-mesa-dev \
                libgles2-mesa-dev \
                mesa-common-dev
            ;;
        dnf|yum)
            $INSTALL_CMD \
                glib2-devel \
                wayland-devel \
                wayland-protocols-devel \
                mesa-libgbm-devel \
                libdrm-devel \
                mesa-libEGL-devel \
                mesa-libGLES-devel
            ;;
        pacman)
            $INSTALL_CMD \
                glib2 \
                wayland \
                wayland-protocols \
                mesa \
                libdrm
            ;;
    esac
    
    print_info "Dependencies installed successfully"
}

# Function to check required dependencies
check_dependencies() {
    print_info "Checking for required dependencies..."
    
    local missing_deps=()
    
    # Check for pkg-config packages (matching build.sh approach)
    local required_pkgs=(
        "glib-2.0"
        "gthread-2.0"
        "wayland-client"
        "wayland-server"
        "gbm"
        "libdrm"
    )
    
    for pkg in "${required_pkgs[@]}"; do
        if ! pkg-config --exists "$pkg" 2>/dev/null; then
            missing_deps+=("$pkg")
        fi
    done
    
    # Check for optional EGL
    if pkg-config --exists egl 2>/dev/null; then
        print_info "EGL support: Available"
    else
        print_warn "EGL support: Not found (optional)"
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing required dependencies: ${missing_deps[*]}"
        print_info "Attempting to install missing dependencies..."
        install_dependencies
        
        # Re-check packages after installation
        local still_missing=()
        for pkg in "${missing_deps[@]}"; do
            if ! pkg-config --exists "$pkg" 2>/dev/null; then
                still_missing+=("$pkg")
            fi
        done
        
        if [ ${#still_missing[@]} -ne 0 ]; then
            print_error "Failed to install dependencies: ${still_missing[*]}"
            exit 1
        fi
        print_info "All dependencies successfully installed"
    else
        print_info "All required dependencies are satisfied"
    fi
}

# Function to fetch westeros headers from repo
# Headers are fetched into build/external/westeros to keep main repo clean
fetch_westeros_header() {
    local WESTEROS_DIR="$BUILD_DIR/external/westeros"
    local REPO_WESTEROS_DIR="$SCRIPT_DIR/external/westeros"
    local HEADER_FILES=(
        "westeros-compositor.h"
        "westeros-render.h"
    )
    
    # First check if headers already exist in the repo (original location)
    local all_exist_in_repo=true
    for HEADER_FILE in "${HEADER_FILES[@]}"; do
        if [ ! -f "$REPO_WESTEROS_DIR/$HEADER_FILE" ]; then
            all_exist_in_repo=false
            break
        fi
    done
    
    if [ "$all_exist_in_repo" = true ]; then
        print_info "Westeros headers already exist in repository at: external/westeros/"
        print_info "No need to fetch - using existing headers"
        return 0
    fi
    
    # Create build/external directory structure
    mkdir -p "$WESTEROS_DIR"
    
    # Check if headers already exist in build directory
    local all_exist=true
    for HEADER_FILE in "${HEADER_FILES[@]}"; do
        if [ ! -f "$WESTEROS_DIR/$HEADER_FILE" ]; then
            all_exist=false
            break
        fi
    done
    
    if [ "$all_exist" = true ]; then
        print_info "All westeros headers already exist in $WESTEROS_DIR"
        return 0
    fi
    
    print_info "Fetching westeros headers from repository..."
    print_info "Headers will be stored in: $WESTEROS_DIR"
    
    # Create temporary directory
    local TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"
    
    # Try RDK Central repo first, then GitHub as fallback
    local REPOS=(
        "https://code.rdkcentral.com/r/components/opensource/westeros"
        "https://github.com/rdkcentral/westeros.git"
    )
    
    local SUCCESS=false
    for REPO_URL in "${REPOS[@]}"; do
        print_info "Trying repository: $REPO_URL"
        
        git init > /dev/null 2>&1
        git remote add origin "$REPO_URL" > /dev/null 2>&1
        
        if git pull --depth=1 origin master > /dev/null 2>&1 || git pull --depth=1 origin main > /dev/null 2>&1; then
            local files_found=0
            for HEADER_FILE in "${HEADER_FILES[@]}"; do
                if [ -f "$HEADER_FILE" ]; then
                    cp "$HEADER_FILE" "$WESTEROS_DIR/"
                    print_info "Successfully fetched $HEADER_FILE"
                    ((files_found++))
                else
                    print_warn "$HEADER_FILE not found in repository"
                fi
            done
            
            if [ $files_found -gt 0 ]; then
                SUCCESS=true
                break
            fi
        fi
        
        # Clean up for next attempt
        rm -rf .git
    done
    
    cd "$SCRIPT_DIR"
    rm -rf "$TEMP_DIR"
    
    if [ "$SUCCESS" = true ]; then
        print_info "Westeros headers successfully fetched to: $WESTEROS_DIR"
        return 0
    else
        print_error "Failed to fetch westeros headers from any repository"
        print_info "Please manually place the files in: $WESTEROS_DIR/"
        for HEADER_FILE in "${HEADER_FILES[@]}"; do
            echo "  - $HEADER_FILE"
        done
        return 1
    fi
}

# Main execution
main() {
    echo ""
    print_info "======================================"
    print_info "Westeros GL DRM - Install Dependencies"
    print_info "======================================"
    echo ""
    
    # Get script directory
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "$SCRIPT_DIR"
    
    # Set build directory (all external files go here)
    BUILD_DIR="$SCRIPT_DIR/build"
    
    # Parse command line arguments
    FORCE_INSTALL=false
    SKIP_FETCH=false
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            --force)
                FORCE_INSTALL=true
                shift
                ;;
            --skip-fetch)
                SKIP_FETCH=true
                shift
                ;;
            --help)
                echo "Usage: $0 [OPTIONS]"
                echo ""
                echo "Options:"
                echo "  --force          Force reinstall all dependencies"
                echo "  --skip-fetch     Skip fetching westeros headers"
                echo "  --help           Show this help message"
                echo ""
                echo "This script installs all prerequisites for building westeros-gl-drm."
                echo "External files are fetched into: ./build/external/"
                echo ""
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
    
    # Check platform
    check_platform
    
    # Detect package manager
    detect_package_manager
    
    # Install dependencies (or check if already installed)
    if [ "$FORCE_INSTALL" = true ]; then
        install_dependencies
    else
        check_dependencies
    fi
    
    # Fetch westeros headers into build directory
    if [ "$SKIP_FETCH" = false ]; then
        if ! fetch_westeros_header; then
            print_error "Failed to fetch required westeros headers"
            exit 1
        fi
    else
        print_warn "Skipping westeros header fetch (--skip-fetch flag set)"
    fi
    
    echo ""
    print_info "======================================"
    print_info "Dependencies Installation Completed!"
    print_info "======================================"
    echo ""
    
    print_info "Next steps:"
    echo "  1. Run build-dependencies.sh to build the project"
    echo "  2. Build artifacts will be created in: $BUILD_DIR"
    echo ""
}

# Run main function
main "$@"
