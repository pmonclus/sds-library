#!/bin/bash
#
# run_scale_test.sh - Launch scale test with owner and multiple devices
#
# Usage:
#   ./run_scale_test.sh [num_devices] [duration_seconds] [broker_ip]
#
# Default: 25 devices, 30 seconds, localhost
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# Parameters
NUM_DEVICES=${1:-25}
DURATION=${2:-30}
BROKER=${3:-localhost}

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    
    # Kill all device processes
    if [ -n "$DEVICE_PIDS" ]; then
        for pid in $DEVICE_PIDS; do
            kill $pid 2>/dev/null || true
        done
    fi
    
    # Kill owner process
    if [ -n "$OWNER_PID" ]; then
        kill $OWNER_PID 2>/dev/null || true
    fi
    
    wait 2>/dev/null || true
    echo -e "${GREEN}Cleanup complete.${NC}"
}

trap cleanup EXIT INT TERM

# Build
echo -e "${CYAN}"
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║              SDS Scale Test                                  ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║  Devices: $NUM_DEVICES                                                ║"
echo "║  Duration: ${DURATION}s                                               ║"
echo "║  Broker: $BROKER                                           ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

echo -e "${YELLOW}Building test executables...${NC}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. > /dev/null
make test_scale_owner test_scale_device > /dev/null 2>&1 || {
    echo -e "${RED}Build failed. Trying verbose build...${NC}"
    make test_scale_owner test_scale_device
}
echo -e "${GREEN}Build complete.${NC}\n"

# Check broker connectivity
echo -e "${YELLOW}Checking MQTT broker at $BROKER:1883...${NC}"
if ! nc -z "$BROKER" 1883 2>/dev/null; then
    echo -e "${RED}Cannot connect to MQTT broker at $BROKER:1883${NC}"
    echo "Please start Mosquitto or specify a valid broker:"
    echo "  brew services start mosquitto  (macOS)"
    echo "  sudo systemctl start mosquitto (Linux)"
    echo "  docker run -p 1883:1883 eclipse-mosquitto (Docker)"
    exit 1
fi
echo -e "${GREEN}Broker is reachable.${NC}\n"

# Start owner process
echo -e "${YELLOW}Starting owner process...${NC}"
"$BUILD_DIR/test_scale_owner" "$BROKER" "$DURATION" &
OWNER_PID=$!
sleep 1

# Verify owner started
if ! kill -0 $OWNER_PID 2>/dev/null; then
    echo -e "${RED}Owner process failed to start${NC}"
    exit 1
fi

# Start device processes
echo -e "${YELLOW}Starting $NUM_DEVICES device processes...${NC}"
DEVICE_PIDS=""

for i in $(seq 1 $NUM_DEVICES); do
    device_id=$(printf "device_%02d" $i)
    "$BUILD_DIR/test_scale_device" "$device_id" "$BROKER" "$DURATION" > /dev/null 2>&1 &
    DEVICE_PIDS="$DEVICE_PIDS $!"
    
    # Stagger device startup slightly to avoid connection storm
    sleep 0.1
done

echo -e "${GREEN}All $NUM_DEVICES devices started.${NC}\n"

# Wait for owner to complete (it shows the consolidated output)
echo -e "${CYAN}Running test...${NC}\n"
wait $OWNER_PID
OWNER_EXIT=$?

# Wait for devices to finish
echo -e "\n${YELLOW}Waiting for devices to finish...${NC}"
for pid in $DEVICE_PIDS; do
    wait $pid 2>/dev/null || true
done

# Clear the trap since we've handled cleanup
DEVICE_PIDS=""
OWNER_PID=""

echo -e "\n${GREEN}Scale test complete.${NC}"

if [ $OWNER_EXIT -eq 0 ]; then
    exit 0
else
    exit 1
fi
