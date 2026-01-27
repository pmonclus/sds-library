#!/bin/bash
#
# run_fuzz.sh - Build and run fuzz tests locally
#
# Usage:
#   ./scripts/run_fuzz.sh [json|mqtt|all] [duration_seconds]
#
# Requirements:
#   - clang with libFuzzer support, or
#   - AFL++ (afl-clang-fast)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_fuzz"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Default duration
DURATION=${2:-60}

print_header() {
    echo ""
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}  $1${NC}"
    echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
    echo ""
}

mkdir -p "$BUILD_DIR"

build_libfuzzer() {
    print_header "Building with LibFuzzer (clang)"
    
    local CC="${CC:-clang}"
    
    # Check for libFuzzer support
    if ! $CC -fsanitize=fuzzer -x c -c /dev/null -o /dev/null 2>/dev/null; then
        echo -e "${RED}clang with libFuzzer not found. Install with:${NC}"
        echo "  brew install llvm (macOS)"
        echo "  apt install clang (Linux)"
        return 1
    fi
    
    echo "Building JSON fuzzer..."
    $CC -g -O1 -fsanitize=fuzzer,address \
        -I "$PROJECT_ROOT/include" \
        -DUSE_LIBFUZZER \
        -o "$BUILD_DIR/fuzz_json" \
        "$PROJECT_ROOT/tests/fuzz/fuzz_json_parser.c" \
        "$PROJECT_ROOT/src/sds_json.c" \
        -lm
    
    echo "Building MQTT fuzzer..."
    $CC -g -O1 -fsanitize=fuzzer,address \
        -I "$PROJECT_ROOT/include" -I "$PROJECT_ROOT/tests" \
        -DUSE_LIBFUZZER \
        -o "$BUILD_DIR/fuzz_mqtt" \
        "$PROJECT_ROOT/tests/fuzz/fuzz_mqtt_message.c" \
        "$PROJECT_ROOT/tests/mock/sds_platform_mock.c" \
        "$PROJECT_ROOT/src/sds_core.c" \
        "$PROJECT_ROOT/src/sds_json.c" \
        -lm
    
    echo -e "${GREEN}Build complete!${NC}"
}

run_json_fuzz() {
    print_header "Fuzzing JSON Parser ($DURATION seconds)"
    
    mkdir -p "$BUILD_DIR/corpus_json"
    cp "$PROJECT_ROOT/tests/fuzz/corpus/json/"* "$BUILD_DIR/corpus_json/" 2>/dev/null || true
    
    echo "Starting JSON fuzzer..."
    timeout "$DURATION" "$BUILD_DIR/fuzz_json" "$BUILD_DIR/corpus_json" \
        -max_len=4096 \
        -print_final_stats=1 \
        || true
    
    echo ""
    echo -e "${GREEN}JSON fuzzing completed.${NC}"
}

run_mqtt_fuzz() {
    print_header "Fuzzing MQTT Message Handler ($DURATION seconds)"
    
    mkdir -p "$BUILD_DIR/corpus_mqtt"
    cp "$PROJECT_ROOT/tests/fuzz/corpus/mqtt/"* "$BUILD_DIR/corpus_mqtt/" 2>/dev/null || true
    
    echo "Starting MQTT fuzzer..."
    timeout "$DURATION" "$BUILD_DIR/fuzz_mqtt" "$BUILD_DIR/corpus_mqtt" \
        -max_len=4096 \
        -print_final_stats=1 \
        || true
    
    echo ""
    echo -e "${GREEN}MQTT fuzzing completed.${NC}"
}

# Main
case "${1:-all}" in
    json)
        build_libfuzzer
        run_json_fuzz
        ;;
    mqtt)
        build_libfuzzer
        run_mqtt_fuzz
        ;;
    all)
        build_libfuzzer
        run_json_fuzz
        run_mqtt_fuzz
        ;;
    build)
        build_libfuzzer
        ;;
    *)
        echo "Usage: $0 [json|mqtt|all|build] [duration_seconds]"
        echo ""
        echo "Examples:"
        echo "  $0              # Build and run all fuzzers for 60 seconds"
        echo "  $0 json 300     # Run JSON fuzzer for 5 minutes"
        echo "  $0 build        # Just build fuzzers"
        exit 1
        ;;
esac

print_header "Summary"
echo "Corpus directories:"
echo "  JSON: $BUILD_DIR/corpus_json"
echo "  MQTT: $BUILD_DIR/corpus_mqtt"
echo ""
echo "Check for crash files in $BUILD_DIR"
ls -la "$BUILD_DIR"/crash-* "$BUILD_DIR"/leak-* 2>/dev/null || echo "No crashes found."
