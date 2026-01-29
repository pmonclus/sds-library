# Hybrid Demo: All Language Combinations

A self-contained example demonstrating SDS communication between owners and devices
in any combination of C and Python.

## Available Combinations

| Owner | Device | Commands |
|-------|--------|----------|
| Python | C | `python_owner/owner.py` + `c_device/device` |
| Python | Python | `python_owner/owner.py` + `python_device/device.py` |
| C | C | `c_owner/owner` + `c_device/device` |
| C | Python | `c_owner/owner` + `python_device/device.py` |

ESP32 combinations also supported (see ESP32 section below).

## Table Schema

```
table DeviceDemo {
    config {
        led_control: uint8       // 0=off, 1=on - controls device LED
        active_device: string    // Node ID of device allowed to publish state
    }
    
    state {
        temperature: float       // Only active_device publishes
        humidity: float          // Only active_device publishes
    }
    
    status {
        power_consumption: float // All devices publish (simulated)
        latest_log: string       // Rotating messages every 5 seconds
    }
}
```

## Behavior

| Component | Behavior |
|-----------|----------|
| **Owner** | Sets `led_control` on/off, sets `active_device` to control which device publishes state |
| **Devices** | Respond to LED control, only `active_device` publishes temperature/humidity |
| **All Devices** | Publish power consumption and rotating log messages to status |

### Data Flows

```
Owner (C or Python)               Devices (C, Python, or ESP32)
     │                                   │
     │──── config ──────────────────────►│  led_control, active_device
     │                                   │
     │◄─── state (LWW) ─────────────────│  temperature, humidity (active only)
     │                                   │
     │◄─── status (per-device) ─────────│  power_consumption, latest_log
```

## Prerequisites

1. **MQTT Broker** (Mosquitto) running on localhost:1883
   ```bash
   # macOS
   brew install mosquitto
   brew services start mosquitto
   
   # Linux
   sudo apt install mosquitto
   sudo systemctl start mosquitto
   ```

2. **Python 3.8+** with cffi
   ```bash
   pip install cffi
   ```

3. **For C programs**: GCC and libpaho-mqtt
   ```bash
   # macOS
   brew install libpaho-mqtt
   
   # Linux
   sudo apt install libpaho-mqtt-dev
   ```

4. **For ESP32**: Arduino IDE or PlatformIO

## Quick Start

### Step 1: Set Up the Example

```bash
cd examples/hybrid_demo
./generate.sh
```

This script:
1. Copies the SDS library files to `lib/` (making this example self-contained)
2. Generates `lib/include/demo_types.h` (C types) and `python_owner/demo_types.py` (Python types)

### Step 2: Build C Programs

```bash
# Build C device
cd c_device && make && cd ..

# Build C owner
cd c_owner && make && cd ..
```

### Step 3: Run Any Combination

**Example 1: Python Owner + C Device**
```bash
# Terminal 1 - Owner
cd python_owner && python owner.py

# Terminal 2 - Device
cd c_device && ./device c_dev_01
```

**Example 2: C Owner + Python Device**
```bash
# Terminal 1 - Owner
cd c_owner && ./owner

# Terminal 2 - Device
cd python_device && python device.py py_dev_01
```

**Example 3: Python Owner + Python Device**
```bash
# Terminal 1 - Owner
cd python_owner && python owner.py

# Terminal 2 - Device
cd python_device && python device.py py_dev_01
```

**Example 4: C Owner + C Device**
```bash
# Terminal 1 - Owner
cd c_owner && ./owner

# Terminal 2 - Device
cd c_device && ./device c_dev_01
```

### Step 4: Interact

All owners support the same commands:

| Command | Description |
|---------|-------------|
| `led on/off` | Toggle LED on all devices |
| `active <id>` | Set which device publishes state |
| `active none` | Disable state publishing |
| `status` | Show all device statuses |
| `verbose on/off` | Toggle live state/status messages |
| `help` | Show available commands |
| `quit` | Exit |

## ESP32 Device

### Configuration

```bash
cd esp32_device
cp config.h.example config.h
# Edit config.h with your WiFi and MQTT settings
```

### Build with PlatformIO (recommended)

```bash
pio run              # Build
pio run -t upload    # Flash to ESP32
pio device monitor   # View serial output
```

### Build with Arduino IDE

1. Make sure you've run `./generate.sh` first (creates symlinks in esp32_device/)
2. Install ESP32 board support (see Prerequisites)
3. Install PubSubClient library (Tools → Manage Libraries)
4. Open `esp32_device/esp32_device.ino` in Arduino IDE
5. Select your ESP32 board (Tools → Board → ESP32 Arduino)
6. Click Upload

The ESP32 device works with any owner (Python or C).

## Folder Structure

```
hybrid_demo/
├── README.md              # This file
├── schema.sds             # Table definition
├── generate.sh            # Sets up lib/ and generates types
├── lib/                   # (generated) SDS library copy
│   ├── include/           # Headers + demo_types.h
│   ├── src/               # Source files
│   └── platform/          # Platform implementations
├── c_owner/
│   ├── main.c             # C owner with menu interface
│   └── Makefile
├── c_device/
│   ├── main.c             # C device implementation
│   └── Makefile
├── python_owner/
│   ├── owner.py           # Python owner with menu interface
│   └── demo_types.py      # (generated) Python types
├── python_device/
│   └── device.py          # Python device implementation
└── esp32_device/
    ├── esp32_device.ino   # ESP32 Arduino sketch
    └── config.h.example   # WiFi/MQTT configuration template
```

**Note:** `lib/` and `demo_types.py` are generated by `./generate.sh` and not committed to git.

## Educational Notes

This example demonstrates:

1. **Config Broadcast**: Owner publishes config, all devices receive it
2. **Application-Level Access Control**: `active_device` acts as a token
3. **Shared State (LWW)**: Multiple devices could write, but app logic restricts it
4. **Per-Device Status**: Owner tracks each device's health independently
5. **Cross-Language Interop**: Python and C using the same protocol
6. **Callback user_data Pattern**: C callbacks receive table pointer without globals

### C Callback Pattern

The C programs use the `user_data` parameter to access the table in callbacks:

```c
void on_config_update(const char* table_type, void* user_data) {
    DeviceDemoTable* table = (DeviceDemoTable*)user_data;
    
    // Access table data without global variables
    if (table->config.led_control) {
        led_on();
    }
}

int main() {
    DeviceDemoTable table = {0};
    sds_register_table(&table, "DeviceDemo", SDS_ROLE_DEVICE, NULL);
    
    // Pass table pointer as user_data
    sds_on_config_update("DeviceDemo", on_config_update, &table);
}
```
