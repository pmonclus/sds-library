#!/bin/bash
#
# run_sanitizers.sh - Run tests with various memory and undefined behavior sanitizers
#
# This script builds and runs the SDS tests with:
#   - AddressSanitizer (ASan) - Buffer overflows, use-after-free, etc.
#   - UndefinedBehaviorSanitizer (UBSan) - Integer overflow, null deref, etc.
#   - Valgrind (optional) - Memory leaks and errors
#
# Usage:
#   ./scripts/run_sanitizers.sh [asan|ubsan|valgrind|all]
#
# Requirements:
#   - clang (for ASan and UBSan)
#   - valgrind (optional, for memory leak detection)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Build directories
BUILD_DIR="$PROJECT_ROOT/build_sanitizers"

# Source files for unit tests (mock platform)
UNIT_TEST_SRCS=(
    "$PROJECT_ROOT/tests/test_unit_core.c"
    "$PROJECT_ROOT/tests/mock/sds_platform_mock.c"
    "$PROJECT_ROOT/src/sds_core.c"
    "$PROJECT_ROOT/src/sds_json.c"
)

# Source files for reconnection tests
RECONNECTION_TEST_SRCS=(
    "$PROJECT_ROOT/tests/test_reconnection.c"
    "$PROJECT_ROOT/tests/mock/sds_platform_mock.c"
    "$PROJECT_ROOT/src/sds_core.c"
    "$PROJECT_ROOT/src/sds_json.c"
)

# Source files for buffer overflow tests
BUFFER_TEST_SRCS=(
    "$PROJECT_ROOT/tests/test_buffer_overflow.c"
    "$PROJECT_ROOT/tests/mock/sds_platform_mock.c"
    "$PROJECT_ROOT/src/sds_core.c"
    "$PROJECT_ROOT/src/sds_json.c"
)

# Source files for concurrent tests
CONCURRENT_TEST_SRCS=(
    "$PROJECT_ROOT/tests/test_concurrent.c"
    "$PROJECT_ROOT/tests/mock/sds_platform_mock.c"
    "$PROJECT_ROOT/src/sds_core.c"
    "$PROJECT_ROOT/src/sds_json.c"
)

# Source files for JSON tests (no platform needed)
JSON_TEST_SRCS=(
    "$PROJECT_ROOT/tests/test_json.c"
    "$PROJECT_ROOT/src/sds_json.c"
)

# Includes
INCLUDES="-I$PROJECT_ROOT/include -I$PROJECT_ROOT/tests"

# Compiler flags
BASE_FLAGS="-g -O1 -Wall -Wextra"

print_header() {
    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}  $1${NC}"
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
    echo ""
}

print_pass() {
    echo -e "${GREEN}✓ $1: PASSED${NC}"
}

print_fail() {
    echo -e "${RED}✗ $1: FAILED${NC}"
}

# Create build directory
mkdir -p "$BUILD_DIR"

run_asan() {
    print_header "AddressSanitizer (ASan)"
    
    local CC="${CC:-clang}"
    local ASAN_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
    
    echo "Building with ASan..."
    
    # Build unit tests
    $CC $BASE_FLAGS $ASAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_unit_asan" \
        "${UNIT_TEST_SRCS[@]}" \
        -lm
    
    # Build reconnection tests
    $CC $BASE_FLAGS $ASAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_reconnection_asan" \
        "${RECONNECTION_TEST_SRCS[@]}" \
        -lm
    
    # Build buffer overflow tests
    $CC $BASE_FLAGS $ASAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_buffer_asan" \
        "${BUFFER_TEST_SRCS[@]}" \
        -lm
    
    # Build JSON tests
    $CC $BASE_FLAGS $ASAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_json_asan" \
        "${JSON_TEST_SRCS[@]}" \
        -lm
    
    echo "Running unit tests with ASan..."
    # detect_leaks is not supported on macOS, so we conditionally enable it
    if [[ "$(uname)" == "Darwin" ]]; then
        export ASAN_OPTIONS="halt_on_error=1:print_stats=1"
    else
        export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:print_stats=1"
    fi
    
    if "$BUILD_DIR/test_unit_asan"; then
        print_pass "Unit tests (ASan)"
    else
        print_fail "Unit tests (ASan)"
        return 1
    fi
    
    echo ""
    echo "Running reconnection tests with ASan..."
    if "$BUILD_DIR/test_reconnection_asan"; then
        print_pass "Reconnection tests (ASan)"
    else
        print_fail "Reconnection tests (ASan)"
        return 1
    fi
    
    echo ""
    echo "Running buffer overflow tests with ASan..."
    if "$BUILD_DIR/test_buffer_asan"; then
        print_pass "Buffer overflow tests (ASan)"
    else
        print_fail "Buffer overflow tests (ASan)"
        return 1
    fi
    
    echo ""
    echo "Running JSON tests with ASan..."
    if "$BUILD_DIR/test_json_asan"; then
        print_pass "JSON tests (ASan)"
    else
        print_fail "JSON tests (ASan)"
        return 1
    fi
    
    return 0
}

run_ubsan() {
    print_header "UndefinedBehaviorSanitizer (UBSan)"
    
    local CC="${CC:-clang}"
    local UBSAN_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer"
    
    echo "Building with UBSan..."
    
    # Build unit tests
    $CC $BASE_FLAGS $UBSAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_unit_ubsan" \
        "${UNIT_TEST_SRCS[@]}" \
        -lm
    
    # Build reconnection tests
    $CC $BASE_FLAGS $UBSAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_reconnection_ubsan" \
        "${RECONNECTION_TEST_SRCS[@]}" \
        -lm
    
    # Build buffer overflow tests
    $CC $BASE_FLAGS $UBSAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_buffer_ubsan" \
        "${BUFFER_TEST_SRCS[@]}" \
        -lm
    
    # Build JSON tests
    $CC $BASE_FLAGS $UBSAN_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_json_ubsan" \
        "${JSON_TEST_SRCS[@]}" \
        -lm
    
    echo "Running unit tests with UBSan..."
    export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
    
    if "$BUILD_DIR/test_unit_ubsan"; then
        print_pass "Unit tests (UBSan)"
    else
        print_fail "Unit tests (UBSan)"
        return 1
    fi
    
    echo ""
    echo "Running reconnection tests with UBSan..."
    if "$BUILD_DIR/test_reconnection_ubsan"; then
        print_pass "Reconnection tests (UBSan)"
    else
        print_fail "Reconnection tests (UBSan)"
        return 1
    fi
    
    echo ""
    echo "Running buffer overflow tests with UBSan..."
    if "$BUILD_DIR/test_buffer_ubsan"; then
        print_pass "Buffer overflow tests (UBSan)"
    else
        print_fail "Buffer overflow tests (UBSan)"
        return 1
    fi
    
    echo ""
    echo "Running JSON tests with UBSan..."
    if "$BUILD_DIR/test_json_ubsan"; then
        print_pass "JSON tests (UBSan)"
    else
        print_fail "JSON tests (UBSan)"
        return 1
    fi
    
    return 0
}

run_valgrind() {
    print_header "Valgrind Memory Check"
    
    # Check if valgrind is available
    if ! command -v valgrind &> /dev/null; then
        echo -e "${YELLOW}Valgrind not found, skipping...${NC}"
        echo "Install with: brew install valgrind (macOS) or apt install valgrind (Linux)"
        return 0
    fi
    
    local CC="${CC:-gcc}"
    local VALGRIND_FLAGS="-g -O0"
    
    echo "Building for Valgrind..."
    
    # Build unit tests
    $CC $VALGRIND_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_unit_valgrind" \
        "${UNIT_TEST_SRCS[@]}" \
        -lm
    
    # Build reconnection tests
    $CC $VALGRIND_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_reconnection_valgrind" \
        "${RECONNECTION_TEST_SRCS[@]}" \
        -lm
    
    # Build buffer overflow tests
    $CC $VALGRIND_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_buffer_valgrind" \
        "${BUFFER_TEST_SRCS[@]}" \
        -lm
    
    # Build JSON tests
    $CC $VALGRIND_FLAGS $INCLUDES \
        -o "$BUILD_DIR/test_json_valgrind" \
        "${JSON_TEST_SRCS[@]}" \
        -lm
    
    echo "Running unit tests with Valgrind..."
    if valgrind --leak-check=full \
                --show-leak-kinds=all \
                --error-exitcode=1 \
                --track-origins=yes \
                --quiet \
                "$BUILD_DIR/test_unit_valgrind"; then
        print_pass "Unit tests (Valgrind)"
    else
        print_fail "Unit tests (Valgrind)"
        return 1
    fi
    
    echo ""
    echo "Running reconnection tests with Valgrind..."
    if valgrind --leak-check=full \
                --show-leak-kinds=all \
                --error-exitcode=1 \
                --track-origins=yes \
                --quiet \
                "$BUILD_DIR/test_reconnection_valgrind"; then
        print_pass "Reconnection tests (Valgrind)"
    else
        print_fail "Reconnection tests (Valgrind)"
        return 1
    fi
    
    echo ""
    echo "Running buffer overflow tests with Valgrind..."
    if valgrind --leak-check=full \
                --show-leak-kinds=all \
                --error-exitcode=1 \
                --track-origins=yes \
                --quiet \
                "$BUILD_DIR/test_buffer_valgrind"; then
        print_pass "Buffer overflow tests (Valgrind)"
    else
        print_fail "Buffer overflow tests (Valgrind)"
        return 1
    fi
    
    echo ""
    echo "Running JSON tests with Valgrind..."
    if valgrind --leak-check=full \
                --show-leak-kinds=all \
                --error-exitcode=1 \
                --track-origins=yes \
                --quiet \
                "$BUILD_DIR/test_json_valgrind"; then
        print_pass "JSON tests (Valgrind)"
    else
        print_fail "JSON tests (Valgrind)"
        return 1
    fi
    
    return 0
}

run_all() {
    local failed=0
    
    run_asan || failed=1
    run_ubsan || failed=1
    run_valgrind || failed=1
    
    return $failed
}

# Main
case "${1:-all}" in
    asan)
        run_asan
        ;;
    ubsan)
        run_ubsan
        ;;
    valgrind)
        run_valgrind
        ;;
    all)
        run_all
        ;;
    *)
        echo "Usage: $0 [asan|ubsan|valgrind|all]"
        exit 1
        ;;
esac

result=$?

print_header "Summary"

if [ $result -eq 0 ]; then
    echo -e "${GREEN}All sanitizer tests passed!${NC}"
else
    echo -e "${RED}Some sanitizer tests failed!${NC}"
fi

exit $result
