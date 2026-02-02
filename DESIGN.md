# SDS (Simple DDS) - Design Specification

**Version:** 0.5.0 (Delta Sync, 1KB Sections)  
**Date:** February 2026  
**Status:** Library complete with delta sync, 1KB section support, ready for production

## 1. Overview

SDS (Simple DDS) is a lightweight MQTT-based state synchronization library for embedded systems and distributed applications. It provides a simplified alternative to DDS with:

- **Single table type** with three sections: `config`, `state`, `status`
- **Two roles**: `SDS_ROLE_OWNER` and `SDS_ROLE_DEVICE`
- **Fluid architecture**: Any node can register any tables with any role
- **Cross-platform**: ESP32/Arduino + macOS/Linux for local testing

## 2. Core Concepts

| Concept | Description |
|---------|-------------|
| **Node** | An endpoint running SDS (ESP32, Linux process, etc.) |
| **Table** | A typed data structure with config/state/status sections |
| **Role** | How a node participates: OWNER (exactly 1 per table) or DEVICE (many) |

### 2.1 Roles

- **OWNER**: Publishes config, receives aggregated state (LWW) and per-device status
- **DEVICE**: Receives config, publishes its own state and status

A single node can be OWNER for some tables and DEVICE for others. This enables fluid, non-hierarchical architectures.

### 2.2 Table Sections

| Section | Owner Writes | Device Writes | Owner Reads | Device Reads |
|---------|--------------|---------------|-------------|--------------|
| `config` | ✅ | ❌ | ✅ | ✅ |
| `state` | ✅ (optional) | ✅ | ✅ (merged LWW) | ❌ |
| `status` | ❌ | ✅ | ✅ (per-device) | ❌ |

## 3. Data Flow & MQTT Topics

```
Topic Structure:
  sds/{table_type}/config           # Owner → All devices (retained)
  sds/{table_type}/state/           # All nodes → Owner only (QoS 0)
  sds/{table_type}/status/{node_id} # Each device → Owner (QoS 0)
```

### 3.1 Config Flow
```
┌────────┐   publish (retained)    ┌─────────────────────┐
│ OWNER  │ ──────────────────────► │ sds/SensorNode/config│
└────────┘                         └──────────┬──────────┘
                                              │ subscribe
                    ┌─────────────────────────┼─────────────────────────┐
                    ▼                         ▼                         ▼
              [DEVICE A]                [DEVICE B]                [DEVICE C]
```

### 3.2 State Flow (Merged at Owner, LWW)
```
[DEVICE A] ──publish──►                                    
[DEVICE B] ──publish──► sds/SensorNode/state/ ──subscribe──► ┌────────┐
[DEVICE C] ──publish──►                                      │ OWNER  │
[OWNER]    ──publish──► (optional)                           └────────┘
                                                                  │
                                              Merged into single state struct
                                              Last perceived arrival wins
```

### 3.3 Status Flow (Per-Device at Owner)
```
[DEVICE A] ──► sds/SensorNode/status/device_A ──┐
[DEVICE B] ──► sds/SensorNode/status/device_B ──┼──► ┌────────┐
[DEVICE C] ──► sds/SensorNode/status/device_C ──┘    │ OWNER  │
                                                      └────────┘
                                                           │
                                          Stored in per-device slots array
```

## 4. Table Struct Types

### 4.1 Device Table (Used by DEVICE role)

```c
typedef struct {
    SensorNodeConfig config;   // Received from owner
    SensorNodeState state;     // Written locally, synced to owner
    SensorNodeStatus status;   // Written locally, synced to owner
} SensorNodeTable;
```

### 4.2 Owner Table (Used by OWNER role)

```c
#define SDS_MAX_NODES 16

typedef struct {
    char node_id[32];
    bool valid;
    bool online;              // false if LWT received or graceful disconnect
    uint32_t last_seen_ms;    // Timestamp of last received status
    bool eviction_pending;    // true if eviction timer is running
    uint32_t eviction_deadline; // Timestamp when eviction will trigger
    SensorNodeStatus status;
} SensorNodeStatusSlot;

typedef struct {
    SensorNodeConfig config;                    // Written locally, synced to devices
    SensorNodeState state;                      // Merged from all devices (LWW)
    SensorNodeStatusSlot status[SDS_MAX_NODES]; // Per-device status array
    uint8_t status_count;                       // Number of known devices
} SensorNodeOwnerTable;
```

### 4.3 Codegen Implications

The code generator will produce both struct variants from the same schema:
- `{TableName}Table` - for DEVICE role
- `{TableName}OwnerTable` - for OWNER role

## 5. API Design

### 5.1 Configuration Constants

```c
#define SDS_MAX_TABLES           8      // Max tables per node
#define SDS_MAX_NODE_ID_LEN      32     // Max node ID string length
#define SDS_MAX_TABLE_TYPE_LEN   32     // Max table type name length
#define SDS_DEFAULT_MQTT_PORT    1883
#define SDS_DEFAULT_SYNC_INTERVAL_MS 1000
#define SDS_TOPIC_BUFFER_SIZE    128
#define SDS_MSG_BUFFER_SIZE      512
```

### 5.2 Initialization

```c
typedef struct {
    const char* node_id;        // Unique node identifier (NULL = auto-generate)
    const char* mqtt_broker;    // MQTT broker hostname/IP
    uint16_t mqtt_port;         // MQTT port (default 1883)
    const char* mqtt_username;  // MQTT username (NULL = no auth)
    const char* mqtt_password;  // MQTT password (NULL = no auth)
    uint32_t eviction_grace_ms; // Grace period before evicting offline devices (0 = disabled)
} SdsConfig;

SdsError sds_init(const SdsConfig* config);
void sds_loop(void);            // Call every iteration
void sds_shutdown(void);
const char* sds_get_node_id(void);
bool sds_is_ready(void);        // Returns true if connected
```

### 5.3 Table Registration

```c
typedef enum {
    SDS_ROLE_OWNER,
    SDS_ROLE_DEVICE
} SdsRole;

typedef struct {
    uint32_t sync_interval_ms;  // Sync frequency (default 1000ms)
} SdsTableOptions;

// Simple registration using metadata registry (recommended)
// Automatically looks up serialization callbacks from generated registry
SdsError sds_register_table(
    void* table,
    const char* table_type,
    SdsRole role,
    const SdsTableOptions* options  // NULL for defaults
);

SdsError sds_unregister_table(const char* table_type);
uint8_t sds_get_table_count(void);
```

**Usage with simple API:**
```c
#include "sds.h"
#include "sds_types.h"  // Generated header (auto-registers via constructor)

// Initialize SDS
SdsConfig config = { .node_id = "my_node", .mqtt_broker = "localhost" };
sds_init(&config);

// Register tables with simple one-liner
SensorNodeTable sensor = {0};
sds_register_table(&sensor, "SensorNode", SDS_ROLE_DEVICE, NULL);

// That's it! No callbacks, offsets, or serialization functions needed.
```

**Note:** The generated `sds_types.h` uses `__attribute__((constructor))` to 
auto-register table metadata before `main()` runs. No manual setup required.

### 5.4 Error Codes

```c
typedef enum {
    SDS_OK = 0,
    SDS_ERR_NOT_INITIALIZED,
    SDS_ERR_ALREADY_INITIALIZED,
    SDS_ERR_MQTT_CONNECT_FAILED,
    SDS_ERR_TABLE_NOT_FOUND,
    SDS_ERR_TABLE_ALREADY_REGISTERED,
    SDS_ERR_OWNER_EXISTS,           // Another node is already owner
    SDS_ERR_MAX_TABLES_REACHED,
    SDS_ERR_MAX_NODES_REACHED,      // Owner's status slots full
    SDS_ERR_INVALID_ROLE,
} SdsError;
```

### 5.5 Callbacks

All callbacks receive a `user_data` pointer that was passed during registration,
allowing access to the table without global variables.

```c
// Config updated (device receives from owner)
typedef void (*SdsConfigCallback)(const char* table_type, void* user_data);
void sds_on_config_update(const char* table_type, SdsConfigCallback cb, void* user_data);

// State updated (owner only - receives merged state)
typedef void (*SdsStateCallback)(const char* table_type, const char* from_node_id, void* user_data);
void sds_on_state_update(const char* table_type, SdsStateCallback cb, void* user_data);

// Status updated (owner only - receives per-device status)
typedef void (*SdsStatusCallback)(const char* table_type, const char* from_node_id, void* user_data);
void sds_on_status_update(const char* table_type, SdsStatusCallback cb, void* user_data);

// Error callback for async operations (e.g., reconnection failures)
typedef void (*SdsErrorCallback)(SdsError error, const char* context);
void sds_on_error(SdsErrorCallback cb);

// Schema version mismatch callback (owner only)
// Return true to accept the message, false to reject
typedef bool (*SdsVersionMismatchCallback)(
    const char* table_type,
    const char* device_node_id,
    const char* local_version,
    const char* remote_version
);
void sds_on_version_mismatch(SdsVersionMismatchCallback cb);
```

**Example usage:**

```c
void on_config_update(const char* table_type, void* user_data) {
    SensorDataTable* table = (SensorDataTable*)user_data;
    printf("Config received: threshold=%.1f\n", table->config.threshold);
}

int main() {
    SensorDataTable table = {0};
    sds_register_table(&table, "SensorData", SDS_ROLE_DEVICE, NULL);
    sds_on_config_update("SensorData", on_config_update, &table);  // Pass table pointer
    // ...
}
```

### 5.6 Extended Registration (with Serialization)

```c
// Serialization callback types
typedef void (*SdsSerializeFunc)(void* section, SdsJsonWriter* w);
typedef void (*SdsDeserializeFunc)(void* section, SdsJsonReader* r);

// Extended registration with serialization callbacks
// Section pointers are passed to callbacks (not full table)
SdsError sds_register_table_ex(
    void* table,
    const char* table_type,
    SdsRole role,
    const SdsTableOptions* options,
    size_t config_offset, size_t config_size,
    size_t state_offset, size_t state_size,
    size_t status_offset, size_t status_size,
    SdsSerializeFunc serialize_config,
    SdsDeserializeFunc deserialize_config,
    SdsSerializeFunc serialize_state,
    SdsDeserializeFunc deserialize_state,
    SdsSerializeFunc serialize_status,
    SdsDeserializeFunc deserialize_status
);
```

**Example serialize/deserialize functions:**
```c
static void serialize_sensor_config(void* section, SdsJsonWriter* w) {
    SensorConfig* cfg = (SensorConfig*)section;
    sds_json_add_uint(w, "mode", cfg->mode);
    sds_json_add_float(w, "threshold", cfg->threshold);
}

static void deserialize_sensor_config(void* section, SdsJsonReader* r) {
    SensorConfig* cfg = (SensorConfig*)section;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
    sds_json_get_float_field(r, "threshold", &cfg->threshold);
}
```

### 5.7 Owner Helpers

```c
// Find status slot by node_id (returns NULL if not found)
void* sds_find_status_slot(void* owner_table, const char* table_type, const char* node_id);

// Iterate over valid status slots
typedef void (*SdsStatusIterator)(const char* node_id, const void* status, void* user_data);
void sds_foreach_status(void* owner_table, const char* table_type, 
                        SdsStatusIterator callback, void* user_data);
```

### 5.8 Statistics

```c
typedef struct {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t reconnect_count;
    uint32_t errors;
} SdsStats;

const SdsStats* sds_get_stats(void);
```

### 5.9 Runtime Log Level

```c
typedef enum {
    SDS_LOG_NONE = 0,   // Disable all logging
    SDS_LOG_ERROR = 1,  // Error conditions only
    SDS_LOG_WARN = 2,   // Warnings and errors
    SDS_LOG_INFO = 3,   // Informational messages (default)
    SDS_LOG_DEBUG = 4   // Debug messages
} SdsLogLevel;

void sds_set_log_level(SdsLogLevel level);
SdsLogLevel sds_get_log_level(void);
```

Can be called anytime (before or after `sds_init()`).

### 5.10 Schema Version

```c
// Get local schema version (from generated code)
const char* sds_get_schema_version(void);

// Set schema version (typically called from generated code)
void sds_set_schema_version(const char* version);
```

Schema version is included in status messages (`"sv":"1.0.0"`). If a mismatch is detected by the owner, the `SdsVersionMismatchCallback` is invoked.

### 5.11 Raw MQTT Publish

Publish arbitrary messages through the SDS-managed MQTT connection. Useful for logging, diagnostics, or application-specific messages:

```c
// Check if MQTT connection is active
bool sds_is_connected(void);

// Publish raw MQTT message
// topic: MQTT topic (null-terminated)
// payload: Message data
// payload_len: Length in bytes
// qos: 0, 1, or 2 (currently qos > 0 treated as 0)
// retained: Whether broker should retain message
SdsError sds_publish_raw(
    const char* topic,
    const void* payload,
    size_t payload_len,
    int qos,
    bool retained
);
```

**Example: Centralized logging**
```c
void remote_log(const char* message) {
    if (sds_is_connected()) {
        char topic[48];
        snprintf(topic, sizeof(topic), "log/%s", sds_get_node_id());
        sds_publish_raw(topic, message, strlen(message), 0, false);
    }
}
```

**Note**: The `sds/` topic prefix is reserved for internal SDS use.

### 5.12 Liveness Detection (Owner)

```c
// Check if a device is online (owner only)
// timeout_ms should typically be 1.5× the @liveness interval
bool sds_is_device_online(
    const void* owner_table,
    const char* table_type,
    const char* node_id,
    uint32_t timeout_ms
);

// Get liveness interval for a table type
uint32_t sds_get_liveness_interval(const char* table_type);
```

A device is considered online if:
- We have received status from it
- The `online` flag is true (not cleared by LWT)
- Last message was within `timeout_ms`

### 5.13 LWT (Last Will and Testament) Handling

SDS uses MQTT LWT to detect unexpected device disconnections. Each node publishes a retained LWT message on connection that the broker delivers if the node disconnects unexpectedly.

**LWT Topic:** `sds/lwt/{node_id}`

**LWT Payload:**
```json
{"online":false,"node":"sensor_01","ts":0}
```

Owner nodes automatically subscribe to `sds/lwt/+` and receive LWT messages for all devices. When an LWT is received:
1. The device's `online` flag is set to `false` in all relevant status slots
2. The `last_seen_ms` is updated
3. If `eviction_grace_ms > 0`, an eviction timer starts
4. The status callback is invoked

### 5.14 Device Eviction (Owner)

When a device goes offline (LWT received), SDS can automatically evict it from status slots after a configurable grace period. This prevents slots from being permanently consumed by devices that never reconnect.

**Configuration:** Set `eviction_grace_ms` in `SdsConfig` (0 = disabled).

```c
SdsConfig config = {
    .node_id = "owner",
    .mqtt_broker = "localhost",
    .eviction_grace_ms = 60000  // Evict after 60 seconds offline
};
```

**Eviction Callback:**
```c
typedef void (*SdsDeviceEvictedCallback)(
    const char* table_type,
    const char* node_id,
    void* user_data
);

// Set global eviction callback (called for all tables)
void sds_on_device_evicted(const char* table_type, SdsDeviceEvictedCallback cb, void* user_data);

// Get the configured eviction grace period
uint32_t sds_get_eviction_grace(const char* table_type);
```

**Eviction Flow:**
1. Device goes offline (LWT received)
2. `eviction_pending = true`, `eviction_deadline = now + eviction_grace_ms`
3. If device reconnects (status message with `online:true`), eviction is cancelled
4. If grace period expires, device is evicted:
   - `slot.valid = false`
   - `status_count` decremented
   - Eviction callback invoked
   - Slot can now be reused by a new device

### 5.15 JSON Serialization API

```c
// Writer API
void sds_json_writer_init(SdsJsonWriter* w, char* buffer, size_t size);
void sds_json_start_object(SdsJsonWriter* w);
void sds_json_end_object(SdsJsonWriter* w);
void sds_json_add_string(SdsJsonWriter* w, const char* key, const char* value);
void sds_json_add_int(SdsJsonWriter* w, const char* key, int32_t value);
void sds_json_add_uint(SdsJsonWriter* w, const char* key, uint32_t value);
void sds_json_add_float(SdsJsonWriter* w, const char* key, float value);
void sds_json_add_bool(SdsJsonWriter* w, const char* key, bool value);
const char* sds_json_get_string(SdsJsonWriter* w);
size_t sds_json_get_length(SdsJsonWriter* w);

// Reader API
void sds_json_reader_init(SdsJsonReader* r, const char* json, size_t len);
bool sds_json_get_string_field(SdsJsonReader* r, const char* key, char* out, size_t out_size);
bool sds_json_get_int_field(SdsJsonReader* r, const char* key, int32_t* out);
bool sds_json_get_uint_field(SdsJsonReader* r, const char* key, uint32_t* out);
bool sds_json_get_uint8_field(SdsJsonReader* r, const char* key, uint8_t* out);
bool sds_json_get_float_field(SdsJsonReader* r, const char* key, float* out);
bool sds_json_get_bool_field(SdsJsonReader* r, const char* key, bool* out);
```

### 5.16 Table Metadata Registry

The codegen generates a complete metadata registry that enables the simple `sds_register_table()` API.

```c
// Complete metadata for a table type (generated by codegen)
typedef struct {
    const char* table_type;             // Table type name (e.g., "SensorData")
    uint32_t sync_interval_ms;          // Default sync interval
    uint32_t liveness_interval_ms;      // Heartbeat interval (device role)
    
    // Struct sizes
    size_t device_table_size;           // sizeof({Table}Table)
    size_t owner_table_size;            // sizeof({Table}OwnerTable)
    
    // Section offsets and sizes for DEVICE role
    size_t dev_config_offset;
    size_t dev_config_size;
    size_t dev_state_offset;
    size_t dev_state_size;
    size_t dev_status_offset;
    size_t dev_status_size;
    
    // Section offsets and sizes for OWNER role
    size_t own_config_offset;
    size_t own_config_size;
    size_t own_state_offset;
    size_t own_state_size;
    
    // Function pointers to generated serializers
    SdsSerializeFunc serialize_config;
    SdsSerializeFunc serialize_state;
    SdsSerializeFunc serialize_status;
    SdsDeserializeFunc deserialize_config;
    SdsDeserializeFunc deserialize_state;
    SdsDeserializeFunc deserialize_status;
} SdsTableMeta;

// Set the registry (called automatically by generated constructor)
void sds_set_table_registry(const SdsTableMeta* registry, size_t count);

// Look up table metadata by name
const SdsTableMeta* sds_find_table_meta(const char* table_type);
```

**How it works:**
1. Codegen generates `SDS_TABLE_REGISTRY[]` array with all metadata
2. Codegen generates `#define SDS_SCHEMA_VERSION "x.y.z"` from `@version`
3. A constructor function auto-registers the array and schema version before `main()` runs
4. `sds_register_table()` looks up the table type and uses the metadata
5. No runtime `switch(type)` - just direct function pointer calls

## 6. Schema Syntax

```sds
@version = "1.0.0"

table SensorData {
    @sync_interval = 1000   // Check for changes every 1s
    @liveness = 3000        // Send heartbeat every 3s (even if no changes)
    
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

table ActuatorData {
    @sync_interval = 100    // Fast sync for responsive control
    @liveness = 1500        // Quick heartbeat for critical systems
    
    config {
        uint8 target_position;
        uint8 speed = 50;
    }
    
    state {
        uint8 current_position;
    }
    
    status {
        uint8 motor_status;
        uint16 error_code;
    }
}
```

### Schema Annotations

| Annotation | Description | Default |
|------------|-------------|---------|
| `@version` | Schema version string (checked at runtime) | "1.0.0" |
| `@sync_interval` | Change detection interval in ms | 1000 |
| `@liveness` | Max time between status publishes (heartbeat) in ms | 30000 |

## 7. Platform Abstraction

### 7.1 Platform Interface

```c
// sds_platform.h
typedef struct {
    // MQTT
    bool (*mqtt_connect)(const char* broker, uint16_t port, const char* client_id);
    void (*mqtt_disconnect)(void);
    bool (*mqtt_connected)(void);
    bool (*mqtt_publish)(const char* topic, const void* payload, size_t len, bool retained);
    bool (*mqtt_subscribe)(const char* topic);
    void (*mqtt_loop)(void);
    void (*mqtt_set_callback)(void (*cb)(const char* topic, const void* payload, size_t len));
    
    // Timing
    uint32_t (*millis)(void);
    void (*delay_ms)(uint32_t ms);
    
    // Logging
    void (*log_print)(const char* msg);
} SdsPlatform;

void sds_set_platform(const SdsPlatform* platform);
```

### 7.2 Platform Implementations

| Platform | MQTT Library | Build System |
|----------|--------------|--------------|
| ESP32/Arduino | PubSubClient | PlatformIO |
| macOS/Linux | Paho MQTT C | CMake |

## 8. Project Structure

```
sds/
├── include/
│   ├── sds.h                   # Public API ✅
│   ├── sds_error.h             # Error codes ✅
│   ├── sds_json.h              # JSON serialization API ✅
│   ├── sds_platform.h          # Platform abstraction interface ✅
│   └── SDS.h                   # Arduino C++ wrapper ✅
├── src/
│   ├── sds_core.c              # Main implementation (init, loop, tables, sync) ✅
│   └── sds_json.c              # JSON serialization implementation ✅
├── platform/
│   ├── esp32/
│   │   └── sds_platform_esp32.cpp  # PubSubClient wrapper ✅
│   └── posix/
│       └── sds_platform_posix.c    # Paho MQTT C wrapper ✅
├── codegen/
│   ├── __init__.py             # Package exports ✅
│   ├── parser.py               # Schema parser ✅
│   ├── c_generator.py          # C struct/function generator ✅
│   └── cli.py                  # CLI tool ✅
├── examples/
│   ├── linux_owner/
│   │   └── main.c              # Basic owner example ✅
│   └── esp32_sensor/
│       ├── esp32_sensor.ino    # ESP32 sensor device example ✅
│       └── platformio.ini      # PlatformIO config ✅
├── tests/
│   ├── test_basic.c            # Init/connectivity tests ✅
│   ├── test_multi_node.c       # 3-node manual test ✅
│   ├── test_generated.c        # Test with generated types ✅
│   └── test_simple_api.c       # Test simple registry-based API ✅
├── build/                      # CMake build output
├── schema.sds                  # Example schema ✅
├── CMakeLists.txt              # POSIX build ✅
├── library.json                # PlatformIO library config ✅
├── DESIGN.md                   # This document ✅
├── README.md                   # Quick start guide ✅
└── Doxyfile                    # Doxygen configuration ✅
```

## 9. Implementation Phases

### Phase 1: Foundation ✅ COMPLETED
- [x] Project structure setup (`include/`, `src/`, `platform/`, `tests/`, `examples/`)
- [x] Platform abstraction layer (`sds_platform.h` interface)
- [x] POSIX platform implementation with Paho MQTT C (`sds_platform_posix.c`)
- [x] Core initialization (`sds_init`, `sds_loop`, `sds_shutdown`, `sds_is_ready`)
- [x] CMake build system with Paho MQTT dependency detection
- [x] Basic connectivity test (`test_basic.c`)

### Phase 2: Table Management ✅ COMPLETED
- [x] Table registration with role (`sds_register_table`, `sds_register_table_ex`)
- [x] Internal `SdsTableContext` tracking (up to `SDS_MAX_TABLES`)
- [x] Shadow copies for change detection (config, state, status sections)
- [x] Section offset/size tracking for memory comparison
- [x] Serialization callback registration (`SdsSerializeFunc`, `SdsDeserializeFunc`)
- [x] Simple JSON library (`sds_json.h/c`) for serialization

### Phase 3: Config Flow ✅ COMPLETED
- [x] Owner publishes config on registration (retained)
- [x] Owner publishes config on change (shadow comparison)
- [x] Device subscribes to `sds/{table}/config`
- [x] Device receives and deserializes config via callback
- [x] Config callback invocation (`SdsConfigCallback`)

### Phase 4: State Flow ✅ COMPLETED
- [x] All nodes publish state to `sds/{table}/state`
- [x] Owner subscribes and receives state messages
- [x] LWW merge into owner's state struct (last arrival wins)
- [x] State callback with source `from_node_id` (`SdsStateCallback`)
- [x] Self-message filtering (owner ignores own state)

### Phase 5: Status Flow ✅ COMPLETED
- [x] Device publishes status to `sds/{table}/status/{node_id}`
- [x] Owner subscribes with wildcard `sds/{table}/status/+`
- [x] Status callback with source `from_node_id` (`SdsStatusCallback`)
- [x] Multi-node test validation (`test_multi_node.c`)

### Phase 6: Codegen ✅ COMPLETED
- [x] Parser for `.sds` schema files (`codegen/parser.py`)
- [x] Generator for `{Table}Table` (device struct)
- [x] Generator for `{Table}OwnerTable` (owner struct with status slots)
- [x] Serialize/deserialize functions generation
- [x] CLI tool: `python -m codegen.cli schema.sds -o sds_types.h`
- [x] Test with generated code (`test_generated.c`)

### Phase 7: ESP32 Platform ✅ COMPLETED
- [x] PubSubClient integration (`platform/esp32/sds_platform_esp32.cpp`)
- [x] Platform implementation with WiFi/MQTT support
- [x] PlatformIO library config (`library.json`)
- [x] Arduino-friendly C++ wrapper (`SDS.h` with `SDSClient` class)
- [x] ESP32 sensor example (`examples/esp32_sensor/`)
- [x] Build file for PlatformIO (`src/sds.cpp`)

### Phase 8: Simple Registration API ✅ COMPLETED
- [x] `SdsTableMeta` type definition with complete metadata (offsets, sizes, callbacks)
- [x] `sds_set_table_registry()` and `sds_find_table_meta()` functions
- [x] Codegen generates complete `SDS_TABLE_REGISTRY[]` with function pointers
- [x] Auto-registration via `__attribute__((constructor))` - no manual setup needed
- [x] `sds_register_table()` now uses registry lookup (no explicit callbacks needed)
- [x] Removed generated helper functions (`sds_register_{table}_device/owner`)
- [x] Updated all examples to use simple API
- [x] New `test_simple_api.c` test validating registry-based registration
- [x] Refactored core to use `alloc_table_slot()` helper (no recursion)

### Phase 9: Hardware Testing & Examples
- [x] Linux multi-node test (3 nodes with mixed roles)
- [ ] ESP32 sensor device example with real hardware
- [ ] Integration test with real broker and ESP32 hardware

## 10. JSON Message Format

Messages use a flat structure (no nested "fields" object) for simplicity:

### 10.1 Config Message
```json
{
    "ts": 1706000000,
    "from": "owner_node_1",
    "mode": 2,
    "threshold": 25.5,
    "sample_rate": 1000
}
```

### 10.2 State Message
```json
{
    "ts": 1706000000,
    "node": "sensor_A3B2C1",
    "temperature": 23.5,
    "humidity": 45.0,
    "reading_count": 42
}
```

### 10.3 Status Message
```json
{
    "ts": 1706000000,
    "online": true,
    "sv": "1.0.0",
    "error_code": 0,
    "battery_level": 87,
    "uptime_ms": 3600000
}
```

| Field | Description |
|-------|-------------|
| `ts` | Timestamp (millis) |
| `online` | Device online flag (always true in normal messages, false in LWT/shutdown) |
| `sv` | Schema version string |

### 10.4 Delta Updates (v0.5.0+)

When `enable_delta_sync` is enabled in `SdsConfig`, **state** and **status** messages only include fields that have changed since the last sync. This significantly reduces bandwidth usage for tables with many fields.

**Full State Message (delta disabled):**
```json
{
    "ts": 1706000000,
    "node": "sensor_A3B2C1",
    "temperature": 23.5,
    "humidity": 45.0,
    "reading_count": 42
}
```

**Delta State Message (delta enabled, only temperature changed):**
```json
{
    "ts": 1706000001,
    "node": "sensor_A3B2C1",
    "temperature": 24.0
}
```

**Important notes:**
- **Config messages are always full** (retained on broker for new subscribers)
- **Status liveness heartbeats are full** (sent on liveness timer expiry)
- Delta sync requires field metadata from codegen (schema-driven registration)
- Manual registration via `sds_register_table_ex()` uses full sync only
- Float comparisons use configurable tolerance (`delta_float_tolerance`)

**Configuration:**
```c
SdsConfig config = {
    .node_id = "sensor_01",
    .mqtt_broker = "192.168.1.100",
    .mqtt_port = 1883,
    .enable_delta_sync = true,         // Enable delta updates
    .delta_float_tolerance = 0.001f    // Ignore float changes < 0.001
};
```

## 10.5 Building and Testing (POSIX)

### Prerequisites
```bash
# macOS
brew install libpaho-mqtt

# Ubuntu/Debian
apt-get install libpaho-mqtt-dev
```

### Build
```bash
cd sds
mkdir build && cd build
cmake ..
make
```

### Run Tests
```bash
# Requires MQTT broker running (e.g., mosquitto)
# Terminal 1
./test_multi_node node1 localhost

# Terminal 2
./test_multi_node node2 localhost

# Terminal 3
./test_multi_node node3 localhost
```

### Test Topology
```
node1: TableA=OWNER,  TableB=DEVICE  → publishes TableA config, receives TableB config
node2: TableA=DEVICE, TableB=OWNER   → receives TableA config, publishes TableB config
node3: TableA=DEVICE, TableB=DEVICE  → receives both configs
```

## 11. Design Decisions & Open Questions

### Decided

1. **Owner uniqueness enforcement**: 
   - **Decision**: Application-level coordination (no SDS enforcement)
   - If two nodes register as OWNER, both will publish config; last one wins

2. **Node ID generation**: 
   - **Decision**: Explicit ID preferred, auto-generate fallback
   - Auto-generated format: `node_{timestamp}` on POSIX
   - ESP32 will use MAC-based ID

3. **JSON library**: 
   - **Decision**: Custom minimal JSON (`sds_json.h/c`)
   - Lightweight, no external dependencies
   - Same code for POSIX and ESP32

4. **Retransmit policy**: 
   - **Decision**: Left to application layer
   - SDS publishes on change detection only

5. **Serialization callback signature**:
   - **Decision**: Section pointer passed (not full table)
   - Simpler callbacks, no offset calculations needed in user code

6. **Registration API approach**:
   - **Decision**: Registry-based lookup with generated metadata
   - Simple API: `sds_register_table(&table, "Type", role, NULL)`
   - No runtime `switch(type)` in core library
   - Codegen generates `SdsTableMeta` array with function pointers
   - Auto-registration via constructor attribute (no manual setup)
   - Alternative explicit API (`sds_register_table_ex`) still available

### Open Questions

1. **Status slot management**: How to handle slot overflow when > `SDS_MAX_NODES` devices?
   - **Partially addressed**: Eviction grace period (`eviction_grace_ms`) allows automatic cleanup of offline devices
   - Current: Callback invoked but no slot storage for overflow
   - Remaining options: LRU eviction for active devices, dynamic allocation

2. **Reconnection behavior**: Should retained config be re-published on reconnect?
   - Current: Subscriptions restored, but no explicit re-publish

3. **Timestamp handling**: Use local millis or require NTP sync?
   - Current: Local `millis()` for relative ordering

---

## Appendix A: Comparison with DDS

| Feature | DDS (Current) | SDS (New) |
|---------|---------------|-----------|
| Table types | Global + Local | Single type |
| Hierarchy | Hub-and-spoke with controller | Fluid (any node, any role) |
| Config source | Always controller | Any OWNER node |
| State visibility | Device↔Controller | All→Owner only |
| Status aggregation | API-based | Struct array in OwnerTable |
| Schema | `global table`, `local table` | `table` |
| Complexity | Higher | Lower |

## Appendix B: Registration API Options

SDS provides two ways to register tables:

### Option 1: Simple API (Recommended)
```c
#include "sds_types.h"  // Auto-registers via constructor
sds_register_table(&table, "SensorNode", SDS_ROLE_DEVICE, NULL);
```
- Uses generated metadata registry (auto-registered)
- Minimal boilerplate - just one line!
- Table type verified at runtime

### Option 2: Explicit API
```c
sds_register_table_ex(
    &table, "SensorNode", SDS_ROLE_DEVICE, NULL,
    offsetof(SensorNodeTable, config), sizeof(SensorNodeConfig),
    offsetof(SensorNodeTable, state), sizeof(SensorNodeState),
    offsetof(SensorNodeTable, status), sizeof(SensorNodeStatus),
    NULL, sensor_node_deserialize_config,
    sensor_node_serialize_state, NULL,
    sensor_node_serialize_status, NULL
);
```
- Full control over callbacks
- Useful for custom serialization or testing
- Does not require codegen registry

| Aspect | Simple API | Explicit API |
|--------|-----------|--------------|
| Lines of code | 1 | 8+ |
| Flexibility | Low | High |
| Requires codegen | Yes | No |

