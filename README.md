# SDS (Synchronized Data Structures)

A lightweight MQTT-based state synchronization library for IoT applications.

## Overview

SDS enables automatic state synchronization between devices and owners over MQTT:
- **Devices** (sensors, actuators) send state/status, receive config
- **Owners** (servers, dashboards) send config, receive state/status from all devices

## Quick Start

### 1. Install Dependencies

**macOS:**
```bash
brew install libpaho-mqtt python3
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libpaho-mqtt-dev python3 python3-pip
```

### 2. Install SDS Library

```bash
# Clone the repository
git clone https://github.com/pmonclus/sds-library.git
cd sds-library

# Create a virtual environment (recommended)
python3 -m venv venv
source venv/bin/activate

# Install Python dependencies
pip install cffi

# Build C library
mkdir -p build && cd build
cmake .. -DSDS_BUILD_TESTS=OFF -DSDS_BUILD_EXAMPLES=OFF
make
cd ..

# Install SDS Python bindings (compiles CFFI extension)
cd python && pip install -e . && cd ..

# Install codegen CLI
pip install -e .
```

Verify installation:
```bash
sds-codegen --help
python3 -c "from sds import SdsNode; print('SDS OK')"
```

**Note:** Always activate the virtual environment before using SDS:
```bash
source venv/bin/activate
```

### 3. Define Your Schema

Create a `schema.sds` file that defines your data structure:

```
// schema.sds
@version = "1.0.0"

table SensorData {
    @sync_interval = 1000    // Sync every 1 second
    @liveness = 5000         // Heartbeat every 5 seconds
    
    // Config: owner → devices
    config {
        uint8 led_enabled = 0;
        float threshold = 25.0;
    }
    
    // State: devices → owner (merged across all devices)
    state {
        float temperature;
        float humidity;
    }
    
    // Status: each device → owner (per-device data)
    status {
        uint8 battery_percent;
        uint8 error_code;
    }
}
```

### 4. Generate Type Definitions

```bash
sds-codegen schema.sds --c --python
```

This creates:
- `sds_types.h` - C structs and serialization
- `sds_types.py` - Python dataclasses

### 5. Start an MQTT Broker

```bash
# macOS
brew install mosquitto
brew services start mosquitto

# Ubuntu
sudo apt-get install mosquitto
sudo systemctl start mosquitto
```

### 6. Run Your First Device (Python)

Create `device.py`:

```python
#!/usr/bin/env python3
from sds import SdsNode, Role
import random
import time

with SdsNode("sensor_01", "localhost") as node:
    table = node.register_table("SensorData", Role.DEVICE)
    
    print("Device running. Press Ctrl+C to stop.")
    while True:
        # Update sensor readings
        table.state.temperature = 20.0 + random.uniform(0, 5)
        table.state.humidity = 50.0 + random.uniform(0, 10)
        table.status.battery_percent = 85
        
        node.poll(timeout_ms=1000)
```

Run it:
```bash
python3 device.py
```

### 7. Run an Owner (Python)

Create `owner.py`:

```python
#!/usr/bin/env python3
from sds import SdsNode, Role

with SdsNode("owner", "localhost") as node:
    table = node.register_table("SensorData", Role.OWNER)
    
    @node.on_state("SensorData")
    def handle_state(table_type):
        print(f"Temperature: {table.state.temperature:.1f}°C, "
              f"Humidity: {table.state.humidity:.1f}%")
    
    print("Owner running. Press Ctrl+C to stop.")
    while True:
        node.poll(timeout_ms=1000)
```

Run it in another terminal:
```bash
python3 owner.py
```

You should see temperature/humidity readings appearing on the owner!

---

## Arduino / ESP32

### Installation

1. Run the packaging script to create the Arduino ZIP:
   ```bash
   ./packaging/build-package.sh quick
   ```

2. The Arduino library ZIP is at:
   ```
   # After brew install (future):
   $(brew --prefix)/share/sds/sds-arduino-*.zip
   
   # For now, create manually:
   cd packaging && ./build-package.sh tarball
   # Then find ZIP in the tarball
   ```

3. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library**

4. Install dependency: **PubSubClient** from Library Manager

### ESP32 Device Example

```cpp
#include <WiFi.h>
#include <PubSubClient.h>
#include "sds.h"
#include "sds_types.h"  // Generated from your schema.sds

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
SensorDataTable table;

void setup() {
    Serial.begin(115200);
    
    // Connect WiFi
    WiFi.begin("your_ssid", "your_password");
    while (WiFi.status() != WL_CONNECTED) delay(500);
    
    // Initialize SDS
    SdsConfig config = {
        .node_id = "esp32_sensor",
        .mqtt_broker = "192.168.1.100",
        .mqtt_port = 1883
    };
    sds_init(&config);
    sds_register_table(&table, "SensorData", SDS_ROLE_DEVICE, NULL);
}

void loop() {
    sds_loop();
    
    table.state.temperature = readTemperature();
    table.state.humidity = readHumidity();
    
    delay(100);
}
```

See [examples/hybrid_demo/esp32_device/](examples/hybrid_demo/esp32_device/) for a complete example.

---

## C Library (Linux/macOS)

### Building

```bash
mkdir build && cd build
cmake .. -DSDS_BUILD_TESTS=OFF
make
```

### Simple Device Example

```c
#include "sds.h"
#include "sds_types.h"

SensorDataTable table;

int main() {
    SdsConfig config = {
        .node_id = "linux_sensor",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    
    sds_init(&config);
    sds_register_table(&table, "SensorData", SDS_ROLE_DEVICE, NULL);
    
    while (1) {
        sds_loop();
        table.state.temperature = read_sensor();
        usleep(100000);
    }
    
    sds_shutdown();
    return 0;
}
```

Compile:
```bash
gcc -o device device.c -I../include -L../build -lsds -lpaho-mqtt3c
```

See [examples/hybrid_demo/c_device/](examples/hybrid_demo/c_device/) for a complete example.

---

## Schema Reference

```
// Comments use double slashes
@version = "1.0.0"              // Schema version

table TableName {
    @sync_interval = 1000       // State sync interval (ms)
    @liveness = 5000            // Heartbeat interval (ms)
    
    config {                    // Owner → Devices
        uint8 field_name;
        float value = 1.0;      // With default
    }
    
    state {                     // Devices → Owner (merged)
        float sensor_value;
    }
    
    status {                    // Each Device → Owner (per-device)
        uint8 error_code;
    }
}
```

**Supported types:** `bool`, `uint8`, `int8`, `uint16`, `int16`, `uint32`, `int32`, `float`, `string`

---

## Documentation

- [Python README](python/README.md) - Full Python API documentation
- [DESIGN.md](DESIGN.md) - Architecture and design decisions
- [TESTING.md](TESTING.md) - Test suite documentation
- [examples/hybrid_demo/](examples/hybrid_demo/) - Complete multi-language example

---

## Running Tests

```bash
# Unit tests (no MQTT broker required)
cd build && cmake .. && make
./test_unit_core && ./test_json && ./test_utilities

# Python tests
cd python && pytest tests/

# Integration tests (requires MQTT broker running)
./run_tests.sh
```

---

## Troubleshooting

**"sds-codegen: command not found"**
```bash
export PATH="$HOME/.local/bin:$PATH"
```

**"ModuleNotFoundError: No module named 'codegen'"**
```bash
cd sds-library && pip install -e .
```

**MQTT connection fails**
- Check broker is running: `brew services list` or `systemctl status mosquitto`
- Verify broker address and port

---

## License

MIT
