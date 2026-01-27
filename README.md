# SDS (Simple DDS)

A lightweight MQTT-based state synchronization library for embedded systems.

## Features

- **Single table type** with config/state/status sections
- **Two roles**: OWNER and DEVICE
- **Fluid architecture**: Any node can be owner or device for any table
- **Cross-platform**: ESP32/ESP8266/Arduino + macOS/Linux
- **Liveness detection**: Automatic heartbeats with configurable intervals
- **LWT support**: Broker notifies on unexpected disconnects
- **MQTT authentication**: Optional username/password support
- **Schema versioning**: Detect and handle version mismatches between nodes
- **Runtime log level**: Adjust logging verbosity without recompiling

## Quick Start

```c
#include "sds.h"
#include "sds_types.h"  // Generated from schema

SensorDataTable myTable;

void setup() {
    SdsConfig config = {
        .node_id = "sensor_A",
        .mqtt_broker = "192.168.1.100",
        .mqtt_port = 1883,
        .mqtt_username = NULL,  // Optional: set for authentication
        .mqtt_password = NULL
    };
    
    sds_init(&config);
    sds_register_table(&myTable, "SensorData", SDS_ROLE_DEVICE, NULL);
}

void loop() {
    // Update state
    myTable.state.temperature = read_sensor();
    
    // SDS syncs automatically
    sds_loop();
}
```

## Documentation

- **[DESIGN.md](DESIGN.md)** - Full design specification
- **[API Reference](docs/html/index.html)** - Doxygen-generated API docs

### Generating API Documentation

```bash
# Install Doxygen (if not installed)
brew install doxygen  # macOS
# apt-get install doxygen  # Ubuntu

# Generate HTML documentation
doxygen Doxyfile

# Open in browser
open docs/html/index.html
```

## Memory Requirements

### Static RAM Usage

| Component | Size | Notes |
|-----------|------|-------|
| Node ID buffer | 32 bytes | `SDS_MAX_NODE_ID_LEN` |
| Broker buffer | 128 bytes | `SDS_MAX_BROKER_LEN` |
| Table contexts | ~600 bytes each | Includes shadow buffers |
| Stats | 16 bytes | Counters |
| **Total (per table)** | **~776 bytes** | + table data |

### Configuration Defaults

| Setting | Default | Can Override |
|---------|---------|--------------|
| `SDS_MAX_TABLES` | 8 | Compile-time |
| `SDS_MAX_NODE_ID_LEN` | 32 | Compile-time |
| `SDS_MSG_BUFFER_SIZE` | 512 | Compile-time |
| `SDS_SHADOW_SIZE` | Auto-calculated | From schema |

### Stack Usage (per sync cycle)

| Buffer | Size |
|--------|------|
| Topic buffer | 128 bytes |
| JSON buffer | 512 bytes |
| SdsJsonWriter | ~24 bytes |
| **Total** | **~700 bytes** |

### Platform-Specific Considerations

| Platform | RAM Available | Recommendation |
|----------|---------------|----------------|
| ESP32 | 320 KB | ✅ Full support, up to 8 tables |
| ESP8266 | 80 KB | ⚠️ Limit to 2-3 tables, reduce buffer sizes |
| Linux/macOS | Unlimited | ✅ Full support |

### Reducing Memory Footprint

For constrained devices (ESP8266), add to your build:

```c
#define SDS_MAX_TABLES 2
#define SDS_MSG_BUFFER_SIZE 256
```

## Building

### macOS/Linux (for testing)
```bash
mkdir build && cd build
cmake ..
make
```

### ESP32/ESP8266 (PlatformIO)
```bash
cd examples/esp32_sensor
pio run
```

### Running Tests

```bash
# Unit tests (no MQTT broker required) - runs in ~0.5s
cd build && cmake .. && make
./test_unit_core && ./test_json && ./test_utilities

# Integration tests (requires MQTT broker)
brew services start mosquitto  # or: sudo systemctl start mosquitto
./run_tests.sh

# Scale test (25 devices + 1 owner)
./tests/scale/run_scale_test.sh 25 30
```

## Testing

The library includes **167 unit tests** with **~84% code coverage**.

| Test Suite | Tests | MQTT Required | Description |
|------------|-------|---------------|-------------|
| `test_unit_core` | 45 | No | Core SDS functionality |
| `test_json` | 65 | No | JSON serialization/parsing |
| `test_utilities` | 23 | No | Utility functions |
| `test_reconnection` | 11 | No | Reconnection scenarios |
| `test_buffer_overflow` | 16 | No | Buffer limits |
| `test_concurrent` | 7 | No | Thread safety |
| `test_sds_basic` | - | Yes | Integration tests |
| `test_scale_*` | - | Yes | Scale testing (25+ devices) |

See [TESTING.md](TESTING.md) for details.

## License

MIT

