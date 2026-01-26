# Device Liveness Detection - Design Document

**Status:** ✅ Implemented  
**Author:** AI Assistant  
**Date:** January 2026

---

## Overview

This document describes the design for device liveness detection in SDS. The goal is to allow owners to know when devices go offline, with fast detection for crashes and reliable coverage for all failure modes.

## Problem Statement

Currently, if a device goes offline (crash, network failure, power loss), the owner has no way to detect this. The owner's status data becomes stale with no indication.

## Solution: Two-Layer Liveness Detection

| Layer | Mechanism | Detects | Speed |
|-------|-----------|---------|-------|
| **Application** | Heartbeat timeout | No messages in X seconds | Configurable |
| **Protocol** | MQTT Last Will (LWT) | TCP connection drop | ~1.5× keepalive |

Both mechanisms work together:
- **LWT** provides fast detection when device crashes
- **Heartbeat** provides reliable detection when network degrades slowly

---

## Schema Changes

### New Annotation: `@liveness`

```sds
table SensorNode {
    @sync_interval = 1000    // Check for changes every 1s
    @liveness = 3000        // Max 3s between status publishes
    
    config { ... }
    state { ... }
    status {
        uint8 error_code;
        uint8 battery_percent;
    }
}
```

| Annotation | Purpose | Default |
|------------|---------|---------|
| `@sync_interval` | How often to check for data changes | 1000ms |
| `@liveness` | Max time between status publishes (heartbeat) | 30000ms |

**Note:** `@liveness` applies to status messages only. If not specified, defaults to `SDS_DEFAULT_LIVENESS_MS` (30 seconds).

---

## Device-Side Behavior

### Optimized Heartbeat Logic

Regular messages count as heartbeats. The device only sends an explicit heartbeat if no other message was sent within the liveness interval.

```
┌─────────────────────────────────────────────────────────────────┐
│                    Device sds_loop() Logic                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. Check @sync_interval timer                                  │
│     └─ If data changed → publish status → reset liveness timer  │
│                                                                 │
│  2. Check @liveness timer                                       │
│     └─ If expired AND nothing sent recently → publish heartbeat │
│                                                                 │
│  Result: Minimum traffic, maximum liveness guarantee            │
└─────────────────────────────────────────────────────────────────┘
```

### Timeline Example

```
@sync_interval = 1000ms
@liveness = 3000ms

Time    Data Changed?    Action                    Last Publish
────────────────────────────────────────────────────────────────
0s      -               Initial status             0s
1s      YES             Publish (changed)          1s
2s      YES             Publish (changed)          2s
3s      NO              Skip (no change)           2s
4s      NO              Skip                       2s
5s      NO              → HEARTBEAT (3s elapsed)   5s
6s      YES             Publish (changed)          6s
```

### LWT Setup on Connect

When device connects, it registers a Last Will and Testament with the broker:

```
LWT Topic:   sds/{TableType}/status/{node_id}
LWT Payload: {"online":false,"node":"{node_id}","ts":0}
LWT Retain:  true
LWT QoS:     1
```

If the device crashes or loses network, the broker automatically publishes this message.

### Graceful Disconnect

On intentional shutdown, device should:
1. Publish `{"online":false,...}` with current timestamp
2. Call `mqtt.disconnect()` (prevents broker from sending LWT)

---

## Broker Behavior

The MQTT broker handles LWT automatically (MQTT specification feature):

| Event | Broker Action |
|-------|---------------|
| Client connects with LWT | Stores the will message |
| Client disconnects gracefully | Discards the will (no publish) |
| Client connection drops unexpectedly | Publishes the will message |

No broker configuration needed - this is standard MQTT behavior.

---

## Owner-Side Behavior

### StatusSlot Structure (Enhanced)

```c
typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    bool valid;
    uint32_t last_seen_ms;    // Updated on every received status
    bool online;              // From "online" field in message
    SensorNodeStatus status;  // Deserialized status data
} SensorNodeStatusSlot;
```

### On Receiving Status Message

```c
void handle_status_message(ctx, from_node, payload, len) {
    // 1. Find or allocate slot for this device
    StatusSlot* slot = find_or_alloc_status_slot(ctx, from_node);
    
    // 2. Update last_seen timestamp (ALWAYS)
    slot->last_seen_ms = sds_platform_millis();
    
    // 3. Parse "online" field (defaults to true)
    bool online = true;
    sds_json_get_bool_field(&r, "online", &online);
    slot->online = online;
    
    // 4. Deserialize status data (only if online)
    if (online) {
        ctx->deserialize_status(&slot->status, &r);
    }
    
    // 5. Invoke user callback
    if (ctx->status_callback) {
        ctx->status_callback(ctx->table_type, from_node);
    }
}
```

### Owner API: Check Device Liveness

```c
/**
 * Check if a device is currently online.
 * 
 * A device is considered online if:
 * - We have a valid status slot for it
 * - The "online" flag is true (not set false by LWT)
 * - Last message was received within timeout
 * 
 * @param owner_table   Pointer to owner table
 * @param table_type    Table type name
 * @param node_id       Device node ID to check
 * @param timeout_ms    Liveness timeout (typically 1.5× @liveness)
 * @return true if device is online
 */
bool sds_is_device_online(
    const void* owner_table,
    const char* table_type,
    const char* node_id,
    uint32_t timeout_ms
);
```

### Example Owner Code

```c
// Check all devices periodically
void check_device_health() {
    uint32_t timeout = 4500;  // 1.5× liveness (3s)
    
    sds_foreach_node(&owner_table, "SensorNode", check_device, &timeout);
}

void check_device(const char* node_id, const void* status, void* user_data) {
    uint32_t timeout = *(uint32_t*)user_data;
    
    if (!sds_is_device_online(&owner_table, "SensorNode", node_id, timeout)) {
        printf("ALERT: Device %s is OFFLINE\n", node_id);
    }
}
```

---

## Detection Scenarios

| Scenario | Detection Method | Time to Detect |
|----------|------------------|----------------|
| Device crashes | LWT from broker | ~1.5× MQTT keepalive (~90s default) |
| Device power loss | LWT from broker | ~1.5× MQTT keepalive |
| Network dies (clean TCP close) | LWT from broker | Immediate |
| Network dies (no TCP close) | Heartbeat timeout | @liveness + tolerance |
| Device frozen/hung | Heartbeat timeout | @liveness + tolerance |
| Graceful shutdown | Explicit `online:false` | Immediate |

---

## Configuration Recommendations

| Parameter | Recommended Value | Notes |
|-----------|-------------------|-------|
| `@liveness` | 30000ms | Balance between traffic and detection speed |
| MQTT keepalive | 60s | Standard; broker detects dead TCP in ~90s |
| Owner timeout | 1.5× @liveness | Allow for network jitter |

### For Battery-Powered Devices

```sds
table BatteryNode {
    @sync_interval = 60000   // Check every 60s (save power)
    @liveness = 300000       // Heartbeat every 5 minutes
    ...
}
```

### For Critical Systems

```sds
table CriticalNode {
    @sync_interval = 100     // Fast updates
    @liveness = 5000         // Heartbeat every 5s
    ...
}
```

---

## Implementation Plan

### Phase 1: Core Liveness

| File | Changes |
|------|---------|
| `codegen/parser.py` | Parse `@liveness` annotation |
| `codegen/c_generator.py` | Generate `liveness_interval_ms` in `SdsTableMeta` |
| `include/sds.h` | Add `liveness_interval_ms` to metadata, `last_seen_ms`/`online` to slots |
| `src/sds_core.c` | Add `last_publish_ms` tracking, liveness timer logic |

### Phase 2: LWT Support

| File | Changes |
|------|---------|
| `include/sds_platform.h` | Add LWT parameters to `sds_platform_mqtt_connect()` |
| `platform/esp32/sds_platform_esp32.cpp` | Pass LWT to PubSubClient connect |
| `platform/posix/sds_platform_posix.c` | Pass LWT to Paho connect options |
| `src/sds_core.c` | Build LWT topic/payload, pass to platform |

### Phase 3: Owner API

| File | Changes |
|------|---------|
| `include/sds.h` | Add `sds_is_device_online()` declaration |
| `src/sds_core.c` | Implement `sds_is_device_online()`, update `handle_status_message()` |

### Phase 4: Testing

| Test | Purpose |
|------|---------|
| Unit test | Liveness timer logic |
| Integration test | LWT delivery on simulated crash |
| Multi-node test | Owner detects device going offline |

---

## Open Questions

1. **Default @liveness value?** 
   - Proposed: 30 seconds
   - Could be 0 (disabled) to maintain backward compatibility

2. **Should LWT be optional?**
   - Some deployments may not want retained offline messages
   - Proposed: Enabled by default, can be disabled in `SdsConfig`

3. **Status section required for liveness?**
   - Currently, liveness only makes sense with status
   - Tables without status section would skip liveness

---

## Summary

```
Device                          Broker                          Owner
───────────────────────────────────────────────────────────────────────
Connect (set LWT) ──────────────► Store LWT
                                                                 
Publish status ─────────────────► Forward ─────────────────────► last_seen=now
                                                                 online=true
                                                                 
(3s, no changes)
Heartbeat ──────────────────────► Forward ─────────────────────► last_seen=now
                                                                 
(crash!)
                                  Publish LWT ─────────────────► online=false
                                                                 ALERT!
```

This design provides fast, reliable device liveness detection with minimal traffic overhead.

