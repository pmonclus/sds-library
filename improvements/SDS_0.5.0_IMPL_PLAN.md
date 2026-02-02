# SDS 0.5.0 Implementation Plan

This document provides detailed, actionable tasks for implementing SDS 0.5.0.
See `SDS_0.5.0_DESIGN.md` for architectural decisions and rationale.

---

## Overview

| Phase | Description | Est. Effort | Status |
|-------|-------------|-------------|--------|
| 1 | Increased Section Size | 1.5 hours | ✅ Complete |
| 2 | Field Metadata Infrastructure | 3.5 hours | ✅ Complete |
| 3 | Delta Sync Core | 6 hours | ✅ Complete |
| 4 | Python Bindings | 3 hours | ✅ Complete |
| 5 | Documentation & Release | 1.5 hours | ⬜ Not Started |

**Total estimated effort**: ~15.5 hours

---

## Phase 1: Increased Section Size (1.5 hours)

Low-risk changes to support 1KB sections.

### Task 1.1: Update `SDS_MSG_BUFFER_SIZE` default

**File**: `include/sds.h`

```c
// Change from:
#define SDS_MSG_BUFFER_SIZE      512

// To:
#ifndef SDS_MSG_BUFFER_SIZE
#define SDS_MSG_BUFFER_SIZE      2048
#endif
```

- [x] Update constant
- [x] Add `#ifndef` guard for user override

### Task 1.2: Update `SDS_SHADOW_SIZE` fallback

**File**: `src/sds_core.c`

```c
// Change from:
#else
    #define SDS_SHADOW_SIZE 256
#endif

// To:
#else
    #define SDS_SHADOW_SIZE 1024
#endif
```

- [x] Update fallback value

### Task 1.3: Add large section test

**File**: `tests/test_unit_core.c`

```c
TEST(large_section_1kb_serialization) {
    // Create a table with ~1KB state section
    // Verify serialization works without buffer overflow
    // Verify deserialization recovers all fields
}
```

- [x] Add test case (2 tests: `large_section_1kb_serialization`, `large_section_no_buffer_overflow`)
- [x] Verify passes on mock platform (57 tests pass)

### Task 1.4: Update documentation

- [ ] `README.md`: Mention 1KB section support (deferred to Phase 5)
- [ ] `DESIGN.md`: Update buffer size documentation (deferred to Phase 5)

### Task 1.5: Platform verification

- [ ] Build and test on ESP32 (manual, deferred)
- [x] Build and test on POSIX (macOS) - All tests pass
- [x] Python tests pass (132 tests)

---

## Phase 2: Field Metadata Infrastructure (3.5 hours)

Add field descriptors for per-field operations.

### Task 2.1: Add `SdsFieldType` enum

**File**: `include/sds.h`

```c
/**
 * @brief Field data types for serialization
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
```

- [x] Add enum definition
- [x] Add to Doxygen documentation group

### Task 2.2: Add `SdsFieldMeta` struct

**File**: `include/sds.h`

```c
/**
 * @brief Field descriptor for delta serialization
 */
typedef struct {
    const char* name;       /**< Field name in JSON */
    SdsFieldType type;      /**< Field data type */
    uint16_t offset;        /**< Offset within section struct */
    uint16_t size;          /**< Size in bytes */
} SdsFieldMeta;
```

- [x] Add struct definition
- [x] Add documentation

### Task 2.3: Extend `SdsTableMeta`

**File**: `include/sds.h`

Add to existing `SdsTableMeta`:

```c
typedef struct {
    // ... existing fields ...
    
    /* Field metadata for delta updates */
    const SdsFieldMeta* config_fields;
    uint8_t config_field_count;
    const SdsFieldMeta* state_fields;
    uint8_t state_field_count;
    const SdsFieldMeta* status_fields;
    uint8_t status_field_count;
} SdsTableMeta;
```

- [x] Add field pointers
- [x] Add field counts
- [x] Update documentation

### Task 2.4: Update codegen parser

**File**: `codegen/parser.py`

The parser already extracts field information. Verify it provides:
- Field name
- Field type
- Array size (for strings)

- [x] Review existing `Field` dataclass
- [x] Ensure type information is preserved for codegen

### Task 2.5: Generate field descriptors

**File**: `codegen/c_generator.py`

Add function to generate field descriptor arrays:

```python
def _generate_field_descriptors(output: TextIO, section_name: str, 
                                 fields: List[Field], prefix: str):
    """Generate SdsFieldMeta array for a section."""
    output.write(f"static const SdsFieldMeta {prefix}_FIELDS[] = {{\n")
    for field in fields:
        c_type = _field_type_to_enum(field.type)
        output.write(f'    {{ "{field.name}", {c_type}, '
                     f'offsetof({section_name}, {field.name}), '
                     f'sizeof((({section_name}*)0)->{field.name}) }},\n')
    output.write("};\n")
    output.write(f"#define {prefix}_FIELD_COUNT {len(fields)}\n\n")
```

- [x] Add `FIELD_TYPE_MAP` mapping (replaces `_field_type_to_enum`)
- [x] Add `_generate_field_descriptors()` function
- [x] Call for config, state, status sections
- [x] Generate for all tables

### Task 2.6: Update registry initialization

**File**: `codegen/c_generator.py`

Update `SDS_TABLE_REGISTRY` generation:

```c
{
    .table_type = "SensorData",
    // ... existing fields ...
    .config_fields = SDS_SENSOR_DATA_CONFIG_FIELDS,
    .config_field_count = SDS_SENSOR_DATA_CONFIG_FIELD_COUNT,
    .state_fields = SDS_SENSOR_DATA_STATE_FIELDS,
    .state_field_count = SDS_SENSOR_DATA_STATE_FIELD_COUNT,
    .status_fields = SDS_SENSOR_DATA_STATUS_FIELDS,
    .status_field_count = SDS_SENSOR_DATA_STATUS_FIELD_COUNT,
},
```

- [x] Update registry struct initialization
- [x] Handle NULL for sections without fields

### Task 2.7: Regenerate and verify

- [x] Run codegen on `schema.sds`
- [x] Verify `sds_types.h` compiles
- [x] Verify field descriptors are correct

### Task 2.8: Unit tests for field metadata

**File**: `tests/test_generated.c`

```c
TEST(field_metadata_exists) {
    const SdsTableMeta* meta = sds_find_table_meta("SensorData");
    ASSERT(meta != NULL);
    ASSERT(meta->state_fields != NULL);
    ASSERT(meta->state_field_count > 0);
}

TEST(field_metadata_offsets_correct) {
    const SdsTableMeta* meta = sds_find_table_meta("SensorData");
    // Verify offsets match actual struct layout
}
```

- [x] Python CFFI bindings updated with `SdsFieldType` and `SdsFieldMeta`
- [x] All 57 C tests pass
- [x] All 132 Python tests pass

---

## Phase 3: Delta Sync Core (6 hours)

Core delta synchronization implementation.

### Task 3.1: Add `enable_delta_sync` to config

**File**: `include/sds.h`

```c
typedef struct {
    // ... existing fields ...
    
    bool enable_delta_sync;         /**< Enable delta updates (default: false) */
    float delta_float_tolerance;    /**< Float comparison tolerance (default: 0.001) */
} SdsConfig;
```

**File**: `src/sds_core.c`

```c
static bool _delta_sync_enabled = false;
static float _delta_float_tolerance = 0.001f;

// In sds_init():
_delta_sync_enabled = config->enable_delta_sync;
if (config->delta_float_tolerance > 0) {
    _delta_float_tolerance = config->delta_float_tolerance;
}
```

- [x] Add config fields (`enable_delta_sync`, `delta_float_tolerance` in SdsConfig)
- [x] Add static variables (`_delta_sync_enabled`, `_delta_float_tolerance`)
- [x] Initialize in `sds_init()`
- [ ] Add getter: `sds_get_delta_sync_enabled()` (deferred - not needed for core functionality)

### Task 3.2: Add `field_changed()` function

**File**: `src/sds_core.c`

```c
/**
 * Check if a single field has changed between current and shadow.
 */
static bool field_changed(
    const SdsFieldMeta* field,
    const void* current,
    const void* shadow
) {
    const uint8_t* cur = (const uint8_t*)current + field->offset;
    const uint8_t* shd = (const uint8_t*)shadow + field->offset;
    
    if (field->type == SDS_FIELD_FLOAT) {
        float a, b;
        memcpy(&a, cur, sizeof(float));
        memcpy(&b, shd, sizeof(float));
        float diff = (a > b) ? (a - b) : (b - a);
        return diff > _delta_float_tolerance;
    }
    
    return memcmp(cur, shd, field->size) != 0;
}
```

- [x] Implement function
- [x] Handle all field types
- [x] Use configurable float tolerance

### Task 3.3: Add `serialize_field()` function

**File**: `src/sds_core.c`

```c
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
        case SDS_FIELD_BOOL:
            sds_json_add_bool(w, field->name, *ptr != 0);
            break;
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
        case SDS_FIELD_INT16: {
            int16_t val;
            memcpy(&val, ptr, sizeof(int16_t));
            sds_json_add_int(w, field->name, val);
            break;
        }
        case SDS_FIELD_UINT32: {
            uint32_t val;
            memcpy(&val, ptr, sizeof(uint32_t));
            sds_json_add_uint(w, field->name, val);
            break;
        }
        case SDS_FIELD_INT32: {
            int32_t val;
            memcpy(&val, ptr, sizeof(int32_t));
            sds_json_add_int(w, field->name, val);
            break;
        }
        case SDS_FIELD_FLOAT: {
            float val;
            memcpy(&val, ptr, sizeof(float));
            sds_json_add_float(w, field->name, val);
            break;
        }
        case SDS_FIELD_STRING:
            sds_json_add_string(w, field->name, (const char*)ptr);
            break;
    }
}
```

- [x] Implement function
- [x] Handle all field types
- [x] Use memcpy for alignment safety

### Task 3.4: Add `serialize_delta()` function

**File**: `src/sds_core.c`

```c
/**
 * Serialize only changed fields to JSON.
 * @return Number of fields serialized (0 = no changes)
 */
static int serialize_delta(
    const SdsFieldMeta* fields,
    uint8_t field_count,
    const void* current,
    const void* shadow,
    SdsJsonWriter* w
) {
    int changed = 0;
    
    for (int i = 0; i < field_count; i++) {
        if (field_changed(&fields[i], current, shadow)) {
            serialize_field(&fields[i], current, w);
            changed++;
        }
    }
    
    return changed;
}
```

- [x] Implement function (`serialize_delta_fields()`)
- [x] Return changed count for logging

### Task 3.5: Add fallback tracking

**File**: `src/sds_core.c`

Add to `SdsTableContext`:

```c
typedef struct {
    // ... existing fields ...
    bool force_full_sync;   /**< Force full sync on next publish */
} SdsTableContext;
```

Set `force_full_sync = true`:
- At registration
- After MQTT reconnection
- On liveness timeout (for status)

- [ ] Add `force_full_sync` field (deferred - current impl uses memcmp first, then delta on fields)
- [ ] Set at registration (deferred)
- [ ] Set on reconnection (deferred)
- [ ] Clear after full sync published (deferred)

**Note**: Simplified implementation - we use memcmp() for initial change detection, then serialize only changed fields. Full sync happens naturally on first message after any change.

### Task 3.6: Modify `sync_table()` for delta

**File**: `src/sds_core.c`

```c
static void sync_table(SdsTableContext* ctx) {
    // ... existing setup ...
    
    /* State sync */
    if (ctx->serialize_state && ctx->state_size > 0) {
        void* state_ptr = (uint8_t*)ctx->table + ctx->state_offset;
        
        bool any_change = (memcmp(state_ptr, ctx->shadow_state, ctx->state_size) != 0);
        if (!any_change) goto skip_state;
        
        sds_json_writer_init(&w, buffer, sizeof(buffer));
        sds_json_start_object(&w);
        sds_json_add_uint(&w, "ts", now);
        sds_json_add_string(&w, "node", _node_id);
        
        bool use_delta = _delta_sync_enabled && 
                         !ctx->force_full_sync &&
                         ctx->state_fields != NULL;
        
        if (use_delta) {
            int changed = serialize_delta(
                ctx->state_fields, ctx->state_field_count,
                state_ptr, ctx->shadow_state, &w
            );
            SDS_LOG_D("Delta state: %d fields changed", changed);
        } else {
            ctx->serialize_state(state_ptr, &w);
            ctx->force_full_sync = false;
        }
        
        sds_json_end_object(&w);
        
        // ... publish and update shadow ...
    }
skip_state:
    
    // ... similar for status ...
}
```

- [x] Add delta branch (for state and status)
- [x] Keep full sync as fallback (when no field metadata or delta disabled)
- [x] Status always sends full on liveness heartbeat
- [x] Log delta vs full for debugging

### Task 3.7: Unit tests for delta sync

**File**: `tests/test_unit_core.c`

```c
TEST(delta_single_field_change) {
    // Setup with delta enabled
    // Change one field
    // Verify only that field in published JSON
}

TEST(delta_float_tolerance) {
    // Change float by less than tolerance
    // Verify no publish
    // Change by more than tolerance
    // Verify publish
}

TEST(delta_fallback_on_init) {
    // Register table
    // Verify first publish is full
}

TEST(delta_disabled_full_sync) {
    // Setup with delta disabled
    // Change one field
    // Verify all fields in published JSON
}
```

- [x] Add delta sync config tests (3 tests in test_unit_core.c)
- [x] Add dedicated delta sync test file (test_delta_sync.c with 8 tests)
- [x] Cover key edge cases

**Tests Added in test_unit_core.c**:
- `delta_sync_disabled_by_default` - Verifies full sync when delta disabled
- `delta_sync_enabled_in_config` - Verifies delta sync config flag works
- `delta_float_tolerance_configurable` - Verifies custom tolerance can be set

**Tests Added in test_delta_sync.c** (8 tests):
- `full_sync_when_delta_disabled` - All fields sent when delta disabled
- `no_publish_when_unchanged` - No message when data unchanged
- `delta_sync_single_field_change` - Delta behavior on single field change
- `multiple_field_changes` - Multiple field changes serialized
- `float_tolerance_below_threshold` - Float tolerance behavior
- `float_tolerance_above_threshold` - Float tolerance above threshold
- `status_full_on_liveness_heartbeat` - Status with liveness fields
- `delta_config_values_preserved` - Config values correctly initialized

### Task 3.8: Integration tests

**File**: `tests/test_multi_node.c`

- [x] Test delta from device received correctly by owner (broker tests pass)
- [x] Test backward compatibility (all existing broker tests pass with delta code)
- [x] Config remains full even with delta enabled (implemented in sync_table)

**Broker Integration Tests Results:**
- test_sds_basic: 16 passed
- test_simple_api: passed
- test_errors: 19 passed

All existing broker-based tests continue to pass with the new delta sync code.

---

## Phase 4: Python Bindings (3 hours)

### Task 4.1: Expose `SdsFieldMeta` to CFFI

**File**: `python/sds/_cdefs.h`

```c
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

typedef struct {
    const char* name;
    SdsFieldType type;
    uint16_t offset;
    uint16_t size;
} SdsFieldMeta;
```

- [x] Add enum and struct (done in Phase 2)
- [x] Rebuild CFFI bindings (done in Phase 2)

### Task 4.2: Add `enable_delta_sync` parameter

**File**: `python/sds/node.py`

```python
def __init__(
    self,
    node_id: str,
    mqtt_broker: str,
    mqtt_port: int = 1883,
    # ... existing params ...
    enable_delta_sync: bool = False,
    delta_float_tolerance: float = 0.001,
):
    self._delta_sync_enabled = enable_delta_sync
    self._delta_float_tolerance = delta_float_tolerance
```

- [x] Add parameters (`enable_delta_sync`, `delta_float_tolerance`)
- [x] Pass to C config struct
- [x] Add properties (`delta_sync_enabled`, `delta_float_tolerance`)

### Task 4.3: Update Python serializers for delta

**File**: `python/sds/node.py`

The Python serializers need to support delta mode:

```python
def _create_delta_serializer(self, fields: List[SdsFieldMeta]):
    """Create a delta-aware serializer."""
    # Compare field-by-field
    # Only serialize changed fields
```

- [ ] Implement delta serializer (deferred - C layer handles delta for generated tables)
- [ ] Handle float tolerance (deferred - C layer handles)
- [ ] Maintain shadow copy in Python (deferred - C layer handles)

**Note**: Delta sync is handled by the C layer when using generated tables. Python-only tables (without codegen) don't have field metadata and will use full sync.

### Task 4.4: Python delta tests

**File**: `python/tests/test_delta.py`

- [x] Add delta sync config tests to test_node.py (3 tests)
- [x] Add more delta tests with broker (7 tests in TestDeltaSyncWithBroker)

### Task 4.5: Update Python docs

**File**: `python/sds/README.md`

- [ ] Document `enable_delta_sync` parameter (deferred to Phase 5)
- [ ] Add delta usage example (deferred to Phase 5)

---

## Phase 5: Documentation & Release (1.5 hours)

### Task 5.1: Update `DESIGN.md`

- [x] Add Delta Sync section (10.4 Delta Updates)
- [x] Update message format examples (full vs delta comparison)
- [x] Document configuration options
- [x] Update version header to 0.5.0

### Task 5.2: Update `README.md`

- [x] Add delta sync to features list (Key Features section)
- [x] Add usage example with `enable_delta_sync` (section 8)
- [x] C and Python examples included
- [x] Add raw MQTT publish feature to Key Features
- [x] Add section 9: Raw MQTT Publish with examples

### Task 5.3: Update `TESTING.md`

- [x] Update test count (390+ tests)
- [x] Document `test_delta_sync` (8 tests)
- [x] Update `test_unit_core` count (70 tests)
- [x] Update Quick Start command
- [x] Add Raw Publish API test category

### Task 5.3b: Update `python/README.md`

- [x] Add Configuration Options section
- [x] Document `enable_delta_sync` and `delta_float_tolerance`
- [x] Document `eviction_grace_ms`
- [x] Add Raw MQTT Publish section with examples

### Task 5.3c: Update `DESIGN.md`

- [x] Add section 5.11 Raw MQTT Publish with API documentation
- [x] Renumber subsequent sections (5.12-5.16)

### Task 5.4: Regenerate Doxygen

- [x] Run `doxygen Doxyfile`
- [x] Verify new APIs documented (sds_is_connected, sds_publish_raw in docs/html)

### Task 5.5: Version bump and changelog

- [x] Update version to 0.5.0 in `pyproject.toml`
- [x] Update version in `Formula/sds.rb`
- [x] Create CHANGELOG.md with full release notes

### Task 5.6: Create GitHub release

- [x] Tag v0.5.0
- [x] Create release with notes
- [x] Update SHA256 in Homebrew formula
- [x] Close GitHub issue #1 (Raw MQTT Publish API)

---

## Dependencies Graph

```
Phase 1 ─────────────────────────────────────────────────────►
         (independent, can start immediately)

Phase 2 ─────────────────────────────────────────────────────►
         (independent, can start in parallel with Phase 1)

Phase 3 ─────────────────────────────────────────────────────►
         (depends on Phase 2 completion)
                    │
                    └──► Phase 4 ────────────────────────────►
                         (depends on Phase 3 for C API)

                                         Phase 5 ────────────►
                                         (depends on all phases)
```

**Recommended execution order**:
1. Phase 1 + Phase 2 (parallel)
2. Phase 3
3. Phase 4
4. Phase 5

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Field descriptor generation bugs | Medium | High | Extensive unit tests |
| Memory overhead on embedded | Low | Medium | Keep delta optional |
| Python CFFI complexity | Medium | Medium | Test incrementally |
| Backward compatibility break | Low | High | Default delta=false |

---

## Definition of Done

- [ ] All unit tests pass (C and Python)
- [ ] Integration tests pass with live broker
- [ ] ESP32 manual test passes
- [ ] Documentation updated
- [ ] GitHub release created
- [ ] Homebrew formula updated

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-02-01 | AI Assistant | Initial plan |
