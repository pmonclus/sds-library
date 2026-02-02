# SDS Library - Testing Guide

This document describes the comprehensive test suite for the SDS library.

## Test Suite Overview

The library has **378+ tests** across C and Python, achieving **~84% code coverage**.

| Category | Tests | MQTT Broker | Runtime |
|----------|-------|-------------|---------|
| Unit Tests (C, Mock) | 200+ | No | ~0.5s |
| Integration Tests (C) | ~36 | Yes | ~60s |
| Python Tests | 142 | Yes | ~25s |
| Scale Tests | 1 | Yes | configurable |
| Fuzz Tests | 2 targets | No | configurable |

## Quick Start

```bash
# Build
mkdir -p build && cd build
cmake .. && make

# Run all unit tests (no MQTT required)
./test_unit_core && ./test_delta_sync && ./test_json && ./test_utilities && \
./test_reconnection && ./test_buffer_overflow && ./test_concurrent
```

---

## Unit Tests (Mock-Based)

These tests use a **mock platform layer** that simulates MQTT without a real broker. They run in ~0.5 seconds and are ideal for CI/CD.

### `test_unit_core` (60 tests)

Core SDS library functionality.

| Category | Tests |
|----------|-------|
| Initialization | init, shutdown, double init, ready state |
| Table Registration | device/owner roles, multiple tables, unregister |
| Subscriptions | device/owner topic subscriptions |
| Sync / Publish | state changes, dirty detection, publish |
| Message Receive | config/state/status handling |
| Callbacks | config, state, status callbacks |
| Reconnection | disconnect detection, auto-reconnect |
| Statistics | message counters, error tracking |
| Edge Cases | empty payload, malformed JSON, unknown table |
| LWT (Device Offline) | LWT subscription, offline detection, callbacks |
| Eviction | eviction timer, reconnect cancellation, grace period |
| Large Sections | 1KB section serialization, buffer overflow protection |
| Delta Sync | config enabled/disabled, float tolerance |

```bash
./build/test_unit_core
```

### `test_delta_sync` (8 tests)

Delta synchronization tests.

| Category | Tests |
|----------|-------|
| Full Sync | disabled behavior, no publish when unchanged |
| Delta Sync | single field change, multiple field changes |
| Float Tolerance | below/above threshold behavior |
| Status/Liveness | full status on heartbeat |
| Configuration | delta config value preservation |

```bash
./build/test_delta_sync
```

### `test_json` (75 tests)

JSON serialization and parsing.

| Category | Tests |
|----------|-------|
| Writer | strings, integers, floats, booleans, nested objects |
| Reader | field extraction, type parsing, error handling |
| Edge Cases | escaping, unicode, buffer limits, malformed input |
| Escape Sequences | unescape quotes/backslash/newlines, control char escaping |
| Integer Overflow | int32/uint32/uint8 range validation |
| Round-trip | serialize then deserialize validation |

```bash
./build/test_json
```

### `test_utilities` (23 tests)

Utility functions and APIs.

| Category | Tests |
|----------|-------|
| Error Strings | all error codes, unknown codes |
| Log Levels | get/set, all levels |
| Schema Version | get/set, NULL handling |
| Table Registry | find, set, NULL params |
| Simple Register API | registry lookup, not found |
| Node Status | find, iterate, online check |
| Liveness | interval queries |

```bash
./build/test_utilities
```

### `test_reconnection` (11 tests)

MQTT reconnection scenarios.

| Category | Tests |
|----------|-------|
| Connection Loss | detect disconnect, state tracking |
| Auto-Reconnect | reconnect success, failure handling |
| Message Recovery | publish after reconnect |
| Callbacks | error callbacks during disconnect |

```bash
./build/test_reconnection
```

### `test_buffer_overflow` (16 tests)

Buffer limits and overflow handling.

| Category | Tests |
|----------|-------|
| JSON Writer | small buffers, exact fit, overflow flag |
| JSON Reader | truncation, zero-size buffer |
| Shadow Buffer | large state, serialization limits |
| Status Slots | max slots, slot overflow |

```bash
./build/test_buffer_overflow
```

### `test_concurrent` (7 tests)

Thread safety and race conditions.

| Category | Tests |
|----------|-------|
| Baseline | single-threaded operation |
| Concurrent Access | loop + modify, loop + message inject |
| Multi-threaded | multiple modifiers, stats races |
| Edge Cases | message during shutdown |

> **Note**: Run with ThreadSanitizer to detect races:
> ```bash
> clang -fsanitize=thread ... -o test_concurrent_tsan
> ./test_concurrent_tsan
> ```

```bash
./build/test_concurrent
```

---

## Integration Tests (Real MQTT)

These tests require a running MQTT broker on `localhost:1883`.

### Prerequisites

```bash
# macOS
brew install mosquitto
brew services start mosquitto

# Linux
sudo apt install mosquitto
sudo systemctl start mosquitto

# Docker
docker run -p 1883:1883 eclipse-mosquitto
```

### `test_sds_basic`

Core API with real MQTT. Tests initialization, registration, message flow.

```bash
./build/test_sds_basic [broker_ip]
```

### `test_multi_node`

Multi-node communication with 3 processes.

```bash
./build/test_multi_node node1 &
./build/test_multi_node node2 &
./build/test_multi_node node3 &
wait
```

### `test_liveness`

Heartbeat and liveness detection.

```bash
./build/test_liveness node1 &  # Owner
./build/test_liveness node2 &  # Device
wait
```

### Running All Integration Tests

```bash
./run_tests.sh              # All tests
./run_tests.sh --quick      # Skip multi-node
./run_tests.sh --verbose    # Show output
```

---

## Scale Tests

Test with multiple concurrent devices.

### Usage

```bash
# Default: 25 devices, 30 seconds
./tests/scale/run_scale_test.sh

# Custom: 50 devices, 60 seconds, remote broker
./tests/scale/run_scale_test.sh 50 60 192.168.1.100
```

### What It Tests

- 1 owner process tracking multiple devices
- 25+ device processes publishing state/status
- Message throughput under load
- Status slot management at scale

### Sample Output

```
══════════════════════════════════════════════════════════════
  SCALE TEST COMPLETE
══════════════════════════════════════════════════════════════
  Total unique devices seen: 25
  Max concurrent devices: 25
  Messages received: 588
  Total errors: 0
══════════════════════════════════════════════════════════════
```

---

## Sanitizers

### AddressSanitizer (ASan)

Detects memory errors (buffer overflows, use-after-free).

```bash
./scripts/run_sanitizers.sh asan
```

### UndefinedBehaviorSanitizer (UBSan)

Detects undefined behavior (null deref, misaligned access).

```bash
./scripts/run_sanitizers.sh ubsan
```

### Valgrind

Memory leak detection (Linux only).

```bash
./scripts/run_sanitizers.sh valgrind
```

### Run All Sanitizers

```bash
./scripts/run_sanitizers.sh all
```

---

## Fuzzing

Fuzz testing with AFL++ or libFuzzer.

### Build Fuzz Targets

```bash
# With libFuzzer (Linux)
./scripts/run_fuzz.sh build

# Run for 60 seconds
./scripts/run_fuzz.sh all 60
```

### Fuzz Targets

| Target | What It Tests |
|--------|---------------|
| `fuzz_json` | JSON parser with malformed input |
| `fuzz_mqtt` | MQTT message handler with adversarial payloads |

### Corpus

Seed inputs are in `tests/fuzz/corpus/`:
- `corpus/json/` - Valid and edge-case JSON
- `corpus/mqtt/` - Valid and malformed MQTT payloads

---

## Code Coverage

### Generate Coverage Report

```bash
# Build with coverage
mkdir -p build_coverage && cd build_coverage
cmake -DCMAKE_C_FLAGS="--coverage" ..
make

# Run tests
./test_unit_core && ./test_json && ./test_utilities && \
./test_reconnection && ./test_buffer_overflow && ./test_concurrent

# Generate report
cd CMakeFiles/sds_mock.dir/src
gcov sds_core.c.gcda sds_json.c.gcda
```

### Current Coverage

| File | Coverage |
|------|----------|
| `sds_core.c` | ~81% |
| `sds_json.c` | ~94% |
| **Overall** | **~84%** |

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Tests

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. && make
      
      - name: Run unit tests
        run: |
          cd build
          ./test_unit_core
          ./test_json
          ./test_utilities
          ./test_reconnection
          ./test_buffer_overflow
          ./test_concurrent

  integration-tests:
    runs-on: ubuntu-latest
    services:
      mosquitto:
        image: eclipse-mosquitto
        ports:
          - 1883:1883
    steps:
      - uses: actions/checkout@v4
      
      - name: Build and test
        run: |
          mkdir build && cd build
          cmake .. && make
          cd ..
          ./run_tests.sh

  sanitizers:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Run sanitizers
        run: ./scripts/run_sanitizers.sh all
```

---

## Adding New Tests

1. Create test file in `tests/`
2. Add to `CMakeLists.txt`:
   ```cmake
   add_executable(test_new tests/test_new.c)
   target_link_libraries(test_new sds_mock m)
   target_include_directories(test_new PRIVATE include tests)
   ```
3. Update this document
4. Add to CI workflow if needed

---

## Troubleshooting

### "Connection refused"

MQTT broker not running:
```bash
brew services start mosquitto  # macOS
sudo systemctl start mosquitto # Linux
```

### Unit tests fail to build

Missing mock library:
```bash
cd build && cmake .. && make sds_mock
```

### Scale test hangs

Check broker connectivity:
```bash
nc -z localhost 1883
```

### Coverage not generated

Ensure `--coverage` flag is set:
```bash
cmake -DCMAKE_C_FLAGS="--coverage" ..
```
