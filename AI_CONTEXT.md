# SDS Library - AI Assistant Context Document

> **Purpose**: This document provides complete context for an AI assistant to work on the SDS library without prior conversation history. Read this before making changes.

## 1. What is SDS?

**SDS (Simple DDS)** is a lightweight MQTT-based state synchronization library for embedded systems. It allows multiple nodes (ESP32 devices, Linux processes, etc.) to share structured data over MQTT.

### Core Concept

Nodes register **tables** with specific **roles**:
- **OWNER**: One node that controls config, receives state/status from all devices
- **DEVICE**: Many nodes that receive config, send their state/status to owner

```
┌─────────────────────────────────────────────────────────────┐
│                         MQTT Broker                          │
└─────────────────────────────────────────────────────────────┘
        ▲                    ▲                    ▲
        │ config             │ state              │ status
        │ (retained)         │ (best effort)      │ (per-device)
        ▼                    │                    │
   ┌─────────┐               │                    │
   │  OWNER  │───────────────┘                    │
   │ (1 per  │◄───────────────────────────────────┘
   │  table) │
   └─────────┘
        │
        │ config (retained, last value persisted)
        ▼
   ┌─────────┐  ┌─────────┐  ┌─────────┐
   │ DEVICE  │  │ DEVICE  │  │ DEVICE  │
   │    A    │  │    B    │  │    C    │
   └─────────┘  └─────────┘  └─────────┘
```

### Table Structure

Each table has three sections:
- **config**: Owner writes, devices receive (retained on broker)
- **state**: All nodes write, owner receives (last-write-wins merge)
- **status**: Devices write, owner receives (per-device tracking)

## 2. Project Structure

```
sds/
├── include/                    # Public headers
│   ├── sds.h                   # Main API (SdsConfig, sds_init, sds_register_table, etc.)
│   ├── sds_error.h             # Error codes (SdsError enum)
│   ├── sds_json.h              # JSON serialization (SdsJsonWriter/Reader)
│   ├── sds_platform.h          # Platform abstraction interface
│   └── SDS.h                   # Arduino C++ wrapper (SDSClient class)
│
├── src/
│   ├── sds_core.c              # Main implementation
│   ├── sds_json.c              # JSON implementation
│   └── sds.cpp                 # PlatformIO compilation wrapper
│
├── platform/
│   ├── posix/
│   │   └── sds_platform_posix.c    # Linux/macOS using Paho MQTT C
│   └── esp32/
│       └── sds_platform_esp32.cpp  # ESP32 using PubSubClient
│
├── codegen/                    # Python code generator
│   ├── parser.py               # Parses .sds schema files
│   ├── c_generator.py          # Generates C header from schema
│   └── cli.py                  # Command-line interface
│
├── tests/
│   ├── test_basic.c            # Basic connectivity tests
│   ├── test_multi_node.c       # Multi-node integration test
│   ├── test_generated.c        # Tests with generated types
│   └── test_simple_api.c       # Tests simple registration API
│
├── examples/
│   ├── linux_owner/main.c      # Linux owner example
│   └── esp32_sensor/           # ESP32 device example
│
├── schema.sds                  # Example schema file
├── CMakeLists.txt              # POSIX build
├── library.json                # PlatformIO library config
├── DESIGN.md                   # Detailed design specification
└── AI_CONTEXT.md               # This file
```

## 3. Key Files to Understand

### `include/sds.h` - Main API

```c
// Initialization
SdsError sds_init(const SdsConfig* config);
void sds_loop(void);              // Call every iteration
void sds_shutdown(void);
bool sds_is_ready(void);          // True if MQTT connected

// Table Registration (simple API - uses generated registry)
SdsError sds_register_table(void* table, const char* table_type, 
                            SdsRole role, const SdsTableOptions* options);

// Callbacks
void sds_on_config_update(const char* table_type, SdsConfigCallback cb);
void sds_on_state_update(const char* table_type, SdsStateCallback cb);
void sds_on_status_update(const char* table_type, SdsStatusCallback cb);
```

### `src/sds_core.c` - Core Implementation

Key internal structures:
- `SdsTableContext`: Tracks each registered table (role, callbacks, shadow copies)
- `_table_registry`: Points to generated metadata (set by constructor in sds_types.h)

Key functions:
- `sds_init()`: Connects to MQTT broker
- `sds_loop()`: Checks for changes, publishes updates, processes incoming messages
- `sds_register_table()`: Looks up table type in registry, calls `sds_register_table_ex()`
- `sync_table()`: Compares table to shadow, publishes if changed
- `on_mqtt_message()`: Routes incoming messages to appropriate handlers

### `codegen/c_generator.py` - Code Generator

Generates `sds_types.h` from `schema.sds`:
- `{Table}Config`, `{Table}State`, `{Table}Status` structs
- `{Table}Table` (device struct), `{Table}OwnerTable` (owner struct)
- `{table}_serialize_*()` and `{table}_deserialize_*()` functions
- `SDS_TABLE_REGISTRY[]` with complete metadata
- Auto-registration via `__attribute__((constructor))`

## 4. How Registration Works

### The Flow

1. **Compile time**: User includes `sds_types.h` (generated from their schema)
2. **Before main()**: Constructor in `sds_types.h` calls `sds_set_table_registry()`
3. **Runtime**: User calls `sds_register_table(&table, "TypeName", role, NULL)`
4. **Lookup**: `sds_register_table()` finds metadata in registry
5. **Setup**: Calls `sds_register_table_ex()` with correct offsets and callbacks

### Why This Design?

The user never writes serialization code. Just:
```c
SensorNodeTable sensor = {0};
sds_register_table(&sensor, "SensorNode", SDS_ROLE_DEVICE, NULL);
```

The library gets all serialization functions from the generated registry.

## 5. MQTT Topics

```
sds/{table_type}/config              # Owner → Devices (retained)
sds/{table_type}/state               # All → Owner
sds/{table_type}/status/{node_id}    # Device → Owner
```

## 6. JSON Message Format

Flat structure (no nesting):
```json
{"ts": 12345, "from": "node_id", "temperature": 23.5, "humidity": 45.0}
```

Custom minimal JSON library (`sds_json.h/c`) - no external dependencies.

## 7. Platform Abstraction

`sds_platform.h` defines the interface. Two implementations:
- **POSIX** (`sds_platform_posix.c`): Uses Paho MQTT C library
- **ESP32** (`sds_platform_esp32.cpp`): Uses PubSubClient + WiFi

Key functions:
- `sds_platform_mqtt_connect/disconnect/publish/subscribe`
- `sds_platform_millis/delay_ms`
- `sds_platform_log`

## 8. Building

### POSIX (Linux/macOS)
```bash
brew install libpaho-mqtt  # macOS
cd sds && mkdir build && cd build
cmake .. && make
./test_simple_api
```

### ESP32 (PlatformIO)
```bash
cd examples/esp32_sensor
pio run
pio run -t upload
```

## 9. Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Table registration | Registry-based lookup | Simple API, no generated function names |
| Auto-registration | Constructor attribute | No manual setup required |
| JSON library | Custom minimal | No dependencies, same code everywhere |
| Shadow copies | Per-section in SdsTableContext | Detect changes without user intervention |
| Serialization | Generated functions | Type-safe, no runtime reflection |

## 10. Important Implementation Details

### Change Detection
`sds_loop()` compares current table data with shadow copies using `memcmp()`. If different, it serializes and publishes, then updates shadow.

### Initial Config Publish
When owner registers, it immediately publishes its current config (retained). This ensures new devices get config on subscribe.

### Callbacks
User registers callbacks AFTER `sds_register_table()`:
```c
sds_register_table(&table, "SensorNode", SDS_ROLE_DEVICE, NULL);
sds_on_config_update("SensorNode", my_callback);  // After registration
```

### Table Context Array
Fixed-size array of `SDS_MAX_TABLES` (default 8). No dynamic allocation.

## 11. Schema Language

```sds
@version = "1.0.0"

table SensorNode {
    @sync_interval = 1000
    
    config {
        uint8 command;
        float threshold = 25.0;
    }
    
    state {
        float temperature;
        float humidity;
    }
    
    status {
        uint8 error_code;
        uint8 battery_percent;
        uint32 uptime_seconds;
    }
}
```

Supported types: `bool`, `uint8`, `int8`, `uint16`, `int16`, `uint32`, `int32`, `float`, `string[N]`

## 12. Current Status

✅ **Complete and tested:**
- Core library (init, loop, shutdown)
- Table registration (simple and extended APIs)
- Config/state/status flows
- POSIX and ESP32 platforms
- Code generation from schema
- Auto-registration via constructor
- Multi-node integration tests

⏳ **Not yet implemented:**
- Status slot storage at owner (callback works, array storage pending)
- OTA updates
- Device discovery

## 13. Common Tasks

### Add a new table type
1. Edit `schema.sds` to add your table definition
2. Run `python -m codegen.cli schema.sds -o include/sds_types.h`
3. Include `sds_types.h` in your code
4. Register with `sds_register_table()`

### Add a new field type
1. Edit `codegen/c_generator.py`: Update `TYPE_MAP`, `JSON_WRITE_MAP`, `JSON_READ_MAP`
2. Update `_write_serialize_field()` and `_write_deserialize_field()`
3. If needed, add JSON functions to `sds_json.c`

### Add a new platform
1. Create `platform/{name}/sds_platform_{name}.c`
2. Implement all functions from `sds_platform.h`
3. Update build system to conditionally compile

## 14. Testing

```bash
# Run simple API test
./build/test_simple_api

# Run multi-node test (3 terminals)
./build/test_generated node1 &
./build/test_generated node2 &
./build/test_generated node3 &
```

Requires MQTT broker running (e.g., `mosquitto`).

## 15. Files You'll Edit Most Often

| Task | Files |
|------|-------|
| API changes | `include/sds.h`, `src/sds_core.c` |
| New field types | `codegen/c_generator.py`, `src/sds_json.c` |
| Bug fixes | `src/sds_core.c` |
| Platform issues | `platform/{name}/sds_platform_*.c` |
| Schema syntax | `codegen/parser.py` |

---

*This document was created to preserve context for AI assistants working on SDS as a standalone project. Update it when making significant architectural changes.*

