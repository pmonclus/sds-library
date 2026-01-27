# SDS (Simple DDS)

A lightweight MQTT-based state synchronization library for embedded systems.

## Features

- **Single table type** with config/state/status sections
- **Two roles**: OWNER and DEVICE
- **Fluid architecture**: Any node can be owner or device for any table
- **Cross-platform**: ESP32/ESP8266/Arduino + macOS/Linux
- **Liveness detection**: Automatic heartbeats with configurable intervals
- **LWT support**: Broker notifies on unexpected disconnects

## Quick Start

```c
#include "sds.h"
#include "sds_types.h"  // Generated from schema

SensorDataTable myTable;

void setup() {
    SdsConfig config = {
        .node_id = "sensor_A",
        .mqtt_broker = "192.168.1.100",
        .mqtt_port = 1883
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

See [DESIGN.md](DESIGN.md) for the full design specification.

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
# Requires MQTT broker (mosquitto) running locally
./run_tests.sh
```

## Testing

The library includes comprehensive tests:

| Test | Description |
|------|-------------|
| `test_json` | JSON serialization (65 test cases) |
| `test_sds_basic` | Core API functionality |
| `test_errors` | Error handling paths |
| `test_multi_node` | Multi-node communication |
| `test_liveness` | Heartbeat detection |

See [TESTING.md](TESTING.md) for details.

## License

MIT

