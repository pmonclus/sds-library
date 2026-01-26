#!/bin/bash
#
# SDS Library - Automated Test Runner
#
# Usage:
#   ./run_tests.sh           # Run all tests
#   ./run_tests.sh --quick   # Skip multi-node integration tests
#   ./run_tests.sh --verbose # Show full test output
#   ./run_tests.sh --help    # Show help
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build"
BROKER_HOST="localhost"
BROKER_PORT="1883"
MULTI_NODE_DURATION=16  # seconds to wait for multi-node tests

# Options
QUICK_MODE=false
VERBOSE=false

# Test results
TESTS_PASSED=0
TESTS_FAILED=0
FAILED_TESTS=""

# Parse arguments
for arg in "$@"; do
    case $arg in
        --quick|-q)
            QUICK_MODE=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "SDS Library Test Runner"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --quick, -q     Skip multi-node integration tests"
            echo "  --verbose, -v   Show full test output"
            echo "  --help, -h      Show this help message"
            echo ""
            echo "Exit codes:"
            echo "  0  All tests passed"
            echo "  1  One or more tests failed"
            echo "  2  Build failed"
            echo "  3  MQTT broker not running"
            exit 0
            ;;
    esac
done

# Helper functions
print_header() {
    echo ""
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_test_start() {
    echo -e "\n${YELLOW}▶ Running: $1${NC}"
}

print_pass() {
    echo -e "${GREEN}✓ PASSED: $1${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

print_fail() {
    echo -e "${RED}✗ FAILED: $1${NC}"
    TESTS_FAILED=$((TESTS_FAILED + 1))
    FAILED_TESTS="$FAILED_TESTS\n  - $1"
}

check_mqtt_broker() {
    echo -n "Checking MQTT broker at $BROKER_HOST:$BROKER_PORT... "
    if nc -z "$BROKER_HOST" "$BROKER_PORT" 2>/dev/null; then
        echo -e "${GREEN}OK${NC}"
        return 0
    else
        echo -e "${RED}NOT RUNNING${NC}"
        echo ""
        echo "Please start an MQTT broker before running tests:"
        echo "  macOS:  brew services start mosquitto"
        echo "  Linux:  sudo systemctl start mosquitto"
        exit 3
    fi
}

build_project() {
    print_header "Building Project"
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    if $VERBOSE; then
        cmake .. && make
    else
        cmake .. > /dev/null && make -j4 > /dev/null 2>&1
    fi
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}Build failed!${NC}"
        exit 2
    fi
    
    echo -e "${GREEN}Build successful${NC}"
    cd ..
}

run_single_test() {
    local test_name=$1
    local test_binary=$2
    local description=$3
    
    print_test_start "$test_name - $description"
    
    local output
    local exit_code
    
    output=$("$BUILD_DIR/$test_binary" "$BROKER_HOST" 2>&1) || exit_code=$?
    exit_code=${exit_code:-0}
    
    if $VERBOSE; then
        echo "$output"
    fi
    
    if [ $exit_code -eq 0 ]; then
        # Check for specific pass indicators in output
        if echo "$output" | grep -q "passed, 0 failed\|PASSED\|Results:.*passed"; then
            print_pass "$test_name"
            return 0
        fi
    fi
    
    # Test failed - show output if not verbose
    if ! $VERBOSE; then
        echo "$output" | tail -20
    fi
    print_fail "$test_name"
    return 1
}

run_multi_node_test() {
    local test_name=$1
    local test_binary=$2
    local description=$3
    
    print_test_start "$test_name - $description"
    
    local tmpdir
    tmpdir=$(mktemp -d)
    
    # Start owners first (node1=TableA owner, node2=TableB owner)
    # They need to subscribe before devices start publishing
    echo "  Starting owners first (node1, node2)..."
    "$BUILD_DIR/$test_binary" node1 "$BROKER_HOST" > "$tmpdir/node1.log" 2>&1 &
    local pid1=$!
    
    "$BUILD_DIR/$test_binary" node2 "$BROKER_HOST" > "$tmpdir/node2.log" 2>&1 &
    local pid2=$!
    
    # Give owners time to connect and subscribe
    sleep 2
    
    # Now start pure device (node3=both device)
    echo "  Starting device (node3)..."
    "$BUILD_DIR/$test_binary" node3 "$BROKER_HOST" > "$tmpdir/node3.log" 2>&1 &
    local pid3=$!
    
    echo "  Started node1 (PID: $pid1), node2 (PID: $pid2), node3 (PID: $pid3)"
    echo "  Waiting ${MULTI_NODE_DURATION}s for test completion..."
    
    # Wait for tests to complete (adjusted for staggered start)
    sleep "$MULTI_NODE_DURATION"
    
    # Send graceful termination signal (SIGTERM) to allow cleanup
    for pid in $pid1 $pid2 $pid3; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    
    # Give processes time to flush and shutdown gracefully
    sleep 2
    
    # Force kill any remaining processes
    for pid in $pid1 $pid2 $pid3; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    done
    
    # Wait for processes to terminate
    wait $pid1 2>/dev/null || true
    wait $pid2 2>/dev/null || true
    wait $pid3 2>/dev/null || true
    
    # Check results
    local all_passed=true
    local node_results=""
    
    for node in node1 node2 node3; do
        local logfile="$tmpdir/$node.log"
        if [ -f "$logfile" ]; then
            if grep -q "Overall: PASSED" "$logfile"; then
                node_results="$node_results  ${GREEN}✓${NC} $node: PASSED\n"
            else
                node_results="$node_results  ${RED}✗${NC} $node: FAILED\n"
                all_passed=false
                if $VERBOSE; then
                    echo "--- $node output ---"
                    cat "$logfile"
                    echo "---"
                fi
            fi
        else
            node_results="$node_results  ${RED}✗${NC} $node: NO OUTPUT\n"
            all_passed=false
        fi
    done
    
    echo -e "$node_results"
    
    # Cleanup
    rm -rf "$tmpdir"
    
    if $all_passed; then
        print_pass "$test_name"
        return 0
    else
        print_fail "$test_name"
        return 1
    fi
}

# Main execution
print_header "SDS Library Test Suite"

echo "Configuration:"
echo "  Build directory: $BUILD_DIR"
echo "  MQTT broker: $BROKER_HOST:$BROKER_PORT"
echo "  Quick mode: $QUICK_MODE"
echo "  Verbose: $VERBOSE"

# Pre-flight checks
check_mqtt_broker

# Build
build_project

# Run unit tests
print_header "Unit Tests"

run_single_test "test_json" "test_json" "JSON serialization/parsing" || true
run_single_test "test_sds_basic" "test_sds_basic" "Core API functionality" || true
run_single_test "test_simple_api" "test_simple_api" "Simple registration API" || true

# Run integration tests (unless quick mode)
if ! $QUICK_MODE; then
    print_header "Integration Tests (Multi-Node)"
    
    run_multi_node_test "test_generated" "test_generated" "Generated types integration" || true
    run_multi_node_test "test_multi_node" "test_multi_node" "Multi-node communication" || true
else
    echo ""
    echo -e "${YELLOW}Skipping multi-node integration tests (--quick mode)${NC}"
fi

# Summary
print_header "Test Summary"

TOTAL_TESTS=$((TESTS_PASSED + TESTS_FAILED))

echo ""
echo "Results:"
echo -e "  ${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "  ${RED}Failed: $TESTS_FAILED${NC}"
echo "  Total:  $TOTAL_TESTS"

if [ $TESTS_FAILED -gt 0 ]; then
    echo ""
    echo -e "${RED}Failed tests:${NC}"
    echo -e "$FAILED_TESTS"
    echo ""
    echo -e "${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${RED}  TESTS FAILED${NC}"
    echo -e "${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    exit 1
else
    echo ""
    echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}  ALL TESTS PASSED${NC}"
    echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    exit 0
fi

