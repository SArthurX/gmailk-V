#!/bin/bash

set -e

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"
TOOLCHAIN_FILE="${SCRIPT_DIR}/tools/riscv-toolchain.cmake"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

function print_info()    { printf "${GREEN}%s${NC}\n" "$1"; }
function print_warning() { printf "${YELLOW}%s${NC}\n" "$1"; }
function print_error()   { printf "${RED}%s${NC}\n" "$1"; }
function print_header()  { printf "${BLUE}%s${NC}\n" "$1"; }

# Default settings
CHIP="CV181X"
BUILD_TYPE="Release"
CLEAN_BUILD=0
VERBOSE=0
JOBS=$(nproc)
BUILD_TEST=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN_BUILD=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -t|--test)
            BUILD_TEST=1
            shift
            ;;
        --chip)
            CHIP="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            print_header "gmailk-V Build Script"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -c, --clean           Clean build (remove build directory)"
            echo "  -v, --verbose         Verbose build output"
            echo "  -d, --debug           Debug build (default: Release)"
            echo "  -r, --release         Release build"
            echo "  -t, --test            Build test directory"
            echo "  --chip <CHIP>         Target chip: CV181X (default) or CV180X"
            echo "  -j, --jobs <N>        Number of parallel jobs (default: $(nproc))"
            echo "  -h, --help            Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Build with default settings"
            echo "  $0 --clean            # Clean and build"
            echo "  $0 --chip CV180X      # Build for CV180X chip"
            echo "  $0 --debug -v         # Debug build with verbose output"
            echo "  $0 -t                 # Build test directory"
            echo "  $0 -t -c              # Clean and build test directory"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

# Validate chip
if [[ "$CHIP" != "CV181X" && "$CHIP" != "CV180X" ]]; then
    print_error "Invalid CHIP: $CHIP (must be CV181X or CV180X)"
    exit 1
fi

# Set build directory based on test flag
if [ $BUILD_TEST -eq 1 ]; then
    BUILD_DIR="${SCRIPT_DIR}/test/build"
    SOURCE_DIR="${SCRIPT_DIR}/test"
    PROJECT_NAME="Test"
    OUTPUT_NAME="test"
else
    BUILD_DIR="${SCRIPT_DIR}/build"
    SOURCE_DIR="${SCRIPT_DIR}"
    PROJECT_NAME="Main"
    OUTPUT_NAME="main"
fi

print_header "╔════════════════════════════════════════════╗"
print_header "║        gmailk-V CMake Build System         ║"
print_header "╚════════════════════════════════════════════╝"
echo ""
print_info "Configuration:"
echo "  • Target:     $PROJECT_NAME Project"
echo "  • Chip:       $CHIP"
echo "  • Build Type: $BUILD_TYPE"
echo "  • Jobs:       $JOBS"
echo "  • Toolchain:  $TOOLCHAIN_FILE"
echo ""

# Clean build if requested
if [ $CLEAN_BUILD -eq 1 ]; then
    print_warning "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
print_info "Configuring project with CMake..."
CMAKE_ARGS=(
    "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
    "-DCHIP=${CHIP}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
)

if [ $VERBOSE -eq 1 ]; then
    CMAKE_ARGS+=("-DCMAKE_VERBOSE_MAKEFILE=ON")
fi

cmake "$SOURCE_DIR" "${CMAKE_ARGS[@]}"

if [ $? -ne 0 ]; then
    print_error "CMake configuration failed!"
    exit 1
fi

echo ""
print_info "Building project..."

# Build
if [ $VERBOSE -eq 1 ]; then
    make VERBOSE=1 -j${JOBS}
else
    make -j${JOBS}
fi

# Check if build was successful
if [ $? -eq 0 ]; then
    echo ""
    print_header "╔════════════════════════════════════════════╗"
    print_header "║          Build Successful! ✓               ║"
    print_header "╚════════════════════════════════════════════╝"
    echo ""
    print_info "Executable: ${BUILD_DIR}/${OUTPUT_NAME}"
    
    # Show file info
    if [ -f "${BUILD_DIR}/${OUTPUT_NAME}" ]; then
        FILE_SIZE=$(du -h "${BUILD_DIR}/${OUTPUT_NAME}" | cut -f1)
        echo "  • Size: $FILE_SIZE"
        echo "  • Type: $(file -b "${BUILD_DIR}/${OUTPUT_NAME}" | head -n1)"
    fi
    echo ""
else
    echo ""
    print_error "╔════════════════════════════════════════════╗"
    print_error "║            Build Failed! ✗                 ║"
    print_error "╚════════════════════════════════════════════╝"
    exit 1
fi
