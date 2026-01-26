# SDS Library - Testing Guide

This document describes the test suite for the SDS library, including test descriptions, what each test validates, and how to run them.

## Prerequisites

1. **MQTT Broker**: All tests require a running MQTT broker (e.g., Mosquitto) on `localhost:1883`
   ```bash
   # macOS
   brew install mosquitto
   brew services start mosquitto
   
   # Linux
   sudo apt install mosquitto
   sudo systemctl start mosquitto
   ```

2. **Build the project**:
   ```bash
   mkdir -p build && cd build
   cmake .. && make
   ```

## Test Overview

| Test | Type | Duration | Description |
|------|------|----------|-------------|
| `test_json` | Unit | <1s | JSON serialization/parsing |
| `test_sds_basic` | Unit | ~7s | Core API functionality |
| `test_simple_api` | Unit | ~5s | Simple registration API |
| `test_generated` | Integration | ~15s | Multi-node with generated types |
| `test_multi_node` | Integration | ~15s | Multi-node communication patterns |
| `test_liveness` | Integration | ~14s | Liveness/heartbeat detection |

## Running All Tests

Use the automated test runner:

```bash
./run_tests.sh
```

Or run individual tests manually (see below).

---

## Test Descriptions

### 1. `test_sds_basic`

**Purpose**: Validates core SDS library functionality with generated types.

**What it tests**:
- ✅ Platform initialization
- ✅ MQTT broker connection
- ✅ `sds_init()` / `sds_shutdown()` lifecycle
- ✅ `sds_is_ready()` connection state
- ✅ Node ID assignment and retrieval
- ✅ Double initialization rejection
- ✅ Device table registration (`SDS_ROLE_DEVICE`)
- ✅ Owner table registration (`SDS_ROLE_OWNER`)
- ✅ Duplicate registration rejection
- ✅ Table count tracking
- ✅ Callback registration (config/state/status)
- ✅ Main loop execution with message flow
- ✅ Table unregistration
- ✅ Graceful shutdown

**Run manually**:
```bash
./build/test_sds_basic [broker_ip]
```

**Expected output**: `Results: 16 passed, 0 failed`

---

### 2. `test_simple_api`

**Purpose**: Validates the simplified `sds_register_table()` API that uses the generated type registry.

**What it tests**:
- ✅ Auto-registration of type registry via constructor
- ✅ Simple table registration by type name string
- ✅ Device role registration
- ✅ Owner role registration
- ✅ Unknown table type rejection
- ✅ Double registration rejection
- ✅ Message publishing during sync loop
- ✅ Correct table count after operations

**Run manually**:
```bash
./build/test_simple_api [broker_ip]
```

**Expected output**: `Results: 1/1 tests passed`

---

### 3. `test_generated`

**Purpose**: Integration test validating multi-node communication using generated types from `sds_types.h`.

**What it tests**:
- ✅ Three nodes with different role configurations
- ✅ SensorNode and ActuatorNode table types
- ✅ Config propagation from Owner to Devices
- ✅ State updates from Devices to Owner
- ✅ Status updates from Devices to Owner
- ✅ Retained message delivery on subscribe
- ✅ Real-time data synchronization
- ✅ Concurrent MQTT operations

**Node configurations**:
| Node | SensorNode Role | ActuatorNode Role |
|------|-----------------|-------------------|
| node1 | OWNER | DEVICE |
| node2 | DEVICE | OWNER |
| node3 | DEVICE | DEVICE |

**Run manually** (requires 3 terminals or background processes):
```bash
# Terminal 1
./build/test_generated node1

# Terminal 2
./build/test_generated node2

# Terminal 3
./build/test_generated node3
```

Or run all at once:
```bash
./build/test_generated node1 &
./build/test_generated node2 &
./build/test_generated node3 &
wait
```

**Expected output**: Each node prints `Overall: PASSED`

---

### 4. `test_multi_node`

**Purpose**: Integration test with TableA/TableB schema validating complex multi-node scenarios.

**What it tests**:
- ✅ Different table types (TableA, TableB)
- ✅ Cross-table role assignments
- ✅ Config delivery timing and reliability
- ✅ State update frequency and ordering
- ✅ Status update aggregation at owner
- ✅ Message statistics tracking
- ✅ Reconnection handling

**Node configurations**:
| Node | TableA Role | TableB Role |
|------|-------------|-------------|
| node1 | OWNER | DEVICE |
| node2 | DEVICE | OWNER |
| node3 | DEVICE | DEVICE |

**Run manually**:
```bash
./build/test_multi_node node1 &
./build/test_multi_node node2 &
./build/test_multi_node node3 &
wait
```

**Expected output**: Each node prints `Overall: PASSED`

---

### 5. `test_liveness`

**Purpose**: Validates the liveness/heartbeat detection mechanism between owner and device nodes.

**What it tests**:
- ✅ Device sends periodic heartbeats even when data is unchanged
- ✅ Owner tracks `last_seen_ms` timestamp for each device
- ✅ `sds_is_device_online()` API returns correct status
- ✅ Heartbeat interval matches `@liveness` configuration
- ✅ Uptime tracking across heartbeats
- ✅ Graceful shutdown with explicit offline message

**Node configurations**:
| Node | Role | Description |
|------|------|-------------|
| node1 | OWNER | Receives heartbeats, tracks device liveness |
| node2 | DEVICE | Sends periodic heartbeats (1000ms interval) |

**Run manually** (requires 2 terminals):
```bash
# Terminal 1 - Start owner first
./build/test_liveness node1

# Terminal 2 - Start device
./build/test_liveness node2
```

**Expected output**: Each node prints `✓ <node>: PASSED`

**Test validation**:
- Owner verifies it received sufficient heartbeats (min ~6 over 8 seconds)
- Owner verifies `sds_is_device_online()` returns true
- Owner verifies device uptime is incrementing

---

## Test Runner Script

The `run_tests.sh` script automates running all tests:

```bash
./run_tests.sh           # Run all tests
./run_tests.sh --quick   # Skip multi-node tests
./run_tests.sh --verbose # Show full output
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | One or more tests failed |
| 2 | Build failed |
| 3 | MQTT broker not running |

---

## Troubleshooting

### "Connection refused" errors
The MQTT broker is not running. Start it with:
```bash
brew services start mosquitto  # macOS
sudo systemctl start mosquitto # Linux
```

### "Table type not found in registry"
The test is not including `sds_types.h`. Ensure the generated types header is included and the constructor runs before `main()`.

### Multi-node tests hang
Ensure all three node processes are running. The tests wait for messages from other nodes.

### Flaky test results
MQTT message ordering is not guaranteed. If tests occasionally fail, increase timeouts or add retry logic.

---

## Adding New Tests

1. Create a new test file in `tests/`
2. Include `sds.h` and `sds_types.h`
3. Add the test to `CMakeLists.txt`:
   ```cmake
   add_executable(test_new tests/test_new.c)
   target_link_libraries(test_new sds ${PAHO_MQTT_LIBS})
   ```
4. Update `run_tests.sh` to include the new test
5. Update this document

---

## CI/CD Integration

For automated CI pipelines:

```yaml
# Example GitHub Actions
- name: Start MQTT broker
  run: |
    sudo apt-get install -y mosquitto
    sudo systemctl start mosquitto

- name: Build and test
  run: |
    mkdir build && cd build
    cmake .. && make
    cd ..
    ./run_tests.sh
```

