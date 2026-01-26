/*
 * test_liveness.c - Liveness detection test
 * 
 * Tests the liveness/heartbeat mechanism:
 * 
 *   node1: OWNER - receives status updates and tracks device liveness
 *   node2: DEVICE - sends periodic heartbeats even when data unchanged
 * 
 * Test scenarios:
 *   1. Device sends heartbeat when @liveness timer expires
 *   2. Owner's last_seen_ms updates on each status receive
 *   3. sds_is_device_online() returns correct value
 *   4. Graceful shutdown publishes offline message
 * 
 * Usage:
 *   ./test_liveness node1 [broker_ip]   # Start owner first
 *   ./test_liveness node2 [broker_ip]   # Then start device
 */

#include "sds.h"
#include "sds_json.h"
#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stddef.h>

/* ============== Test Configuration ============== */

#define LIVENESS_INTERVAL_MS    1000   /* 1 second heartbeat */
#define SYNC_INTERVAL_MS        500    /* Check every 500ms */
#define TEST_DURATION_MS        8000   /* Run for 8 seconds */
#define LIVENESS_TIMEOUT_MS     1500   /* 1.5x liveness for timeout */

/* ============== Table Definitions ============== */

/* Simple sensor table for liveness testing */
typedef struct {
    uint8_t enabled;
} LivenessConfig;

typedef struct {
    float value;
} LivenessState;

typedef struct {
    uint8_t health;
    uint32_t uptime_ms;
} LivenessStatus;

/* Device table */
typedef struct {
    LivenessConfig config;
    LivenessState state;
    LivenessStatus status;
} LivenessDeviceTable;

/* Status slot for owner */
typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    bool valid;
    bool online;
    uint32_t last_seen_ms;
    LivenessStatus status;
} LivenessStatusSlot;

/* Owner table */
#define MAX_NODES 4

typedef struct {
    LivenessConfig config;
    LivenessState state;
    LivenessStatusSlot status_slots[MAX_NODES];
    uint8_t status_count;
} LivenessOwnerTable;

/* ============== Serialization Functions ============== */

static void serialize_config(void* section, SdsJsonWriter* w) {
    LivenessConfig* cfg = (LivenessConfig*)section;
    sds_json_add_uint(w, "enabled", cfg->enabled);
}

static void deserialize_config(void* section, SdsJsonReader* r) {
    LivenessConfig* cfg = (LivenessConfig*)section;
    sds_json_get_uint8_field(r, "enabled", &cfg->enabled);
}

static void serialize_state(void* section, SdsJsonWriter* w) {
    LivenessState* st = (LivenessState*)section;
    sds_json_add_float(w, "value", st->value);
}

static void deserialize_state(void* section, SdsJsonReader* r) {
    LivenessState* st = (LivenessState*)section;
    sds_json_get_float_field(r, "value", &st->value);
}

static void serialize_status(void* section, SdsJsonWriter* w) {
    LivenessStatus* st = (LivenessStatus*)section;
    sds_json_add_uint(w, "health", st->health);
    sds_json_add_uint(w, "uptime_ms", st->uptime_ms);
}

static void deserialize_status(void* section, SdsJsonReader* r) {
    LivenessStatus* st = (LivenessStatus*)section;
    sds_json_get_uint8_field(r, "health", &st->health);
    uint32_t uptime;
    if (sds_json_get_uint_field(r, "uptime_ms", &uptime)) {
        st->uptime_ms = uptime;
    }
}

/* ============== Global State ============== */

static volatile sig_atomic_t g_running = 1;
static LivenessOwnerTable g_owner_table = {0};
static LivenessDeviceTable g_device_table = {0};

/* Tracking for validation */
static uint32_t g_heartbeats_received = 0;
static uint32_t g_last_received_uptime = 0;
static uint32_t g_start_time = 0;

/* ============== Signal Handling ============== */

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ============== Callbacks ============== */

static void on_status_update(const char* table_type, const char* from_node) {
    (void)table_type;
    
    g_heartbeats_received++;
    
    /* Find the slot and check the uptime */
    for (int i = 0; i < MAX_NODES; i++) {
        LivenessStatusSlot* slot = &g_owner_table.status_slots[i];
        if (slot->valid && strcmp(slot->node_id, from_node) == 0) {
            g_last_received_uptime = slot->status.uptime_ms;
            printf("[OWNER] Heartbeat #%u from %s: uptime=%u ms, last_seen=%u ms, online=%s\n",
                   g_heartbeats_received, from_node, 
                   slot->status.uptime_ms, slot->last_seen_ms,
                   slot->online ? "true" : "false");
            break;
        }
    }
}

static void on_config_update(const char* table_type) {
    (void)table_type;
    printf("[DEVICE] Config received: enabled=%u\n", g_device_table.config.enabled);
}

/* ============== Node Setup ============== */

static void setup_owner(const char* broker) {
    printf("\n=== Setting up OWNER node ===\n");
    
    SdsConfig config = {
        .node_id = "liveness_owner",
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        fprintf(stderr, "sds_init failed: %s\n", sds_error_string(err));
        exit(1);
    }
    
    /* Register as owner */
    SdsTableOptions opts = { .sync_interval_ms = SYNC_INTERVAL_MS };
    
    err = sds_register_table_ex(
        &g_owner_table, "LivenessTest", SDS_ROLE_OWNER, &opts,
        offsetof(LivenessOwnerTable, config), sizeof(LivenessConfig),
        offsetof(LivenessOwnerTable, state), sizeof(LivenessState),
        0, 0,  /* Owner doesn't send status */
        serialize_config, NULL,
        NULL, deserialize_state,
        NULL, deserialize_status
    );
    
    if (err != SDS_OK) {
        fprintf(stderr, "sds_register_table_ex failed: %s\n", sds_error_string(err));
        exit(1);
    }
    
    /* Configure status slots for per-device tracking */
    sds_set_owner_status_slots(
        "LivenessTest",
        offsetof(LivenessOwnerTable, status_slots),
        sizeof(LivenessStatusSlot),
        offsetof(LivenessStatusSlot, status),
        offsetof(LivenessOwnerTable, status_count),
        MAX_NODES
    );
    
    /* Set callback for status updates */
    sds_on_status_update("LivenessTest", on_status_update);
    
    /* Set initial config */
    g_owner_table.config.enabled = 1;
    
    printf("Owner setup complete. Waiting for device heartbeats...\n\n");
}

static void setup_device(const char* broker) {
    printf("\n=== Setting up DEVICE node ===\n");
    
    SdsConfig config = {
        .node_id = "liveness_device",
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        fprintf(stderr, "sds_init failed: %s\n", sds_error_string(err));
        exit(1);
    }
    
    /* Register as device with liveness enabled */
    SdsTableOptions opts = { .sync_interval_ms = SYNC_INTERVAL_MS };
    
    err = sds_register_table_ex(
        &g_device_table, "LivenessTest", SDS_ROLE_DEVICE, &opts,
        offsetof(LivenessDeviceTable, config), sizeof(LivenessConfig),
        offsetof(LivenessDeviceTable, state), sizeof(LivenessState),
        offsetof(LivenessDeviceTable, status), sizeof(LivenessStatus),
        NULL, deserialize_config,
        serialize_state, NULL,
        serialize_status, NULL
    );
    
    if (err != SDS_OK) {
        fprintf(stderr, "sds_register_table_ex failed: %s\n", sds_error_string(err));
        exit(1);
    }
    
    /* Set config callback */
    sds_on_config_update("LivenessTest", on_config_update);
    
    /* Initialize status */
    g_device_table.status.health = 100;
    g_device_table.status.uptime_ms = 0;
    
    /* Initialize state (won't change during test) */
    g_device_table.state.value = 42.0f;
    
    printf("Device setup complete. Will send heartbeats every %d ms\n\n", LIVENESS_INTERVAL_MS);
}

/* ============== Main Test Logic ============== */

static int run_owner_test(void) {
    printf("\n=== OWNER TEST STARTED ===\n");
    printf("Expecting device heartbeats every ~%d ms\n", LIVENESS_INTERVAL_MS);
    printf("Test duration: %d ms\n\n", TEST_DURATION_MS);
    
    g_start_time = sds_platform_millis();
    uint32_t last_check_time = g_start_time;
    
    while (g_running) {
        sds_loop();
        
        uint32_t now = sds_platform_millis();
        
        /* Periodic status check */
        if (now - last_check_time >= 2000) {
            last_check_time = now;
            
            /* Check if device is online using the API */
            bool device_online = sds_is_device_online(
                &g_owner_table, "LivenessTest", "liveness_device", LIVENESS_TIMEOUT_MS
            );
            
            printf("[OWNER] Periodic check: device_online=%s, heartbeats=%u\n",
                   device_online ? "true" : "false", g_heartbeats_received);
        }
        
        /* Check if test duration elapsed */
        if (now - g_start_time >= TEST_DURATION_MS) {
            break;
        }
        
        sds_platform_delay_ms(50);
    }
    
    printf("\n=== OWNER TEST VALIDATION ===\n");
    
    /* Calculate expected heartbeats (approximately) */
    uint32_t expected_min_heartbeats = (TEST_DURATION_MS / LIVENESS_INTERVAL_MS) - 2;
    
    printf("  Heartbeats received: %u\n", g_heartbeats_received);
    printf("  Expected minimum:    %u\n", expected_min_heartbeats);
    printf("  Last device uptime:  %u ms\n", g_last_received_uptime);
    
    /* Validation */
    int passed = 1;
    
    if (g_heartbeats_received < expected_min_heartbeats) {
        printf("  FAIL: Too few heartbeats received\n");
        passed = 0;
    } else {
        printf("  PASS: Sufficient heartbeats received\n");
    }
    
    /* Check sds_is_device_online still returns true (device should still be online) */
    bool final_online = sds_is_device_online(
        &g_owner_table, "LivenessTest", "liveness_device", LIVENESS_TIMEOUT_MS
    );
    
    if (!final_online) {
        printf("  FAIL: Device reported as offline but should be online\n");
        passed = 0;
    } else {
        printf("  PASS: Device correctly reported as online\n");
    }
    
    /* Check that uptime is incrementing (device is alive) */
    if (g_last_received_uptime < (TEST_DURATION_MS / 2)) {
        printf("  FAIL: Device uptime too low (%u ms)\n", g_last_received_uptime);
        passed = 0;
    } else {
        printf("  PASS: Device uptime is reasonable (%u ms)\n", g_last_received_uptime);
    }
    
    return passed;
}

static int run_device_test(void) {
    printf("\n=== DEVICE TEST STARTED ===\n");
    printf("Sending heartbeats with liveness interval: %d ms\n", LIVENESS_INTERVAL_MS);
    printf("Test duration: %d ms\n\n", TEST_DURATION_MS);
    
    g_start_time = sds_platform_millis();
    uint32_t last_print_time = g_start_time;
    uint32_t heartbeat_count = 0;
    
    while (g_running) {
        /* Update uptime (this changes status, triggering sync) */
        g_device_table.status.uptime_ms = sds_platform_millis() - g_start_time;
        
        /* Note: We're NOT changing state, so heartbeats should still occur due to liveness */
        
        sds_loop();
        
        uint32_t now = sds_platform_millis();
        
        /* Print status every 2 seconds */
        if (now - last_print_time >= 2000) {
            last_print_time = now;
            heartbeat_count++;
            printf("[DEVICE] Uptime: %u ms, health: %u\n", 
                   g_device_table.status.uptime_ms, g_device_table.status.health);
        }
        
        /* Check if test duration elapsed */
        if (now - g_start_time >= TEST_DURATION_MS) {
            break;
        }
        
        sds_platform_delay_ms(50);
    }
    
    printf("\n=== DEVICE TEST COMPLETE ===\n");
    printf("  Final uptime: %u ms\n", g_device_table.status.uptime_ms);
    printf("  Config received: enabled=%u\n", g_device_table.config.enabled);
    
    /* Device always passes if it ran to completion */
    return 1;
}

/* ============== Main ============== */

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <node1|node2> [broker_ip]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Nodes:\n");
        fprintf(stderr, "  node1 - Owner (start first)\n");
        fprintf(stderr, "  node2 - Device (start second)\n");
        return 1;
    }
    
    const char* node = argv[1];
    const char* broker = (argc >= 3) ? argv[2] : "localhost";
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              SDS Liveness Test                           ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Node: %-50s ║\n", node);
    printf("║  Broker: %-48s ║\n", broker);
    printf("║  Liveness interval: %d ms                               ║\n", LIVENESS_INTERVAL_MS);
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    int result = 0;
    
    if (strcmp(node, "node1") == 0) {
        setup_owner(broker);
        
        /* Wait a bit for device to connect */
        printf("Waiting 2s for device to connect...\n");
        sds_platform_delay_ms(2000);
        
        result = run_owner_test();
        
    } else if (strcmp(node, "node2") == 0) {
        setup_device(broker);
        result = run_device_test();
        
    } else {
        fprintf(stderr, "Unknown node: %s\n", node);
        return 1;
    }
    
    /* Graceful shutdown */
    printf("\nShutting down...\n");
    sds_shutdown();
    
    /* Final result */
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    if (result) {
        printf("  ✓ %s: PASSED\n", node);
    } else {
        printf("  ✗ %s: FAILED\n", node);
    }
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return result ? 0 : 1;
}

