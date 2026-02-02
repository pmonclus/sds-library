# SDS 0.5.0 Design Document

## Overview

This document specifies the design for SDS version 0.5.0, focusing on two main enhancements:
1. Increased maximum section size (1KB per section)
2. Delta updates (send only changed fields instead of full sections)

**Guiding Principle**: Maintain full API and wire-protocol backward compatibility with 0.4.x.

---

## Table of Contents

1. [Requirements](#1-requirements)
2. [MQTT Broker Assumptions](#2-mqtt-broker-assumptions)
3. [Requirement 1: Increased Section Size](#3-requirement-1-increased-section-size)
4. [Requirement 2: Delta Updates](#4-requirement-2-delta-updates)
5. [Implementation Plan](#5-implementation-plan)
6. [Testing Strategy](#6-testing-strategy)
7. [Migration Guide](#7-migration-guide)
8. [Open Questions](#8-open-questions)

---

## 1. Requirements

### 1.0 API Compatibility (MUST)

All existing 0.4.x code must compile and run without modification:
- No changes to public function signatures
- No changes to struct layouts that users access
- Wire protocol changes must be backward compatible
- New features enabled via opt-in configuration

### 1.1 Increased Section Size

- Increase max section size from 256 bytes to **1024 bytes (1KB)** per section
- Applies to: config, state, and status sections
- Must handle increased JSON payload sizes

### 1.2 Delta Updates

- Publish only fields that have changed, not the entire section
- Reduce MQTT message size and bandwidth
- Maintain receiver compatibility (partial JSON must be correctly handled)

---

## 2. MQTT Broker Assumptions

SDS relies on specific MQTT broker behaviors. This section documents assumptions and their implications for 0.5.0.

### 2.1 Current MQTT Usage in SDS 0.4.x

| Message Type | Topic Pattern | Retain | QoS | Notes |
|--------------|---------------|--------|-----|-------|
| **Config** (Owner→Devices) | `sds/{table}/config` | **Yes** | 0 | Ensures new subscribers get current config |
| **State** (All→Owner) | `sds/{table}/state` | No | 0 | Transient sensor data |
| **Status** (Device→Owner) | `sds/{table}/status/{node_id}` | No | 0 | Heartbeat with liveness |
| **LWT** (Broker→All) | `sds/lwt/{node_id}` | **Yes** | 1 | Broker delivers on unexpected disconnect |
| **Graceful Offline** | `sds/lwt/{node_id}` | **Yes** | 0 | Node publishes before clean disconnect |

**Note on LWT QoS**: LWT messages use QoS 1 across all platforms (ESP32, POSIX, Python) to ensure the broker delivers the offline notification even during transient network issues. This is a protocol-level requirement — LWT is a one-time critical notification with no application-level retry mechanism.

### 2.2 Retained Message Implications for Delta Updates

**Critical Issue**: Config messages use `retain=true`.

When config is retained:
```
Time T0: Owner publishes {"mode": 1, "threshold": 50} (retained)
Time T1: Owner publishes delta {"mode": 2} (retained)
Time T2: New device subscribes → receives ONLY {"mode": 2}
```

**Problem**: The new device misses `threshold` because the retained message only contains the delta.

**Solutions Considered**:

| Approach | Pros | Cons | Selected |
|----------|------|------|----------|
| **A. Always send full config** | Simple, backward compatible | No bandwidth savings for config | **Yes** |
| **B. Delta for config, full on request** | Bandwidth efficient | Requires resync mechanism | No |
| **C. Accumulated retained message** | Most bandwidth efficient | Complex broker logic | No |

**Decision**: For 0.5.0, **config messages will always be full** (no delta for config). Delta updates apply only to state and status, which are not retained.

### 2.3 Message Ordering Assumptions

MQTT guarantees in-order delivery per QoS level per topic. SDS relies on:

1. **Timestamp field (`ts`)**: Each message includes a timestamp for conflict resolution
2. **Last-Write-Wins (LWW)**: For state, the most recent timestamp wins
3. **No sequence numbers**: SDS does not use sequence numbers for ordering

**Implication for Delta**: If deltas arrive out of order:
```
Time T0: Device sends {"temp": 25, "ts": 1000}
Time T1: Device sends {"temp": 26, "ts": 1001}
Network reorder: Owner receives T1 first, then T0
```

With **full messages**, out-of-order delivery is harmless — the receiver applies the entire state, and LWW based on `ts` ensures the latest wins.

With **delta messages**, out-of-order delivery could cause issues:
- Receiver applies delta T1 (temp=26)
- Receiver then applies delta T0 (temp=25) — older value overwrites newer!

A robust solution would require **per-field timestamp tracking** at the receiver:
```c
// Hypothetical per-field LWW (adds complexity)
if (incoming_ts > field_last_ts[i]) {
    apply_field(field);
    field_last_ts[i] = incoming_ts;
}
```

**Decision for 0.5.0**: We will NOT implement per-field timestamp tracking at the receiver. Rationale:
1. Out-of-order delivery is rare with QoS 0 on a single MQTT topic
2. The next sync cycle (typically 1 second) will publish correct values
3. Per-field timestamps add memory overhead (4 bytes × fields × devices at owner)
4. Complexity cost outweighs benefit for typical IoT use cases

The existing message-level `ts` field remains in delta messages for logging/debugging purposes.

### 2.4 QoS Considerations

Currently, SDS uses QoS 0 for all data messages (fire-and-forget). This means:
- Messages may be lost during network issues
- No acknowledgment from broker

**For delta updates**: Lost deltas could cause state divergence.

**Mitigation**:
1. Periodic full sync (existing: heartbeat/liveness triggers full status)
2. Shadow comparison ensures next sync catches up
3. Future: Optional QoS 1 for critical sections

### 2.5 Schema Version in Messages

Status messages include schema version (`sv` field):
```json
{"ts": 12345, "online": true, "sv": "1.0.0", "temperature": 25.5}
```

This enables version mismatch detection. For delta updates:
- Include `sv` in every status message (even deltas)
- Receiver validates `sv` before applying any fields

---

## 3. Requirement 1: Increased Section Size

### 3.1 Current Limits

| Constant | Current Value | Location | Purpose |
|----------|---------------|----------|---------|
| `SDS_MSG_BUFFER_SIZE` | 512 | `sds.h:87` | JSON serialization buffer |
| `SDS_SHADOW_SIZE` | max(sections) | `sds_core.c:21` | Shadow copy for change detection |
| `SDS_GENERATED_MAX_SECTION_SIZE` | calculated | `sds_types.h` | Compile-time max from schema |

### 3.2 Proposed Changes

| Constant | New Value | Rationale |
|----------|-----------|-----------|
| `SDS_MSG_BUFFER_SIZE` | **2048** | 1KB struct → ~1.2-1.5KB JSON + metadata |
| `SDS_SHADOW_SIZE` | **1024** (fallback) | For manual registration without codegen |

The `SDS_GENERATED_MAX_SECTION_SIZE` will naturally increase when schemas define larger sections.

### 3.3 Memory Impact

**Per registered table**:
```
Current (256 bytes/section):
  3 shadows × 256 = 768 bytes

Proposed (1024 bytes/section):
  3 shadows × 1024 = 3072 bytes

Delta: +2304 bytes per table
```

**Maximum impact** (8 tables): +18KB RAM

**Mitigation**: The actual shadow size is calculated from the schema. Small schemas continue to use small shadows.

### 3.4 API Changes

None. `SDS_MSG_BUFFER_SIZE` and `SDS_SHADOW_SIZE` are internal constants. Users can already override them:

```c
#define SDS_MSG_BUFFER_SIZE 2048
#include "sds.h"
```

### 3.5 Code Changes

```c
// sds.h - Line 87
#ifndef SDS_MSG_BUFFER_SIZE
#define SDS_MSG_BUFFER_SIZE      2048  // Was 512
#endif

// sds_core.c - Line 23 (fallback only)
#ifndef SDS_GENERATED_MAX_SECTION_SIZE
    #define SDS_SHADOW_SIZE 1024  // Was 256
#endif
```

---

## 4. Requirement 2: Delta Updates

### 4.1 Design Goals

1. **Reduce message size**: Only transmit changed fields
2. **Maintain compatibility**: 0.4.x receivers must handle partial messages
3. **No state divergence**: Shadow synchronization ensures consistency
4. **Opt-in activation**: Controlled via `SdsConfig` flag

### 4.2 Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        sync_table()                              │
├─────────────────────────────────────────────────────────────────┤
│  For each field in section:                                      │
│    if (field_changed(current, shadow, field_meta)):             │
│      serialize_field(field, &json_writer)                       │
│      changed = true                                              │
│                                                                  │
│  if (changed):                                                   │
│    mqtt_publish(topic, json)                                     │
│    memcpy(shadow, current, section_size)  // Atomic update      │
└─────────────────────────────────────────────────────────────────┘
```

### 4.3 Field Metadata (New)

Inspired by the DDS library pattern, we add field descriptors.

**New Types** (in `sds.h` or generated):

```c
/**
 * @brief Field type enumeration for serialization
 */
typedef enum {
    SDS_FIELD_BOOL = 0,
    SDS_FIELD_UINT8,
    SDS_FIELD_INT8,
    SDS_FIELD_UINT16,
    SDS_FIELD_INT16,
    SDS_FIELD_UINT32,
    SDS_FIELD_INT32,
    SDS_FIELD_FLOAT,
    SDS_FIELD_STRING,
} SdsFieldType;

/**
 * @brief Field descriptor for delta serialization
 */
typedef struct {
    const char* name;       /**< Field name in JSON */
    SdsFieldType type;      /**< Field data type */
    uint16_t offset;        /**< Offset within section struct */
    uint16_t size;          /**< Size in bytes (for strings: buffer size) */
} SdsFieldMeta;
```

**Generated Code** (example from codegen):

```c
#define SDS_SENSOR_DATA_STATE_FIELD_COUNT 2

static const SdsFieldMeta SDS_SENSOR_DATA_STATE_FIELDS[] = {
    { "temperature", SDS_FIELD_FLOAT, offsetof(SensorDataState, temperature), sizeof(float) },
    { "humidity", SDS_FIELD_UINT8, offsetof(SensorDataState, humidity), sizeof(uint8_t) },
};
```

**Extended SdsTableMeta**:

```c
typedef struct {
    // ... existing fields ...
    
    /* Field metadata for delta updates (NULL if not available) */
    const SdsFieldMeta* config_fields;
    uint8_t config_field_count;
    const SdsFieldMeta* state_fields;
    uint8_t state_field_count;
    const SdsFieldMeta* status_fields;
    uint8_t status_field_count;
} SdsTableMeta;
```

### 4.4 Change Detection Algorithm

```c
/**
 * Check if a single field has changed between current and shadow.
 * 
 * Special handling for floats to avoid spurious updates from FP noise.
 */
static bool field_changed(
    const SdsFieldMeta* field,
    const void* current_section,
    const void* shadow_section
) {
    const uint8_t* cur = (const uint8_t*)current_section + field->offset;
    const uint8_t* shd = (const uint8_t*)shadow_section + field->offset;
    
    if (field->type == SDS_FIELD_FLOAT) {
        float a, b;
        memcpy(&a, cur, sizeof(float));
        memcpy(&b, shd, sizeof(float));
        float diff = (a > b) ? (a - b) : (b - a);
        return diff > 0.001f;  // Configurable tolerance
    }
    
    return memcmp(cur, shd, field->size) != 0;
}
```

### 4.5 Delta Serialization

```c
/**
 * Serialize only changed fields to JSON.
 * 
 * @return Number of fields serialized (0 = no changes)
 */
static int serialize_delta(
    const SdsFieldMeta* fields,
    uint8_t field_count,
    const void* current_section,
    const void* shadow_section,
    SdsJsonWriter* w
) {
    int changed_count = 0;
    
    for (int i = 0; i < field_count; i++) {
        if (field_changed(&fields[i], current_section, shadow_section)) {
            serialize_field(&fields[i], current_section, w);
            changed_count++;
        }
    }
    
    return changed_count;
}

/**
 * Serialize a single field to JSON.
 */
static void serialize_field(
    const SdsFieldMeta* field,
    const void* section,
    SdsJsonWriter* w
) {
    const uint8_t* ptr = (const uint8_t*)section + field->offset;
    
    switch (field->type) {
        case SDS_FIELD_FLOAT: {
            float val;
            memcpy(&val, ptr, sizeof(float));
            sds_json_add_float(w, field->name, val);
            break;
        }
        case SDS_FIELD_UINT8:
            sds_json_add_uint(w, field->name, *ptr);
            break;
        case SDS_FIELD_INT8:
            sds_json_add_int(w, field->name, *(int8_t*)ptr);
            break;
        case SDS_FIELD_UINT16: {
            uint16_t val;
            memcpy(&val, ptr, sizeof(uint16_t));
            sds_json_add_uint(w, field->name, val);
            break;
        }
        // ... other types ...
        case SDS_FIELD_STRING:
            sds_json_add_string(w, field->name, (const char*)ptr);
            break;
    }
}
```

### 4.6 Receiver Compatibility

**Good news**: SDS 0.4.x receivers already handle partial JSON correctly.

Current deserialize functions use `sds_json_get_*_field()` which:
- Return `false` if field is missing
- Do NOT modify the target value if field is missing

Example:
```c
// Generated deserializer (0.4.x)
static void sensor_data_deserialize_state(void* section, SdsJsonReader* r) {
    SensorDataState* st = (SensorDataState*)section;
    sds_json_get_float_field(r, "temperature", &st->temperature);  // Only writes if present
    sds_json_get_uint8_field(r, "humidity", &st->humidity);        // Only writes if present
}
```

If a delta message contains only `{"ts": 123, "temperature": 25.5}`:
- `temperature` is updated
- `humidity` remains unchanged (correct behavior!)

### 4.7 Configuration

New field in `SdsConfig`:

```c
typedef struct {
    // ... existing fields ...
    
    /**
     * Enable delta updates for state and status messages.
     * When true, only changed fields are published.
     * Config messages always remain full (due to MQTT retain).
     * Default: false (full sections for backward compatibility)
     */
    bool enable_delta_sync;
    
    /**
     * Float comparison tolerance for delta detection.
     * Fields differing by less than this are considered unchanged.
     * Default: 0.001f
     */
    float delta_float_tolerance;
    
} SdsConfig;
```

### 4.8 Wire Protocol Changes

**Current (0.4.x) state message**:
```json
{
  "ts": 1234567890,
  "node": "sensor_01",
  "temperature": 25.5,
  "humidity": 60
}
```

**Delta (0.5.0) state message** (only temperature changed):
```json
{
  "ts": 1234567891,
  "node": "sensor_01",
  "temperature": 26.0
}
```

**Metadata fields always included**: `ts`, `node`, `from`, `online`, `sv` (where applicable).

### 4.9 Fallback to Full Sync

Certain conditions trigger a full section sync even with delta enabled:

1. **Initial sync after registration**: First publish is always full
2. **After reconnection**: First publish after MQTT reconnect is full
3. **Liveness heartbeat**: Status with `online=true` is full (ensures fresh data)
4. **Manual trigger**: Future API for explicit full sync

### 4.10 Python Bindings Impact

The Python bindings use CFFI callbacks for serialization. Changes needed:

1. **Field metadata access**: Expose `SdsFieldMeta` to Python
2. **Delta serialization**: Implement in Python or call C implementation
3. **Configuration**: Expose `enable_delta_sync` in `SdsNode.__init__`

---

## 5. Implementation Plan

### Phase 1: Increased Section Size (Low Risk)

| Task | File(s) | Effort | Dependencies |
|------|---------|--------|--------------|
| 1.1 Update `SDS_MSG_BUFFER_SIZE` default | `sds.h` | 5 min | None |
| 1.2 Update `SDS_SHADOW_SIZE` fallback | `sds_core.c` | 5 min | None |
| 1.3 Update documentation | `README.md`, `DESIGN.md` | 15 min | 1.1, 1.2 |
| 1.4 Add large section tests | `tests/test_unit_core.c` | 30 min | 1.1, 1.2 |
| 1.5 Verify ESP32/POSIX platforms | Manual testing | 30 min | 1.4 |

**Estimated total**: 1.5 hours

### Phase 2: Field Metadata Infrastructure

| Task | File(s) | Effort | Dependencies |
|------|---------|--------|--------------|
| 2.1 Add `SdsFieldType` enum | `sds.h` | 10 min | None |
| 2.2 Add `SdsFieldMeta` struct | `sds.h` | 10 min | 2.1 |
| 2.3 Extend `SdsTableMeta` | `sds.h` | 15 min | 2.2 |
| 2.4 Update codegen parser | `codegen/parser.py` | 30 min | None |
| 2.5 Generate field descriptors | `codegen/c_generator.py` | 1 hour | 2.3, 2.4 |
| 2.6 Update registry initialization | `codegen/c_generator.py` | 30 min | 2.5 |
| 2.7 Unit tests for field metadata | `tests/test_generated.c` | 45 min | 2.5, 2.6 |

**Estimated total**: 3.5 hours

### Phase 3: Delta Sync Core

| Task | File(s) | Effort | Dependencies |
|------|---------|--------|--------------|
| 3.1 Add `field_changed()` function | `sds_core.c` | 30 min | 2.2 |
| 3.2 Add `serialize_field()` function | `sds_core.c` | 45 min | 2.2 |
| 3.3 Add `serialize_delta()` function | `sds_core.c` | 30 min | 3.1, 3.2 |
| 3.4 Add `enable_delta_sync` to config | `sds.h`, `sds_core.c` | 20 min | None |
| 3.5 Modify `sync_table()` for delta | `sds_core.c` | 1 hour | 3.3, 3.4 |
| 3.6 Add fallback full sync conditions | `sds_core.c` | 30 min | 3.5 |
| 3.7 Unit tests for delta sync | `tests/test_unit_core.c` | 1.5 hours | 3.5, 3.6 |
| 3.8 Integration tests | `tests/test_multi_node.c` | 1 hour | 3.7 |

**Estimated total**: 6 hours

### Phase 4: Python Bindings

| Task | File(s) | Effort | Dependencies |
|------|---------|--------|--------------|
| 4.1 Expose `SdsFieldMeta` to CFFI | `python/sds/_cdefs.h` | 20 min | 2.2 |
| 4.2 Add `enable_delta_sync` param | `python/sds/node.py` | 15 min | 3.4 |
| 4.3 Update serializers for delta | `python/sds/node.py` | 1 hour | 4.1 |
| 4.4 Python delta tests | `python/tests/test_delta.py` | 1 hour | 4.3 |
| 4.5 Update Python docs | `python/sds/README.md` | 20 min | 4.2 |

**Estimated total**: 3 hours

### Phase 5: Documentation & Release

| Task | File(s) | Effort | Dependencies |
|------|---------|--------|--------------|
| 5.1 Update `DESIGN.md` | `DESIGN.md` | 30 min | All |
| 5.2 Update `README.md` | `README.md` | 20 min | All |
| 5.3 Update `TESTING.md` | `TESTING.md` | 15 min | All tests |
| 5.4 Regenerate Doxygen | `docs/` | 10 min | 5.1 |
| 5.5 Update Homebrew formula | `Formula/sds.rb` | 10 min | Release |
| 5.6 Create GitHub release | GitHub | 15 min | All |

**Estimated total**: 1.5 hours

---

## 6. Testing Strategy

### 6.1 Unit Tests (Mock Platform)

| Test Case | Description | File |
|-----------|-------------|------|
| `test_field_changed_int` | Integer field change detection | `test_unit_core.c` |
| `test_field_changed_float` | Float with tolerance | `test_unit_core.c` |
| `test_field_changed_string` | String field change | `test_unit_core.c` |
| `test_delta_single_field` | Delta with one field changed | `test_unit_core.c` |
| `test_delta_multiple_fields` | Delta with several fields | `test_unit_core.c` |
| `test_delta_no_change` | No publish when unchanged | `test_unit_core.c` |
| `test_delta_fallback_initial` | Full sync on registration | `test_unit_core.c` |
| `test_delta_fallback_reconnect` | Full sync after reconnect | `test_unit_core.c` |
| `test_delta_disabled` | Full sync when delta off | `test_unit_core.c` |
| `test_large_section_1kb` | 1KB section serialization | `test_unit_core.c` |

### 6.2 Integration Tests (Live Broker)

| Test Case | Description | File |
|-----------|-------------|------|
| `test_delta_device_to_owner` | Device delta received by owner | `test_multi_node.c` |
| `test_delta_backward_compat` | 0.4.x receiver handles 0.5.0 delta | `test_multi_node.c` |
| `test_config_always_full` | Config never uses delta | `test_multi_node.c` |

### 6.3 Python Tests

| Test Case | Description | File |
|-----------|-------------|------|
| `test_delta_enabled_config` | `enable_delta_sync=True` works | `test_delta.py` |
| `test_delta_field_detection` | Field-level change detection | `test_delta.py` |
| `test_delta_float_tolerance` | Float tolerance respected | `test_delta.py` |

### 6.4 Manual Platform Tests

- [ ] ESP32: Large section (1KB) with delta
- [ ] POSIX (Linux): Multi-node delta test
- [ ] Raspberry Pi: Memory usage validation

---

## 7. Migration Guide

### 7.1 For 0.4.x Users

**No action required**. All existing code works unchanged:
- `enable_delta_sync` defaults to `false`
- Section size limits are backward compatible
- Wire protocol is backward compatible

### 7.2 Enabling Delta Updates

```c
SdsConfig config = {
    .node_id = "sensor_01",
    .mqtt_broker = "192.168.1.100",
    .enable_delta_sync = true,           // NEW: Enable delta
    .delta_float_tolerance = 0.01f,      // NEW: Optional tolerance
};
sds_init(&config);
```

### 7.3 Python

```python
node = SdsNode(
    "sensor_01",
    "192.168.1.100",
    enable_delta_sync=True,              # NEW
    delta_float_tolerance=0.01           # NEW
)
```

---

## 8. Open Questions

### 8.1 Decided

| Question | Decision | Rationale |
|----------|----------|-----------|
| Delta for config? | **No** | MQTT retain breaks with partial messages |
| Field-level callbacks? | **Not in 0.5.0** | Adds complexity; table-level sufficient |
| Float tolerance configurable? | **Yes** | Different sensors need different precision |
| Default delta enabled? | **No** | Backward compatibility first |

### 8.2 Open for Discussion

| Question | Options | Recommendation |
|----------|---------|----------------|
| Include `_changed` bitmask in message? | Yes/No | No - adds overhead, receiver doesn't need it |
| Per-table delta enable? | Global only / Per-table | Global only for simplicity |
| Force full sync API? | Add `sds_force_full_sync(table)` | Yes, useful for debugging |

---

## Appendix A: Comparison with DDS

The delta update design is inspired by the original DDS library (`/Users/pmonclus/code/edge/ref_code/test_mqtt`).

| Aspect | DDS | SDS 0.5.0 |
|--------|-----|-----------|
| Field descriptors | `DdsFieldDescriptor` | `SdsFieldMeta` (simplified) |
| Float tolerance | Hardcoded 0.01 | Configurable |
| JSON library | ArduinoJson (heavy) | Custom lightweight |
| `"fields"` wrapper | Yes | No (flat JSON, compatible) |
| Consistency levels | STRICT/EVENTUAL/NONE | EVENTUAL only |
| Field callbacks | Yes | Not in 0.5.0 |

---

## Appendix B: Wire Protocol Examples

### B.1 Full State Message (0.4.x and 0.5.0 with delta=false)

```json
{
  "ts": 1234567890,
  "node": "sensor_01",
  "temperature": 25.5,
  "humidity": 60,
  "pressure": 1013.25
}
```

### B.2 Delta State Message (0.5.0 with delta=true)

Only `temperature` changed:
```json
{
  "ts": 1234567891,
  "node": "sensor_01",
  "temperature": 26.0
}
```

### B.3 Status Message with Schema Version

```json
{
  "ts": 1234567892,
  "online": true,
  "sv": "0.5.0",
  "battery": 85,
  "rssi": -45
}
```

### B.4 Config Message (Always Full)

```json
{
  "ts": 1234567893,
  "from": "controller_01",
  "sample_rate": 1000,
  "threshold": 50,
  "mode": 2
}
```

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-02-01 | AI Assistant | Initial draft |
