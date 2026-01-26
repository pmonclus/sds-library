# SDS Library Critical Issues - Remediation Plan

**Created:** January 2026  
**Status:** ✅ All Issues Implemented (January 2026)

## Overview

This document identifies **8 critical issues** that need to be fixed before the SDS library is production-ready. Issues are prioritized by severity (security vulnerabilities first, then data corruption risks, then robustness issues).

---

## Issue 1: JSON String Injection Vulnerability

**Severity:** ⚠️ SECURITY - CRITICAL  
**Location:** `src/sds_json.c`, lines 64-70

### Problem

`sds_json_add_string()` writes string values directly without escaping special characters (`"`, `\`, control characters). A malicious or malformed string in a table field could:
- Break JSON structure
- Inject arbitrary JSON fields
- Cause parsing failures on receivers

### Current Code

```c
void sds_json_add_string(SdsJsonWriter* w, const char* key, const char* value) {
    // ...
    json_append(w, value);  // No escaping!
    // ...
}
```

### Fix Plan

1. Create a new helper function `json_append_escaped()` in `sds_json.c`
2. Escape these characters: `"` → `\"`, `\` → `\\`, newline → `\n`, tab → `\t`, carriage return → `\r`
3. Replace `json_append(w, value)` with `json_append_escaped(w, value)` in `sds_json_add_string()`
4. Add bounds checking to prevent buffer overflow during escaping

### Files to Modify

- `src/sds_json.c`

### Estimated Effort

Medium (~30 lines of code)

---

## Issue 2: Shadow Buffer Size Limitation

**Severity:** ⚠️ DATA CORRUPTION - CRITICAL  
**Location:** `src/sds_core.c`, lines 37-42, 394-398

### Problem

Shadow buffers are hardcoded to 256 bytes, and sizes are silently capped:

```c
uint8_t shadow_config[256];  // Fixed size
// ...
ctx->config_size = config_size < 256 ? config_size : 256;  // Silent truncation!
```

If a table section exceeds 256 bytes, the shadow comparison uses truncated data, causing:
- Incorrect change detection
- Partial data syncing
- Silent data loss

### Fix Plan

**Approach: Auto-calculate from codegen**

The code generator already knows the exact sizes of all sections. Instead of a manual compile-time constant, the codegen will calculate and output the required maximum size.

#### Step 1: Modify codegen (`codegen/c_generator.py`)

Add compile-time MAX calculation to `sds_types.h`:

```c
/* Maximum section size (auto-calculated from schema) */
#define _SDS_MAX2(a, b) ((a) > (b) ? (a) : (b))
#define _SDS_MAX3(a, b, c) _SDS_MAX2(_SDS_MAX2(a, b), c)

#define SDS_GENERATED_MAX_SECTION_SIZE _SDS_MAX3( \
    _SDS_MAX3(sizeof(SensorNodeConfig), sizeof(SensorNodeState), sizeof(SensorNodeStatus)), \
    _SDS_MAX3(sizeof(ActuatorNodeConfig), sizeof(ActuatorNodeState), sizeof(ActuatorNodeStatus)), \
    1  /* minimum size */ \
)
```

This uses C's compile-time constant evaluation to pick the largest section across all tables.

#### Step 2: Modify core (`src/sds_core.c`)

Use the generated value with a fallback default:

```c
/* Use generated max if available, otherwise default */
#ifdef SDS_GENERATED_MAX_SECTION_SIZE
    #define SDS_SHADOW_SIZE SDS_GENERATED_MAX_SECTION_SIZE
#else
    #define SDS_SHADOW_SIZE 256  /* Fallback for manual usage */
#endif

typedef struct {
    // ...
    uint8_t shadow_config[SDS_SHADOW_SIZE];
    uint8_t shadow_state[SDS_SHADOW_SIZE];
    uint8_t shadow_status[SDS_SHADOW_SIZE];
    // ...
} SdsTableContext;
```

#### Step 3: Add runtime validation (safety net)

Even with codegen-calculated sizes, add runtime validation in `sds_register_table_ex()`:

```c
if (config_size > SDS_SHADOW_SIZE || state_size > SDS_SHADOW_SIZE || status_size > SDS_SHADOW_SIZE) {
    SDS_LOG_E("Section size exceeds SDS_SHADOW_SIZE (%d bytes)", SDS_SHADOW_SIZE);
    return SDS_ERR_SECTION_TOO_LARGE;
}
```

This catches mismatches if codegen and library versions differ.

#### Step 4: Add error code

Add `SDS_ERR_SECTION_TOO_LARGE` to `sds_error.h`.

### Benefits of This Approach

| Benefit | Description |
|---------|-------------|
| **Self-sizing** | Buffer is exactly the size needed for the schema |
| **Memory efficient** | No wasted RAM if sections are small |
| **Compile-time safe** | Size is guaranteed correct for the defined structures |
| **Transparent** | Users can see the calculated size in generated code |
| **Backwards compatible** | Falls back to 256 if codegen not used |

### Files to Modify

- `codegen/c_generator.py` (add MAX calculation output)
- `src/sds_core.c` (use generated size, add validation)
- `include/sds_error.h` (add new error code)

### Estimated Effort

Medium (~30 lines: 15 in codegen, 10 in core, 5 in error.h)

---

## Issue 3: Broker String Not Copied

**Severity:** ⚠️ MEMORY SAFETY - HIGH  
**Location:** `src/sds_core.c`, line 155

### Problem

The broker hostname is stored as a pointer, not copied:

```c
_mqtt_broker = config->mqtt_broker;  // Stores pointer only!
```

If the caller's string is on the stack or gets freed, the library will use dangling/invalid memory.

### Fix Plan

1. Add a static buffer: `static char _mqtt_broker_buf[128];`
2. In `sds_init()`, copy the string: `strncpy(_mqtt_broker_buf, config->mqtt_broker, sizeof(_mqtt_broker_buf) - 1);`
3. Set `_mqtt_broker = _mqtt_broker_buf;`
4. Validate length and return `SDS_ERR_INVALID_CONFIG` if broker string is too long

### Files to Modify

- `src/sds_core.c`

### Estimated Effort

Low (~10 lines of code)

---

## Issue 4: JSON Parsing Memory Safety

**Severity:** ⚠️ SECURITY - HIGH  
**Location:** `src/sds_json.c`, lines 179-190, 144-177

### Problem

Multiple issues in JSON parsing:
- `sds_json_parse_string()` reads until `'\0'` or `'"'` without checking against a length limit
- `sds_json_find_field()` can read past the JSON buffer if malformed (no null terminator)

### Current Code

```c
bool sds_json_parse_string(const char* value, char* out, size_t out_size) {
    if (!value || *value != '"') return false;
    value++;  // Skip opening quote
    size_t i = 0;
    while (*value && *value != '"' && i < out_size - 1) {  // Reads until \0 - unsafe!
        out[i++] = *value++;
    }
    // ...
}
```

### How to Calculate Max Length

The `SdsJsonReader` already tracks buffer bounds:

```c
typedef struct {
    const char* json;  // Start of JSON buffer
    size_t len;        // Total buffer length
    size_t pos;        // Current position
} SdsJsonReader;
```

When `sds_json_find_field()` returns a pointer to a value, the remaining safe bytes are:

```c
size_t remaining = r->len - (value_ptr - r->json);
```

### Fix Plan

**Option B (Recommended): Pass reader to parse functions**

This keeps bounds information encapsulated in the reader structure.

#### Step 1: Update parse function signatures

```c
// Old (unsafe)
bool sds_json_parse_string(const char* value, char* out, size_t out_size);

// New (bounded)
bool sds_json_parse_string(SdsJsonReader* r, const char* value, char* out, size_t out_size);
```

#### Step 2: Implement bounds checking

```c
bool sds_json_parse_string(SdsJsonReader* r, const char* value, char* out, size_t out_size) {
    if (!r || !value || !out || out_size == 0) return false;
    if (*value != '"') return false;
    
    // Calculate remaining bytes in buffer
    size_t remaining = r->len - (value - r->json);
    if (remaining < 2) return false;  // Need at least opening quote + something
    
    value++;  // Skip opening quote
    remaining--;
    
    size_t i = 0;
    while (remaining > 0 && *value != '"' && i < out_size - 1) {
        out[i++] = *value++;
        remaining--;
    }
    out[i] = '\0';
    
    return (*value == '"');  // Return true only if we found closing quote
}
```

#### Step 3: Update all callers

The generated deserialize functions and internal code need to pass the reader:

```c
// Before
sds_json_get_string_field(r, "name", buffer, sizeof(buffer));

// After (internal implementation uses reader bounds)
// No change to public API - bounds checking happens internally
```

#### Step 4: Add bounds checking to `sds_json_find_field()`

Ensure all loops respect `r->len`:

```c
const char* sds_json_find_field(SdsJsonReader* r, const char* key) {
    const char* p = r->json;
    const char* end = r->json + r->len;  // Hard boundary
    
    while (p < end) {
        // All iterations check p < end before dereferencing
        // ...
    }
}
```

### Files to Modify

- `src/sds_json.c` (update all parse functions with bounds checking)
- `include/sds_json.h` (update function signatures)

### Estimated Effort

Medium (~50 lines of code)

---

## Issue 5: Missing Input Validation in `sds_init()`

**Severity:** ⚠️ ROBUSTNESS - MEDIUM  
**Location:** `src/sds_core.c`, lines 132-176

### Problem

Several validation gaps:
- `config->node_id` is used without null check (only empty string check)
- No validation on broker hostname format or length
- No validation that node_id doesn't exceed buffer size

### Fix Plan

1. Add explicit null check for `config->node_id` before using it
2. Add maximum length validation for `node_id` and `mqtt_broker`
3. Return `SDS_ERR_INVALID_CONFIG` with meaningful error for each case
4. Add debug logging when using default values

### Files to Modify

- `src/sds_core.c`

### Estimated Effort

Low (~15 lines of code)

---

## Issue 6: Topic Parsing Edge Cases

**Severity:** ⚠️ ROBUSTNESS - MEDIUM  
**Location:** `src/sds_core.c`, lines 682-697

### Problem

Topic parsing has edge cases that aren't handled:
- Empty table_type (table_len == 0) not validated
- Topics like `sds//config` would pass through

### Current Code

```c
size_t table_len = table_end - table_start;
if (table_len >= SDS_MAX_TABLE_TYPE_LEN) return;
strncpy(table_type, table_start, table_len);
table_type[table_len] = '\0';
// No check for table_len == 0
```

### Fix Plan

1. Add validation for `table_len > 0`
2. Add validation for empty or whitespace-only table types
3. Log debug message for malformed topics

### Files to Modify

- `src/sds_core.c`

### Estimated Effort

Low (~5 lines of code)

---

## Issue 7: Incomplete Owner Status Handling

**Severity:** ⚠️ MISSING FEATURE - MEDIUM  
**Location:** `src/sds_core.c`, lines 656-670

### Problem

Status messages from devices are received but **never deserialized**:

```c
static void handle_status_message(...) {
    // ...
    /* TODO: Find or create status slot for from_node */
    /* For now, just invoke callback */
    SDS_LOG_I("Status received from %s: %s", from_node, ctx->table_type);
    // ...
    (void)payload;  // Payload is completely ignored!
    (void)len;
}
```

The owner never gets actual status data from devices.

### Current State Analysis

**What exists (but isn't connected):**

1. `SdsTableContext` has status slot fields (never populated):
   ```c
   size_t status_slots_offset;  /* Offset to status array in owner table */
   size_t status_slot_size;     /* Size of each status slot */
   uint8_t max_status_slots;
   ```

2. Owner tables have the status_slots array (generated by codegen):
   ```c
   typedef struct {
       char node_id[SDS_MAX_NODE_ID_LEN];
       bool valid;
       uint32_t last_seen_ms;
       SensorNodeStatus status;
   } SensorNodeStatusSlot;
   
   typedef struct {
       SensorNodeConfig config;
       SensorNodeState state;
       SensorNodeStatusSlot status_slots[SDS_GENERATED_MAX_NODES];  // <-- exists!
       uint8_t status_count;
   } SensorNodeOwnerTable;
   ```

**What's missing:**

1. `SdsTableMeta` doesn't have owner status slot metadata
2. Codegen doesn't output slot offset/size
3. Registration doesn't populate slot fields in `SdsTableContext`
4. `handle_status_message()` is a stub

### Fix Plan

#### Step 1: Extend `SdsTableMeta` (`include/sds.h`)

Add owner status slot metadata to the registry structure:

```c
typedef struct {
    // ... existing fields ...
    
    /* Owner status slot management (for per-device tracking) */
    size_t own_status_slots_offset;   /* offsetof(OwnerTable, status_slots) */
    size_t own_status_slot_size;      /* sizeof(StatusSlot) */
    size_t own_status_count_offset;   /* offsetof(OwnerTable, status_count) */
    uint8_t own_max_status_slots;     /* SDS_GENERATED_MAX_NODES */
    
    /* Offset to status data within a slot */
    size_t slot_status_offset;        /* offsetof(StatusSlot, status) */
} SdsTableMeta;
```

#### Step 2: Update Codegen (`codegen/c_generator.py`)

Output the new metadata in the table registry:

```python
# In _generate_table_registry():
if table.status_fields:
    output.write(f"        .own_status_slots_offset = offsetof({name}OwnerTable, status_slots),\n")
    output.write(f"        .own_status_slot_size = sizeof({name}StatusSlot),\n")
    output.write(f"        .own_status_count_offset = offsetof({name}OwnerTable, status_count),\n")
    output.write(f"        .own_max_status_slots = SDS_GENERATED_MAX_NODES,\n")
    output.write(f"        .slot_status_offset = offsetof({name}StatusSlot, status),\n")
else:
    output.write("        .own_status_slots_offset = 0,\n")
    # ... etc for all fields
```

#### Step 3: Update Registration (`src/sds_core.c`)

Populate status slot fields when registering an owner table:

```c
// In sds_register_table() for OWNER role:
if (role == SDS_ROLE_OWNER && meta->own_status_slots_offset > 0) {
    ctx->status_slots_offset = meta->own_status_slots_offset;
    ctx->status_slot_size = meta->own_status_slot_size;
    ctx->max_status_slots = meta->own_max_status_slots;
}
```

#### Step 4: Implement Slot Lookup Helper

Add a helper function to find or allocate a slot:

```c
static void* find_or_alloc_status_slot(SdsTableContext* ctx, const char* node_id) {
    if (ctx->status_slots_offset == 0 || ctx->max_status_slots == 0) {
        return NULL;  // No slots configured
    }
    
    uint8_t* slots_base = (uint8_t*)ctx->table + ctx->status_slots_offset;
    
    // Search for existing slot
    for (uint8_t i = 0; i < ctx->max_status_slots; i++) {
        uint8_t* slot = slots_base + (i * ctx->status_slot_size);
        char* slot_node_id = (char*)slot;  // node_id is first field
        bool* slot_valid = (bool*)(slot + SDS_MAX_NODE_ID_LEN);
        
        if (*slot_valid && strcmp(slot_node_id, node_id) == 0) {
            return slot;  // Found existing
        }
    }
    
    // Find empty slot
    for (uint8_t i = 0; i < ctx->max_status_slots; i++) {
        uint8_t* slot = slots_base + (i * ctx->status_slot_size);
        bool* slot_valid = (bool*)(slot + SDS_MAX_NODE_ID_LEN);
        
        if (!*slot_valid) {
            // Initialize new slot
            strncpy((char*)slot, node_id, SDS_MAX_NODE_ID_LEN - 1);
            *slot_valid = true;
            return slot;
        }
    }
    
    SDS_LOG_W("Status slots full, dropping status from %s", node_id);
    return NULL;  // All slots full
}
```

#### Step 5: Implement `handle_status_message()`

```c
static void handle_status_message(SdsTableContext* ctx, const char* from_node, 
                                   const uint8_t* payload, size_t len) {
    if (ctx->role != SDS_ROLE_OWNER) return;
    if (!ctx->deserialize_status) return;
    
    // Find or allocate slot for this device
    void* slot = find_or_alloc_status_slot(ctx, from_node);
    if (!slot) {
        return;  // No slot available
    }
    
    // Get pointer to status data within the slot
    // slot_status_offset would need to be stored or calculated
    const SdsTableMeta* meta = sds_find_table_meta(ctx->table_type);
    void* status_ptr = (uint8_t*)slot + meta->slot_status_offset;
    
    // Update last_seen timestamp
    uint32_t* last_seen = (uint32_t*)((uint8_t*)slot + SDS_MAX_NODE_ID_LEN + sizeof(bool));
    *last_seen = sds_platform_millis();
    
    // Deserialize status into slot
    SdsJsonReader r;
    sds_json_reader_init(&r, (const char*)payload, len);
    ctx->deserialize_status(status_ptr, &r);
    
    SDS_LOG_D("Status updated from %s: %s", from_node, ctx->table_type);
    
    if (ctx->status_callback) {
        ctx->status_callback(ctx->table_type, from_node);
    }
}
```

#### Step 6: Add API for Owner to Query Status

```c
// In sds.h - public API
const void* sds_get_device_status(const char* table_type, const char* node_id);
uint8_t sds_get_device_count(const char* table_type);
```

### Files to Modify

| File | Changes |
|------|---------|
| `include/sds.h` | Add 5 new fields to `SdsTableMeta`, add query API |
| `codegen/c_generator.py` | Output new metadata fields |
| `src/sds_core.c` | Populate fields, implement slot lookup, implement handler |
| `include/sds_types.h` | Regenerate with new metadata |

### Estimated Effort

High (~120 lines across multiple files)

### Implementation Approach

Implement **Option B (Full Implementation)** for the v1.0 release. This ensures complete functionality for owner nodes to track per-device status from the start.

**Rationale:** The infrastructure is already partially in place (status slots in owner tables), and shipping without this feature would leave a significant gap in the owner-device data flow.

---

## Issue 8: No Error Callback/Notification Mechanism

**Severity:** ⚠️ ENHANCEMENT - LOW  
**Location:** Throughout `sds_core.c`

### Problem

When errors occur (MQTT publish fails, buffer overflow, etc.), the library:
- Logs the error (if logging is enabled)
- Returns `false` or error code from functions
- But the application has no way to be notified of **async errors** that occur during `sds_loop()`

### Fix Plan

1. Add an error callback type: `typedef void (*SdsErrorCallback)(SdsError error, const char* context);`
2. Add `sds_set_error_callback(SdsErrorCallback cb)` function
3. Call the callback when critical errors occur
4. Allow callback to be NULL (errors just logged)

### Files to Modify

- `include/sds.h`
- `src/sds_core.c`

### Estimated Effort

Medium (~30 lines of code)

---

## Implementation Summary

### Priority Order

| Priority | Issue | Severity | Effort | Status |
|----------|-------|----------|--------|--------|
| **1** | JSON String Escaping | Security/Critical | Medium | ✅ Done |
| **2** | Broker String Copy | Memory Safety | Low | ✅ Done |
| **3** | Shadow Buffer Validation | Data Corruption | Medium | ✅ Done |
| **4** | JSON Parsing Bounds | Security | Medium | ✅ Done |
| **5** | Input Validation | Robustness | Low | ✅ Done |
| **6** | Topic Parsing Edge Cases | Robustness | Low | ✅ Done |
| **7** | Status Handler (Full) | Missing Feature | High | ✅ Done |
| **8** | Error Callback | Enhancement | Medium | ✅ Done |

### Phased Approach

**Phase 1 - Security Fixes (Issues 1, 4):**
- Add JSON string escaping
- Add JSON parsing bounds checking

**Phase 2 - Memory Safety (Issues 2, 3):**
- Copy broker string instead of storing pointer
- Add auto-calculated shadow buffer sizing via codegen
- Add runtime validation as safety net

**Phase 3 - Robustness & Status Handler (Issues 5, 6, 7):**
- Add input validation in `sds_init()`
- Improve topic parsing edge cases
- Implement full status handler:
  - Extend `SdsTableMeta` with status slot metadata
  - Update codegen to output slot offsets
  - Implement slot lookup and deserialization
  - Add owner query API

**Phase 4 - Enhancements (Issue 8):**
- Add error callback mechanism

---

## Testing Requirements

After fixes are implemented:

1. **Unit Tests:**
   - JSON escaping: test strings with quotes, backslashes, newlines
   - JSON parsing: test malformed JSON, missing terminators, oversized strings
   - Buffer validation: test oversized sections, verify codegen MAX calculation

2. **Integration Tests:**
   - End-to-end message flow with special characters
   - Stress test with large payloads

3. **Fuzz Testing (recommended):**
   - JSON parser fuzzing
   - Topic parser fuzzing

---

## Notes

- Issues marked as "NOT A BUG" during analysis:
  - Timing overflow: unsigned arithmetic handles wraparound correctly
  - Log buffer: `vsnprintf` is bounds-safe

- **Shadow buffer sizing**: With the codegen approach, the buffer size is automatically calculated from the schema. Users don't need to configure anything manually. The generated `SDS_GENERATED_MAX_SECTION_SIZE` macro ensures the buffer is exactly large enough for all defined sections.

- For manual usage (without codegen), the default fallback of 256 bytes should be sufficient for most IoT use cases.

