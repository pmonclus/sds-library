# SDS (Simple DDS)

A lightweight MQTT-based state synchronization library for embedded systems.

## Features

- **Single table type** with config/state/status sections
- **Two roles**: OWNER and DEVICE
- **Fluid architecture**: Any node can be owner or device for any table
- **Cross-platform**: ESP32/Arduino + macOS/Linux

## Quick Start

```c
#include "sds.h"
#include "sds_types.h"  // Generated from schema

SensorNodeTable myTable;

void setup() {
    SdsConfig config = {
        .node_id = "sensor_A",
        .mqtt_broker = "192.168.1.100",
        .mqtt_port = 1883
    };
    
    sds_init(&config);
    sds_register_table(&myTable, "SensorNode", SDS_ROLE_DEVICE, NULL);
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

## Building

### macOS/Linux (for testing)
```bash
mkdir build && cd build
cmake ..
make
```

### ESP32 (PlatformIO)
```bash
pio lib install sds
```

## License

MIT

