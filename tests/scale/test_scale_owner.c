/*
 * test_scale_owner.c - Owner process for scale testing
 * 
 * Registers a table as OWNER and tracks status from multiple devices.
 * 
 * Usage:
 *   ./test_scale_owner [broker_ip] [duration_seconds]
 * 
 * Default: localhost, 30 seconds
 */

#include "sds.h"
#include "sds_json.h"
#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stddef.h>
#include <time.h>

/* ============== Table Definition ============== */

typedef struct {
    uint8_t mode;
    float threshold;
    uint32_t interval_ms;
} ScaleConfig;

typedef struct {
    float value;
    uint32_t counter;
} ScaleState;

typedef struct {
    uint8_t error_code;
    uint8_t battery;
    uint32_t uptime_ms;
} ScaleStatus;

/* Status slot for tracking each device */
#define MAX_DEVICES 64

typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    bool valid;
    uint32_t last_seen_ms;
    ScaleStatus status;
} ScaleStatusSlot;

/* Owner table */
typedef struct {
    ScaleConfig config;
    ScaleState state;
    ScaleStatusSlot status_slots[MAX_DEVICES];
    uint8_t status_count;
} ScaleOwnerTable;

static ScaleOwnerTable g_table;

/* ============== Serialization ============== */

static void serialize_config(void* section, SdsJsonWriter* w) {
    ScaleConfig* cfg = (ScaleConfig*)section;
    sds_json_add_uint(w, "mode", cfg->mode);
    sds_json_add_float(w, "threshold", cfg->threshold);
    sds_json_add_uint(w, "interval_ms", cfg->interval_ms);
}

static void deserialize_config(void* section, SdsJsonReader* r) {
    ScaleConfig* cfg = (ScaleConfig*)section;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
    sds_json_get_float_field(r, "threshold", &cfg->threshold);
    sds_json_get_uint_field(r, "interval_ms", &cfg->interval_ms);
}

static void serialize_state(void* section, SdsJsonWriter* w) {
    ScaleState* st = (ScaleState*)section;
    sds_json_add_float(w, "value", st->value);
    sds_json_add_uint(w, "counter", st->counter);
}

static void deserialize_state(void* section, SdsJsonReader* r) {
    ScaleState* st = (ScaleState*)section;
    sds_json_get_float_field(r, "value", &st->value);
    sds_json_get_uint_field(r, "counter", &st->counter);
}

static void serialize_status(void* section, SdsJsonWriter* w) {
    ScaleStatus* st = (ScaleStatus*)section;
    sds_json_add_uint(w, "error_code", st->error_code);
    sds_json_add_uint(w, "battery", st->battery);
    sds_json_add_uint(w, "uptime_ms", st->uptime_ms);
}

static void deserialize_status(void* section, SdsJsonReader* r) {
    ScaleStatus* st = (ScaleStatus*)section;
    sds_json_get_uint8_field(r, "error_code", &st->error_code);
    sds_json_get_uint8_field(r, "battery", &st->battery);
    sds_json_get_uint_field(r, "uptime_ms", &st->uptime_ms);
}

/* ============== Globals ============== */

static volatile int g_running = 1;
static uint32_t g_start_time = 0;

/* Stats */
static int g_messages_received = 0;
static int g_unique_devices = 0;
static int g_max_devices_seen = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ============== Callbacks ============== */

static void on_state_change(const char* table_name, const char* node_id, void* user_data) {
    g_messages_received++;
    (void)table_name;
    (void)node_id;
    (void)user_data;
}

static void on_status_change(const char* table_name, const char* node_id, void* user_data) {
    g_messages_received++;
    (void)table_name;
    (void)node_id;
    (void)user_data;
}

static void on_error(SdsError err, const char* msg) {
    printf("[OWNER] Error %d: %s\n", err, msg ? msg : "unknown");
}

/* ============== Display ============== */

static void print_status(void) {
    uint32_t now = sds_platform_millis();
    uint32_t elapsed = (now - g_start_time) / 1000;
    
    /* Count active devices */
    int active = 0;
    for (int i = 0; i < g_table.status_count && i < MAX_DEVICES; i++) {
        if (g_table.status_slots[i].valid) {
            active++;
        }
    }
    
    if (active > g_max_devices_seen) {
        g_max_devices_seen = active;
    }
    g_unique_devices = g_table.status_count;
    
    const SdsStats* stats = sds_get_stats();
    
    printf("\r[%3us] Devices: %2d active, %2d total | "
           "Msgs: sent=%u recv=%u | Errors: %u    ",
           elapsed, active, g_unique_devices,
           stats->messages_sent, stats->messages_received,
           stats->errors);
    fflush(stdout);
}

/* ============== Main ============== */

int main(int argc, char** argv) {
    const char* broker = argc > 1 ? argv[1] : "localhost";
    int duration = argc > 2 ? atoi(argv[2]) : 30;
    
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           SDS Scale Test - OWNER                             ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Broker: %-51s ║\n", broker);
    printf("║  Duration: %d seconds                                        ║\n", duration);
    printf("║  Max devices: %d                                             ║\n", MAX_DEVICES);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize SDS */
    SdsConfig config = {
        .node_id = "scale_owner",
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        printf("Failed to initialize SDS: %d\n", err);
        return 1;
    }
    
    /* Set error callback */
    sds_on_error(on_error);
    
    /* Initialize table with default config */
    memset(&g_table, 0, sizeof(g_table));
    g_table.config.mode = 1;
    g_table.config.threshold = 50.0f;
    g_table.config.interval_ms = 1000;
    
    /* Register as OWNER */
    sds_register_table_ex(
        &g_table, "ScaleTest", SDS_ROLE_OWNER, NULL,
        offsetof(ScaleOwnerTable, config), sizeof(ScaleConfig),
        offsetof(ScaleOwnerTable, state), sizeof(ScaleState),
        offsetof(ScaleOwnerTable, status_slots[0].status), sizeof(ScaleStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    
    /* Configure status slot tracking for owner */
    sds_set_owner_status_slots(
        "ScaleTest",
        offsetof(ScaleOwnerTable, status_slots),
        sizeof(ScaleStatusSlot),
        offsetof(ScaleStatusSlot, status),
        offsetof(ScaleOwnerTable, status_count),
        MAX_DEVICES
    );
    
    /* Set callbacks for state/status updates */
    sds_on_state_update("ScaleTest", on_state_change, NULL);
    sds_on_status_update("ScaleTest", on_status_change, NULL);
    
    /* Wait for connection */
    printf("Connecting to broker...\n");
    for (int i = 0; i < 50 && !sds_is_ready(); i++) {
        sds_loop();
        sds_platform_delay_ms(100);
    }
    
    if (!sds_is_ready()) {
        printf("Failed to connect to MQTT broker\n");
        sds_shutdown();
        return 1;
    }
    
    printf("Connected! Waiting for devices...\n\n");
    g_start_time = sds_platform_millis();
    
    /* Main loop */
    uint32_t end_time = g_start_time + (duration * 1000);
    uint32_t last_print = 0;
    
    while (g_running && sds_platform_millis() < end_time) {
        sds_loop();
        
        /* Update display every 500ms */
        uint32_t now = sds_platform_millis();
        if (now - last_print >= 500) {
            print_status();
            last_print = now;
        }
        
        sds_platform_delay_ms(10);
    }
    
    /* Final summary */
    printf("\n\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  SCALE TEST COMPLETE\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  Total unique devices seen: %d\n", g_unique_devices);
    printf("  Max concurrent devices: %d\n", g_max_devices_seen);
    printf("  Messages received: %d\n", g_messages_received);
    
    const SdsStats* final_stats = sds_get_stats();
    printf("  Total MQTT messages sent: %u\n", final_stats->messages_sent);
    printf("  Total MQTT messages received: %u\n", final_stats->messages_received);
    printf("  Total errors: %u\n", final_stats->errors);
    printf("══════════════════════════════════════════════════════════════\n");
    
    /* List active devices */
    printf("\nActive devices:\n");
    for (int i = 0; i < g_table.status_count && i < MAX_DEVICES; i++) {
        ScaleStatusSlot* slot = &g_table.status_slots[i];
        if (slot->valid) {
            printf("  - %s (battery: %d%%, uptime: %ums)\n",
                   slot->node_id, slot->status.battery, slot->status.uptime_ms);
        }
    }
    
    sds_shutdown();
    return 0;
}
