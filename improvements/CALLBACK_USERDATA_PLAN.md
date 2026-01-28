# Callback User Data Plan

## Problem

Current callback API only passes table name, forcing users to use global pointers:

```c
static DeviceDemoTable* g_table = NULL;  // Hack: global for callback access

void on_config_update(const char* table_type) {
    g_table->config.led_control;  // Must use global
}
```

## Proposed Solution

Add `void* user_data` parameter to all callback types and registration functions.

### API Changes

**Before:**
```c
typedef void (*SdsConfigCallback)(const char* table_type);
typedef void (*SdsStateCallback)(const char* table_type, const char* from_node);
typedef void (*SdsStatusCallback)(const char* table_type, const char* from_node);

void sds_on_config_update(const char* table_type, SdsConfigCallback callback);
void sds_on_state_update(const char* table_type, SdsStateCallback callback);
void sds_on_status_update(const char* table_type, SdsStatusCallback callback);
```

**After:**
```c
typedef void (*SdsConfigCallback)(const char* table_type, void* user_data);
typedef void (*SdsStateCallback)(const char* table_type, const char* from_node, void* user_data);
typedef void (*SdsStatusCallback)(const char* table_type, const char* from_node, void* user_data);

void sds_on_config_update(const char* table_type, SdsConfigCallback callback, void* user_data);
void sds_on_state_update(const char* table_type, SdsStateCallback callback, void* user_data);
void sds_on_status_update(const char* table_type, SdsStatusCallback callback, void* user_data);
```

### Usage After Change

```c
void on_config_update(const char* table_type, void* user_data) {
    DeviceDemoTable* table = (DeviceDemoTable*)user_data;
    
    if (table->config.led_control != led_state) {
        led_state = table->config.led_control;
        printf("[LED] %s\n", led_state ? "ON" : "OFF");
    }
}

int main() {
    DeviceDemoTable table = {0};
    sds_register_table(&table, "DeviceDemo", SDS_ROLE_DEVICE, NULL);
    sds_on_config_update("DeviceDemo", on_config_update, &table);  // Pass table pointer
    // ...
}
```

## Implementation Steps

| Step | File | Change |
|------|------|--------|
| 1 | `include/sds.h` | Update 3 callback typedefs, 3 function declarations |
| 2 | `src/sds_core.c` | Store user_data per callback, pass to invocations |
| 3 | `python/sds/_cdefs.h` | Update CFFI declarations |
| 4 | `python/sds/node.py` | Update Python callback wrappers |
| 5 | All examples | Add `NULL` or actual pointer as 3rd argument |
| 6 | All tests | Add `NULL` as 3rd argument |
| 7 | `examples/hybrid_demo/lib/` | Sync copied library files |
| 8 | Docs | Regenerate Doxygen |

**Estimated: ~20-25 call sites to update**

## Multiple Tables

When an application uses multiple tables, use **separate callbacks for each table**:

```c
// Tables
SensorDataTable sensor_table = {0};
ActuatorDataTable actuator_table = {0};

// Callbacks - each knows its own type
void on_sensor_config(const char* table_type, void* user_data) {
    SensorDataTable* table = (SensorDataTable*)user_data;
    // Handle sensor config...
}

void on_actuator_config(const char* table_type, void* user_data) {
    ActuatorDataTable* table = (ActuatorDataTable*)user_data;
    // Handle actuator config...
}

int main() {
    // Register tables
    sds_register_table(&sensor_table, "SensorData", SDS_ROLE_DEVICE, NULL);
    sds_register_table(&actuator_table, "ActuatorData", SDS_ROLE_DEVICE, NULL);
    
    // Register callbacks - each with its matching table pointer
    sds_on_config_update("SensorData", on_sensor_config, &sensor_table);
    sds_on_config_update("ActuatorData", on_actuator_config, &actuator_table);
}
```

**Note:** The developer is responsible for matching the correct callback and table pointer.
Wiring them incorrectly (e.g., passing `&actuator_table` to `on_sensor_config`) causes
undefined behavior. This is standard C - the compiler cannot catch `void*` type mismatches.

## Benefits

1. **No globals needed** - cleaner, more maintainable code
2. **Thread-safe** - each callback has its own context
3. **Multiple tables** - each table gets its own callback with correct type
4. **Standard C pattern** - familiar to C developers (like pthread, qsort, etc.)

## Potential Problems

| Problem | Impact | Mitigation |
|---------|--------|------------|
| **Breaking change** | All existing callbacks must update signature | Clear migration: add `void* user_data` param, even if unused |
| **Memory management** | User must ensure user_data outlives callback | Document: "user_data must remain valid while callback is registered" |
| **Python wrapper complexity** | CFFI needs to handle user_data | Python already uses closures, can ignore user_data or pass through |
| **Copied lib in examples** | hybrid_demo/lib/ gets out of sync | Update generate.sh or document manual sync |

## Migration Path

For existing code, migration is simple:

```c
// Old callback
void on_config(const char* table_type) { ... }

// New callback (just add unused parameter)
void on_config(const char* table_type, void* user_data) {
    (void)user_data;  // Suppress unused warning
    // ... existing code unchanged ...
}

// Old registration
sds_on_config_update("Table", on_config);

// New registration
sds_on_config_update("Table", on_config, NULL);
```

## Decision

- [x] **IMPLEMENTED** - Cleaner API with user_data parameter

## Implementation Status

All changes completed and tested:

- [x] `include/sds.h` - Updated callback typedefs and function declarations
- [x] `src/sds_core.c` - Store and pass user_data in callbacks
- [x] `python/sds/_cdefs.h` - Updated CFFI declarations
- [x] `python/sds/node.py` - Updated Python callback wrappers
- [x] All examples updated (hybrid_demo, esp32_sensor, linux_owner)
- [x] All tests updated (45+ C tests, 58+ Python tests pass)
- [x] `examples/hybrid_demo/lib/` synced with updated library

### Test Results

```
C Unit Tests:  177 tests passed
Python Tests:   58 tests passed, 29 skipped (require MQTT broker)
```
