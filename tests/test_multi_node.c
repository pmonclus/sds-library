/*
 * test_multi_node.c - Multi-node data exchange test
 * 
 * Tests data flow between nodes with different roles:
 * 
 *   node1: TableA=OWNER,  TableB=DEVICE
 *   node2: TableA=DEVICE, TableB=OWNER
 *   node3: TableA=DEVICE, TableB=DEVICE
 * 
 * Usage:
 *   ./test_multi_node node1 [broker_ip]
 *   ./test_multi_node node2 [broker_ip]
 *   ./test_multi_node node3 [broker_ip]
 */

#include "sds.h"
#include "sds_json.h"
#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stddef.h>
#include <math.h>

/* ============== Table Definitions ============== */

/* TableA: Temperature sensor table */
typedef struct {
    uint8_t mode;
    float threshold;
    uint32_t sample_rate;
} TableAConfig;

typedef struct {
    float temperature;
    float humidity;
    uint32_t reading_count;
} TableAState;

typedef struct {
    uint8_t error_code;
    uint8_t battery_level;
    uint32_t uptime_ms;
} TableAStatus;

/* Device table for TableA */
typedef struct {
    TableAConfig config;
    TableAState state;
    TableAStatus status;
} TableADeviceTable;

/* Owner table for TableA */
#define MAX_NODES 8

typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    bool valid;
    uint32_t last_seen_ms;
    TableAStatus status;
} TableAStatusSlot;

typedef struct {
    TableAConfig config;
    TableAState state;
    TableAStatusSlot status_slots[MAX_NODES];
    uint8_t status_count;
} TableAOwnerTable;

/* TableB: Actuator control table */
typedef struct {
    uint8_t target_position;
    uint8_t speed;
    uint8_t enabled;  /* Use uint8_t instead of bool for simpler serialization */
} TableBConfig;

typedef struct {
    uint8_t current_position;
    uint8_t is_moving;
} TableBState;

typedef struct {
    uint8_t motor_status;
    uint16_t error_count;
} TableBStatus;

/* Device table for TableB */
typedef struct {
    TableBConfig config;
    TableBState state;
    TableBStatus status;
} TableBDeviceTable;

/* Owner table for TableB */
typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    bool valid;
    uint32_t last_seen_ms;
    TableBStatus status;
} TableBStatusSlot;

typedef struct {
    TableBConfig config;
    TableBState state;
    TableBStatusSlot status_slots[MAX_NODES];
    uint8_t status_count;
} TableBOwnerTable;

/* ============== Serialization Functions ============== */

/* TableA Config */
static void serialize_tableA_config(void* table, SdsJsonWriter* w) {
    TableAConfig* cfg = (TableAConfig*)table;
    sds_json_add_uint(w, "mode", cfg->mode);
    sds_json_add_float(w, "threshold", cfg->threshold);
    sds_json_add_uint(w, "sample_rate", cfg->sample_rate);
}

static void deserialize_tableA_config(void* table, SdsJsonReader* r) {
    TableAConfig* cfg = (TableAConfig*)table;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
    sds_json_get_float_field(r, "threshold", &cfg->threshold);
    sds_json_get_uint_field(r, "sample_rate", &cfg->sample_rate);
}

/* TableA State */
static void serialize_tableA_state(void* section, SdsJsonWriter* w) {
    TableAState* st = (TableAState*)section;
    sds_json_add_float(w, "temperature", st->temperature);
    sds_json_add_float(w, "humidity", st->humidity);
    sds_json_add_uint(w, "reading_count", st->reading_count);
}

static void deserialize_tableA_state(void* section, SdsJsonReader* r) {
    TableAState* st = (TableAState*)section;
    sds_json_get_float_field(r, "temperature", &st->temperature);
    sds_json_get_float_field(r, "humidity", &st->humidity);
    sds_json_get_uint_field(r, "reading_count", &st->reading_count);
}

/* TableA Status */
static void serialize_tableA_status(void* section, SdsJsonWriter* w) {
    TableAStatus* st = (TableAStatus*)section;
    sds_json_add_uint(w, "error_code", st->error_code);
    sds_json_add_uint(w, "battery_level", st->battery_level);
    sds_json_add_uint(w, "uptime_ms", st->uptime_ms);
}

static void deserialize_tableA_status(void* section, SdsJsonReader* r) {
    (void)section;
    (void)r;
    /* Owner would populate status slots - simplified for now */
}

/* TableB Config */
static void serialize_tableB_config(void* table, SdsJsonWriter* w) {
    TableBConfig* cfg = (TableBConfig*)table;
    sds_json_add_uint(w, "target_position", cfg->target_position);
    sds_json_add_uint(w, "speed", cfg->speed);
    sds_json_add_uint(w, "enabled", cfg->enabled);
}

static void deserialize_tableB_config(void* table, SdsJsonReader* r) {
    TableBConfig* cfg = (TableBConfig*)table;
    sds_json_get_uint8_field(r, "target_position", &cfg->target_position);
    sds_json_get_uint8_field(r, "speed", &cfg->speed);
    sds_json_get_uint8_field(r, "enabled", &cfg->enabled);
}

/* TableB State */
static void serialize_tableB_state(void* section, SdsJsonWriter* w) {
    TableBState* st = (TableBState*)section;
    sds_json_add_uint(w, "current_position", st->current_position);
    sds_json_add_uint(w, "is_moving", st->is_moving);
}

static void deserialize_tableB_state(void* section, SdsJsonReader* r) {
    TableBState* st = (TableBState*)section;
    sds_json_get_uint8_field(r, "current_position", &st->current_position);
    sds_json_get_uint8_field(r, "is_moving", &st->is_moving);
}

/* TableB Status */
static void serialize_tableB_status(void* section, SdsJsonWriter* w) {
    TableBStatus* st = (TableBStatus*)section;
    sds_json_add_uint(w, "motor_status", st->motor_status);
    sds_json_add_uint(w, "error_count", st->error_count);
}

static void deserialize_tableB_status(void* section, SdsJsonReader* r) {
    (void)section;
    (void)r;
}

/* ============== Node Role Configuration ============== */

typedef enum {
    NODE_1,
    NODE_2,
    NODE_3,
} NodeType;

/* ============== Global State ============== */

static volatile bool g_running = true;
static NodeType g_node_type;
static const char* g_node_id;

static union {
    TableADeviceTable device;
    TableAOwnerTable owner;
} g_tableA;

static union {
    TableBDeviceTable device;
    TableBOwnerTable owner;
} g_tableB;

static int g_config_updates_received = 0;
static int g_state_updates_received = 0;
static int g_status_updates_received = 0;

/* Expected values */
static uint8_t g_expected_tableA_mode = 0;
static float g_expected_tableA_threshold = 0;
static uint8_t g_expected_tableB_position = 0;
static uint8_t g_expected_tableB_enabled = 0;

/* Status tracking for owner validation */
#define MAX_TRACKED_DEVICES 4
typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    int status_count;
    uint8_t last_battery_level;  /* For TableA status */
    uint32_t last_uptime_ms;
} TrackedDeviceStatus;

static TrackedDeviceStatus g_tableA_device_status[MAX_TRACKED_DEVICES];
static int g_tableA_unique_devices = 0;

static TrackedDeviceStatus g_tableB_device_status[MAX_TRACKED_DEVICES];
static int g_tableB_unique_devices = 0;

/* Helper to find or add a device in the tracking array */
static TrackedDeviceStatus* find_or_add_device(TrackedDeviceStatus* arr, int* count, const char* node_id) {
    /* Search for existing */
    for (int i = 0; i < *count; i++) {
        if (strcmp(arr[i].node_id, node_id) == 0) {
            return &arr[i];
        }
    }
    /* Add new if space available */
    if (*count < MAX_TRACKED_DEVICES) {
        TrackedDeviceStatus* slot = &arr[*count];
        strncpy(slot->node_id, node_id, SDS_MAX_NODE_ID_LEN - 1);
        slot->node_id[SDS_MAX_NODE_ID_LEN - 1] = '\0';
        slot->status_count = 0;
        slot->last_battery_level = 0;
        slot->last_uptime_ms = 0;
        (*count)++;
        return slot;
    }
    return NULL;
}

/* ============== Signal Handler ============== */

static void signal_handler(int sig) {
    (void)sig;
    printf("\n[%s] Shutting down...\n", g_node_id);
    g_running = false;
}

/* ============== Callbacks ============== */

static void on_tableA_config(const char* table_type, void* user_data) {
    (void)table_type;
    (void)user_data;
    g_config_updates_received++;
    
    if (g_node_type == NODE_2 || g_node_type == NODE_3) {
        TableAConfig* cfg = &g_tableA.device.config;
        printf("[%s] TableA config received: mode=%d threshold=%.1f rate=%u\n",
               g_node_id, cfg->mode, cfg->threshold, cfg->sample_rate);
        
        if (cfg->mode == g_expected_tableA_mode && 
            cfg->threshold == g_expected_tableA_threshold) {
            printf("[%s] ✓ TableA config VALID\n", g_node_id);
        }
    }
}

static void on_tableA_state(const char* table_type, const char* from_node, void* user_data) {
    (void)table_type;
    (void)user_data;
    g_state_updates_received++;
    
    if (g_node_type == NODE_1) {
        TableAState* st = &g_tableA.owner.state;
        printf("[%s] TableA state from %s: temp=%.1f humidity=%.1f count=%u\n",
               g_node_id, from_node, st->temperature, st->humidity, st->reading_count);
    }
}

static void on_tableA_status(const char* table_type, const char* from_node, void* user_data) {
    (void)table_type;
    (void)user_data;
    g_status_updates_received++;
    
    if (g_node_type == NODE_1) {
        /* Track this device's status */
        TrackedDeviceStatus* tracked = find_or_add_device(
            g_tableA_device_status, &g_tableA_unique_devices, from_node);
        
        if (tracked) {
            tracked->status_count++;
            
            /* Try to read status from owner's status_slots (Issue 7 validation) */
            /* Once Issue 7 is implemented, this will find the device's status */
            bool found_in_slots = false;
            for (int i = 0; i < MAX_NODES; i++) {
                TableAStatusSlot* slot = &g_tableA.owner.status_slots[i];
                if (slot->valid && strcmp(slot->node_id, from_node) == 0) {
                    found_in_slots = true;
                    tracked->last_battery_level = slot->status.battery_level;
                    tracked->last_uptime_ms = slot->status.uptime_ms;
                    printf("[%s] TableA status from %s: battery=%d%% uptime=%ums (slot %d)\n",
                           g_node_id, from_node, 
                           slot->status.battery_level, slot->status.uptime_ms, i);
                    break;
                }
            }
            
            if (!found_in_slots) {
                printf("[%s] TableA status from %s (callback received, but no slot data yet)\n",
                       g_node_id, from_node);
            }
        } else {
            printf("[%s] TableA status from %s (tracking full)\n", g_node_id, from_node);
        }
    }
}

static void on_tableB_config(const char* table_type, void* user_data) {
    (void)table_type;
    (void)user_data;
    g_config_updates_received++;
    
    if (g_node_type == NODE_1 || g_node_type == NODE_3) {
        TableBConfig* cfg = &g_tableB.device.config;
        printf("[%s] TableB config received: pos=%d speed=%d enabled=%d\n",
               g_node_id, cfg->target_position, cfg->speed, cfg->enabled);
        
        if (cfg->target_position == g_expected_tableB_position && 
            cfg->enabled == g_expected_tableB_enabled) {
            printf("[%s] ✓ TableB config VALID\n", g_node_id);
        }
    }
}

static void on_tableB_state(const char* table_type, const char* from_node, void* user_data) {
    (void)table_type;
    (void)user_data;
    g_state_updates_received++;
    
    if (g_node_type == NODE_2) {
        TableBState* st = &g_tableB.owner.state;
        printf("[%s] TableB state from %s: pos=%d moving=%d\n",
               g_node_id, from_node, st->current_position, st->is_moving);
    }
}

static void on_tableB_status(const char* table_type, const char* from_node, void* user_data) {
    (void)table_type;
    (void)user_data;
    g_status_updates_received++;
    
    if (g_node_type == NODE_2) {
        /* Track this device's status */
        TrackedDeviceStatus* tracked = find_or_add_device(
            g_tableB_device_status, &g_tableB_unique_devices, from_node);
        
        if (tracked) {
            tracked->status_count++;
            
            /* Try to read status from owner's status_slots (Issue 7 validation) */
            bool found_in_slots = false;
            for (int i = 0; i < MAX_NODES; i++) {
                TableBStatusSlot* slot = &g_tableB.owner.status_slots[i];
                if (slot->valid && strcmp(slot->node_id, from_node) == 0) {
                    found_in_slots = true;
                    printf("[%s] TableB status from %s: motor=%d errors=%d (slot %d)\n",
                           g_node_id, from_node, 
                           slot->status.motor_status, slot->status.error_count, i);
                    break;
                }
            }
            
            if (!found_in_slots) {
                printf("[%s] TableB status from %s (callback received, but no slot data yet)\n",
                       g_node_id, from_node);
            }
        } else {
            printf("[%s] TableB status from %s (tracking full)\n", g_node_id, from_node);
        }
    }
}

/* ============== Node Setup ============== */

static SdsError setup_node1(void) {
    SdsError err;
    SdsTableOptions opts = { .sync_interval_ms = 500 };
    
    /* TableA as OWNER */
    memset(&g_tableA.owner, 0, sizeof(g_tableA.owner));
    g_tableA.owner.config.mode = 2;
    g_tableA.owner.config.threshold = 25.5f;
    g_tableA.owner.config.sample_rate = 1000;
    
    err = sds_register_table_ex(
        &g_tableA.owner, "TableA", SDS_ROLE_OWNER, &opts,
        offsetof(TableAOwnerTable, config), sizeof(TableAConfig),
        offsetof(TableAOwnerTable, state), sizeof(TableAState),
        0, 0,  /* Owner doesn't send status */
        serialize_tableA_config, deserialize_tableA_config,
        NULL, deserialize_tableA_state,  /* Owner receives state */
        NULL, deserialize_tableA_status
    );
    if (err != SDS_OK) return err;
    
    /* Configure status slot metadata for per-device tracking (Issue 7) */
    sds_set_owner_status_slots(
        "TableA",
        offsetof(TableAOwnerTable, status_slots),
        sizeof(TableAStatusSlot),
        offsetof(TableAStatusSlot, status),
        offsetof(TableAOwnerTable, status_count),
        MAX_NODES
    );
    
    sds_on_state_update("TableA", on_tableA_state, NULL);
    sds_on_status_update("TableA", on_tableA_status, NULL);
    
    /* TableB as DEVICE */
    memset(&g_tableB.device, 0, sizeof(g_tableB.device));
    g_tableB.device.state.current_position = 0;
    g_tableB.device.state.is_moving = 0;
    g_tableB.device.status.motor_status = 0;
    g_tableB.device.status.error_count = 0;
    
    err = sds_register_table_ex(
        &g_tableB.device, "TableB", SDS_ROLE_DEVICE, &opts,
        offsetof(TableBDeviceTable, config), sizeof(TableBConfig),
        offsetof(TableBDeviceTable, state), sizeof(TableBState),
        offsetof(TableBDeviceTable, status), sizeof(TableBStatus),
        NULL, deserialize_tableB_config,  /* Device receives config */
        serialize_tableB_state, NULL,
        serialize_tableB_status, NULL
    );
    if (err != SDS_OK) return err;
    
    sds_on_config_update("TableB", on_tableB_config, NULL);
    
    g_expected_tableB_position = 75;
    g_expected_tableB_enabled = 1;
    
    printf("[%s] Registered: TableA=OWNER, TableB=DEVICE\n", g_node_id);
    printf("[%s] Publishing TableA config: mode=%d threshold=%.1f\n", 
           g_node_id, g_tableA.owner.config.mode, g_tableA.owner.config.threshold);
    
    return SDS_OK;
}

static SdsError setup_node2(void) {
    SdsError err;
    SdsTableOptions opts = { .sync_interval_ms = 500 };
    
    /* TableA as DEVICE */
    memset(&g_tableA.device, 0, sizeof(g_tableA.device));
    g_tableA.device.state.temperature = 23.5f;
    g_tableA.device.state.humidity = 45.0f;
    g_tableA.device.state.reading_count = 0;
    g_tableA.device.status.error_code = 0;
    g_tableA.device.status.battery_level = 85;
    g_tableA.device.status.uptime_ms = 0;
    
    err = sds_register_table_ex(
        &g_tableA.device, "TableA", SDS_ROLE_DEVICE, &opts,
        offsetof(TableADeviceTable, config), sizeof(TableAConfig),
        offsetof(TableADeviceTable, state), sizeof(TableAState),
        offsetof(TableADeviceTable, status), sizeof(TableAStatus),
        NULL, deserialize_tableA_config,
        serialize_tableA_state, NULL,
        serialize_tableA_status, NULL
    );
    if (err != SDS_OK) return err;
    
    sds_on_config_update("TableA", on_tableA_config, NULL);
    
    g_expected_tableA_mode = 2;
    g_expected_tableA_threshold = 25.5f;
    
    /* TableB as OWNER */
    memset(&g_tableB.owner, 0, sizeof(g_tableB.owner));
    g_tableB.owner.config.target_position = 75;
    g_tableB.owner.config.speed = 50;
    g_tableB.owner.config.enabled = 1;
    
    err = sds_register_table_ex(
        &g_tableB.owner, "TableB", SDS_ROLE_OWNER, &opts,
        offsetof(TableBOwnerTable, config), sizeof(TableBConfig),
        offsetof(TableBOwnerTable, state), sizeof(TableBState),
        0, 0,
        serialize_tableB_config, deserialize_tableB_config,
        NULL, deserialize_tableB_state,
        NULL, deserialize_tableB_status
    );
    if (err != SDS_OK) return err;
    
    /* Configure status slot metadata for per-device tracking (Issue 7) */
    sds_set_owner_status_slots(
        "TableB",
        offsetof(TableBOwnerTable, status_slots),
        sizeof(TableBStatusSlot),
        offsetof(TableBStatusSlot, status),
        offsetof(TableBOwnerTable, status_count),
        MAX_NODES
    );
    
    sds_on_state_update("TableB", on_tableB_state, NULL);
    sds_on_status_update("TableB", on_tableB_status, NULL);
    
    printf("[%s] Registered: TableA=DEVICE, TableB=OWNER\n", g_node_id);
    printf("[%s] Publishing TableB config: pos=%d enabled=%d\n", 
           g_node_id, g_tableB.owner.config.target_position, g_tableB.owner.config.enabled);
    
    return SDS_OK;
}

static SdsError setup_node3(void) {
    SdsError err;
    SdsTableOptions opts = { .sync_interval_ms = 500 };
    
    /* TableA as DEVICE */
    memset(&g_tableA.device, 0, sizeof(g_tableA.device));
    g_tableA.device.state.temperature = 24.8f;
    g_tableA.device.state.humidity = 52.0f;
    g_tableA.device.state.reading_count = 0;
    g_tableA.device.status.error_code = 0;
    g_tableA.device.status.battery_level = 92;
    g_tableA.device.status.uptime_ms = 0;
    
    err = sds_register_table_ex(
        &g_tableA.device, "TableA", SDS_ROLE_DEVICE, &opts,
        offsetof(TableADeviceTable, config), sizeof(TableAConfig),
        offsetof(TableADeviceTable, state), sizeof(TableAState),
        offsetof(TableADeviceTable, status), sizeof(TableAStatus),
        NULL, deserialize_tableA_config,
        serialize_tableA_state, NULL,
        serialize_tableA_status, NULL
    );
    if (err != SDS_OK) return err;
    
    sds_on_config_update("TableA", on_tableA_config, NULL);
    g_expected_tableA_mode = 2;
    g_expected_tableA_threshold = 25.5f;
    
    /* TableB as DEVICE */
    memset(&g_tableB.device, 0, sizeof(g_tableB.device));
    g_tableB.device.state.current_position = 0;
    g_tableB.device.state.is_moving = 0;
    g_tableB.device.status.motor_status = 0;
    g_tableB.device.status.error_count = 0;
    
    err = sds_register_table_ex(
        &g_tableB.device, "TableB", SDS_ROLE_DEVICE, &opts,
        offsetof(TableBDeviceTable, config), sizeof(TableBConfig),
        offsetof(TableBDeviceTable, state), sizeof(TableBState),
        offsetof(TableBDeviceTable, status), sizeof(TableBStatus),
        NULL, deserialize_tableB_config,
        serialize_tableB_state, NULL,
        serialize_tableB_status, NULL
    );
    if (err != SDS_OK) return err;
    
    sds_on_config_update("TableB", on_tableB_config, NULL);
    g_expected_tableB_position = 75;
    g_expected_tableB_enabled = 1;
    
    printf("[%s] Registered: TableA=DEVICE, TableB=DEVICE\n", g_node_id);
    
    return SDS_OK;
}

/* ============== Simulation ============== */

static void simulate_node_behavior(uint32_t elapsed_ms) {
    switch (g_node_type) {
        case NODE_1:
            /* TableB device: update state/status */
            g_tableB.device.state.current_position = (elapsed_ms / 100) % 100;
            g_tableB.device.state.is_moving = (elapsed_ms % 2000) < 1000 ? 1 : 0;
            g_tableB.device.status.motor_status = g_tableB.device.state.is_moving;
            break;
            
        case NODE_2:
            /* TableA device: update state/status */
            g_tableA.device.state.temperature = 23.5f + (float)(elapsed_ms % 1000) / 500.0f;
            g_tableA.device.state.humidity = 45.0f + (float)(elapsed_ms % 500) / 100.0f;
            g_tableA.device.state.reading_count++;
            g_tableA.device.status.uptime_ms = elapsed_ms;
            break;
            
        case NODE_3:
            /* TableA device */
            g_tableA.device.state.temperature = 24.8f + (float)(elapsed_ms % 800) / 400.0f;
            g_tableA.device.state.humidity = 52.0f + (float)(elapsed_ms % 600) / 150.0f;
            g_tableA.device.state.reading_count++;
            g_tableA.device.status.uptime_ms = elapsed_ms;
            g_tableA.device.status.battery_level = 92 - (elapsed_ms / 10000);
            
            /* TableB device */
            g_tableB.device.state.current_position = (elapsed_ms / 200) % 100;
            g_tableB.device.state.is_moving = (elapsed_ms % 3000) < 1500 ? 1 : 0;
            g_tableB.device.status.motor_status = g_tableB.device.state.is_moving;
            break;
    }
}

/* ============== Main ============== */

static void print_usage(const char* prog) {
    printf("Usage: %s <node1|node2|node3> [broker_ip]\n", prog);
    printf("\n");
    printf("  node1: TableA=OWNER, TableB=DEVICE\n");
    printf("  node2: TableA=DEVICE, TableB=OWNER\n");
    printf("  node3: TableA=DEVICE, TableB=DEVICE\n");
}

int main(int argc, char* argv[]) {
    const char* broker = "localhost";
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "node1") == 0) {
        g_node_type = NODE_1;
        g_node_id = "node1";
    } else if (strcmp(argv[1], "node2") == 0) {
        g_node_type = NODE_2;
        g_node_id = "node2";
    } else if (strcmp(argv[1], "node3") == 0) {
        g_node_type = NODE_3;
        g_node_id = "node3";
    } else {
        print_usage(argv[0]);
        return 1;
    }
    
    if (argc > 2) {
        broker = argv[2];
    }
    
    printf("=== SDS Multi-Node Test ===\n");
    printf("Node: %s\n", g_node_id);
    printf("Broker: %s\n", broker);
    printf("Press Ctrl+C to quit\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);  /* Handle graceful termination from test harness */
    
    SdsConfig config = {
        .node_id = g_node_id,
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        printf("[%s] Failed to init: %s\n", g_node_id, sds_error_string(err));
        return 1;
    }
    
    switch (g_node_type) {
        case NODE_1: err = setup_node1(); break;
        case NODE_2: err = setup_node2(); break;
        case NODE_3: err = setup_node3(); break;
    }
    
    if (err != SDS_OK) {
        printf("[%s] Failed to setup: %s\n", g_node_id, sds_error_string(err));
        sds_shutdown();
        return 1;
    }
    
    /* Phase 1: Stabilization - let MQTT subscriptions propagate */
    printf("[%s] Setup complete, stabilizing subscriptions...\n", g_node_id);
    for (int i = 0; i < 20 && g_running; i++) {
        sds_loop();  /* Process any early messages */
        sds_platform_delay_ms(100);
    }
    printf("[%s] Stabilization complete\n", g_node_id);
    
    /* Phase 2: Test phase */
    uint32_t start_time = sds_platform_millis();
    uint32_t last_stats = 0;
    uint32_t test_duration_ms = 10000;  /* Reduced since we added setup/shutdown time */
    
    printf("[%s] Running test phase for %u seconds...\n\n", g_node_id, test_duration_ms / 1000);
    
    while (g_running) {
        uint32_t now = sds_platform_millis();
        uint32_t elapsed = now - start_time;
        
        simulate_node_behavior(elapsed);
        sds_loop();
        
        if (now - last_stats >= 3000) {
            printf("[%s] Stats @ %us: config_rx=%d state_rx=%d status_rx=%d\n",
                   g_node_id, elapsed / 1000,
                   g_config_updates_received,
                   g_state_updates_received,
                   g_status_updates_received);
            last_stats = now;
        }
        
        if (elapsed >= test_duration_ms) {
            printf("[%s] Test duration reached\n", g_node_id);
            break;
        }
        
        sds_platform_delay_ms(50);
    }
    
    /* Final summary */
    printf("\n=== [%s] Final Results ===\n", g_node_id);
    printf("Config updates received: %d\n", g_config_updates_received);
    printf("State updates received: %d\n", g_state_updates_received);
    printf("Status updates received: %d\n", g_status_updates_received);
    
    const SdsStats* stats = sds_get_stats();
    printf("Total messages received: %u\n", stats->messages_received);
    printf("Total messages sent: %u\n", stats->messages_sent);
    printf("Reconnects: %u\n", stats->reconnect_count);
    
    bool test_passed = true;
    
    switch (g_node_type) {
        case NODE_1:
            if (g_config_updates_received == 0) {
                printf("✗ FAIL: No TableB config received from node2\n");
                test_passed = false;
            } else {
                printf("✓ PASS: Received TableB config from node2\n");
            }
            if (g_state_updates_received == 0) {
                printf("✗ FAIL: No TableA state received from devices\n");
                test_passed = false;
            } else {
                printf("✓ PASS: Received TableA state from devices (%d updates)\n", g_state_updates_received);
            }
            
            /* Validate status from devices (node2 and node3 both send TableA status) */
            printf("\n--- TableA Owner Status Validation ---\n");
            printf("Status callbacks received: %d\n", g_status_updates_received);
            printf("Unique devices tracked: %d\n", g_tableA_unique_devices);
            
            if (g_tableA_unique_devices < 2) {
                printf("✗ FAIL: Expected status from 2 devices (node2, node3), got %d\n", 
                       g_tableA_unique_devices);
                test_passed = false;
            } else {
                printf("✓ PASS: Received status from %d unique devices\n", g_tableA_unique_devices);
                for (int i = 0; i < g_tableA_unique_devices; i++) {
                    printf("  Device '%s': %d status updates\n", 
                           g_tableA_device_status[i].node_id,
                           g_tableA_device_status[i].status_count);
                }
            }
            
            /* Validate that status is in owner's status_slots (Issue 7 test) */
            {
                int valid_slots = 0;
                printf("\nOwner status_slots check:\n");
                for (int i = 0; i < MAX_NODES; i++) {
                    TableAStatusSlot* slot = &g_tableA.owner.status_slots[i];
                    if (slot->valid) {
                        valid_slots++;
                        printf("  Slot %d: node='%s' battery=%d%% uptime=%ums\n",
                               i, slot->node_id, 
                               slot->status.battery_level, slot->status.uptime_ms);
                    }
                }
                
                if (valid_slots < 2) {
                    printf("✗ FAIL: Expected 2 valid status slots, got %d (Issue 7 not implemented?)\n", 
                           valid_slots);
                    test_passed = false;
                } else {
                    printf("✓ PASS: Found %d valid status slots with deserialized data\n", valid_slots);
                    
                    /* Validate specific values */
                    bool found_node2 = false, found_node3 = false;
                    for (int i = 0; i < MAX_NODES; i++) {
                        TableAStatusSlot* slot = &g_tableA.owner.status_slots[i];
                        if (slot->valid) {
                            if (strcmp(slot->node_id, "node2") == 0) {
                                found_node2 = true;
                                /* node2 starts with battery_level=85 */
                                if (slot->status.battery_level == 0) {
                                    printf("  ✗ node2 battery_level is 0 (not deserialized?)\n");
                                } else {
                                    printf("  ✓ node2 battery=%d%% (expected ~85%%)\n", 
                                           slot->status.battery_level);
                                }
                            }
                            if (strcmp(slot->node_id, "node3") == 0) {
                                found_node3 = true;
                                /* node3 starts with battery_level=92 */
                                if (slot->status.battery_level == 0) {
                                    printf("  ✗ node3 battery_level is 0 (not deserialized?)\n");
                                } else {
                                    printf("  ✓ node3 battery=%d%% (expected ~92%%)\n", 
                                           slot->status.battery_level);
                                }
                            }
                        }
                    }
                    if (!found_node2 || !found_node3) {
                        printf("✗ FAIL: Missing expected device slots (node2=%s, node3=%s)\n",
                               found_node2 ? "found" : "missing",
                               found_node3 ? "found" : "missing");
                        test_passed = false;
                    }
                }
            }
            break;
            
        case NODE_2:
            if (g_config_updates_received == 0) {
                printf("✗ FAIL: No TableA config received from node1\n");
                test_passed = false;
            } else {
                printf("✓ PASS: Received TableA config from node1\n");
            }
            if (g_state_updates_received == 0) {
                printf("✗ FAIL: No TableB state received from devices\n");
                test_passed = false;
            } else {
                printf("✓ PASS: Received TableB state from devices (%d updates)\n", g_state_updates_received);
            }
            
            /* Validate status from devices (node1 and node3 both send TableB status) */
            printf("\n--- TableB Owner Status Validation ---\n");
            printf("Status callbacks received: %d\n", g_status_updates_received);
            printf("Unique devices tracked: %d\n", g_tableB_unique_devices);
            
            if (g_tableB_unique_devices < 2) {
                printf("✗ FAIL: Expected status from 2 devices (node1, node3), got %d\n", 
                       g_tableB_unique_devices);
                test_passed = false;
            } else {
                printf("✓ PASS: Received status from %d unique devices\n", g_tableB_unique_devices);
            }
            
            /* Validate status slots */
            {
                int valid_slots = 0;
                printf("\nOwner status_slots check:\n");
                for (int i = 0; i < MAX_NODES; i++) {
                    TableBStatusSlot* slot = &g_tableB.owner.status_slots[i];
                    if (slot->valid) {
                        valid_slots++;
                        printf("  Slot %d: node='%s' motor=%d errors=%d\n",
                               i, slot->node_id, 
                               slot->status.motor_status, slot->status.error_count);
                    }
                }
                
                if (valid_slots < 2) {
                    printf("✗ FAIL: Expected 2 valid status slots, got %d (Issue 7 not implemented?)\n", 
                           valid_slots);
                    test_passed = false;
                } else {
                    printf("✓ PASS: Found %d valid status slots with deserialized data\n", valid_slots);
                }
            }
            break;
            
        case NODE_3:
            /* Validate TableA config was applied (may arrive before callback was set) */
            if (g_tableA.device.config.mode == g_expected_tableA_mode &&
                fabsf(g_tableA.device.config.threshold - g_expected_tableA_threshold) < 0.1f) {
                printf("✓ PASS: TableA config applied correctly (mode=%d, threshold=%.1f)\n",
                       g_tableA.device.config.mode, g_tableA.device.config.threshold);
            } else {
                printf("✗ FAIL: TableA config not applied (mode=%d expected %d, threshold=%.1f expected %.1f)\n",
                       g_tableA.device.config.mode, g_expected_tableA_mode,
                       g_tableA.device.config.threshold, g_expected_tableA_threshold);
                test_passed = false;
            }
            /* Validate TableB config was applied */
            if (g_tableB.device.config.target_position == g_expected_tableB_position &&
                g_tableB.device.config.enabled == g_expected_tableB_enabled) {
                printf("✓ PASS: TableB config applied correctly (pos=%d, enabled=%d)\n",
                       g_tableB.device.config.target_position, g_tableB.device.config.enabled);
            } else {
                printf("✗ FAIL: TableB config not applied (pos=%d expected %d, enabled=%d expected %d)\n",
                       g_tableB.device.config.target_position, g_expected_tableB_position,
                       g_tableB.device.config.enabled, g_expected_tableB_enabled);
                test_passed = false;
            }
            printf("(Config callbacks received: %d - may be less due to retained message timing)\n",
                   g_config_updates_received);
            break;
    }
    
    printf("\nOverall: %s\n", test_passed ? "PASSED" : "FAILED");
    
    /* Phase 3: Graceful shutdown - let final messages flush */
    printf("\n[%s] Graceful shutdown, flushing messages...\n", g_node_id);
    for (int i = 0; i < 10 && g_running; i++) {
        sds_loop();
        sds_platform_delay_ms(50);
    }
    
    sds_unregister_table("TableA");
    sds_unregister_table("TableB");
    sds_shutdown();
    
    printf("[%s] Shutdown complete\n", g_node_id);
    
    return test_passed ? 0 : 1;
}
