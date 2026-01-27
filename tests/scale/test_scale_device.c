/*
 * test_scale_device.c - Device process for scale testing
 * 
 * Registers a table as DEVICE and sends periodic state/status updates.
 * 
 * Usage:
 *   ./test_scale_device <device_id> [broker_ip] [duration_seconds]
 * 
 * Example:
 *   ./test_scale_device device_01 localhost 30
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

/* Device table */
typedef struct {
    ScaleConfig config;
    ScaleState state;
    ScaleStatus status;
} ScaleDeviceTable;

static ScaleDeviceTable g_table;

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
static const char* g_device_id = NULL;

/* Stats */
static int g_config_updates = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ============== Callbacks ============== */

static void on_config_change(const char* table_name) {
    g_config_updates++;
    (void)table_name;
}

static void on_error(SdsError err, const char* msg) {
    fprintf(stderr, "[%s] Error %d: %s\n", g_device_id, err, msg ? msg : "unknown");
}

/* ============== Simulation ============== */

static float simulate_sensor_value(void) {
    /* Generate a value that varies with time */
    uint32_t elapsed = sds_platform_millis() - g_start_time;
    float base = 20.0f + (float)(elapsed % 10000) / 500.0f;  /* 20-40 range */
    float noise = (float)(rand() % 100) / 100.0f - 0.5f;     /* +/- 0.5 */
    return base + noise;
}

static uint8_t simulate_battery(void) {
    /* Slowly decreasing battery */
    uint32_t elapsed = (sds_platform_millis() - g_start_time) / 1000;
    int battery = 100 - (int)(elapsed / 10);  /* Lose 1% every 10 seconds */
    return battery > 0 ? (uint8_t)battery : 0;
}

/* ============== Main ============== */

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <device_id> [broker_ip] [duration_seconds]\n", argv[0]);
        return 1;
    }
    
    g_device_id = argv[1];
    const char* broker = argc > 2 ? argv[2] : "localhost";
    int duration = argc > 3 ? atoi(argv[3]) : 30;
    
    /* Seed random with device-specific value */
    srand((unsigned int)time(NULL) ^ (unsigned int)(size_t)g_device_id);
    
    printf("[%s] Starting device (broker: %s, duration: %ds)\n",
           g_device_id, broker, duration);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize SDS */
    SdsConfig config = {
        .node_id = g_device_id,
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    if (err == SDS_OK) {
        sds_on_error(on_error);
        sds_on_config_update("ScaleTest", on_config_change);
    }
    if (err != SDS_OK) {
        printf("[%s] Failed to initialize SDS: %d\n", g_device_id, err);
        return 1;
    }
    
    /* Initialize table */
    memset(&g_table, 0, sizeof(g_table));
    g_table.status.battery = 100;
    
    /* Register as DEVICE */
    sds_register_table_ex(
        &g_table, "ScaleTest", SDS_ROLE_DEVICE, NULL,
        offsetof(ScaleDeviceTable, config), sizeof(ScaleConfig),
        offsetof(ScaleDeviceTable, state), sizeof(ScaleState),
        offsetof(ScaleDeviceTable, status), sizeof(ScaleStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    
    /* Wait for connection */
    for (int i = 0; i < 50 && !sds_is_ready(); i++) {
        sds_loop();
        sds_platform_delay_ms(100);
    }
    
    if (!sds_is_ready()) {
        printf("[%s] Failed to connect to MQTT broker\n", g_device_id);
        sds_shutdown();
        return 1;
    }
    
    printf("[%s] Connected!\n", g_device_id);
    g_start_time = sds_platform_millis();
    
    /* Main loop */
    uint32_t end_time = g_start_time + (duration * 1000);
    uint32_t last_update = 0;
    
    while (g_running && sds_platform_millis() < end_time) {
        sds_loop();
        
        uint32_t now = sds_platform_millis();
        uint32_t interval = g_table.config.interval_ms > 0 ? g_table.config.interval_ms : 1000;
        
        /* Update state/status periodically */
        if (now - last_update >= interval) {
            /* Update state */
            g_table.state.value = simulate_sensor_value();
            g_table.state.counter++;
            
            /* Update status */
            g_table.status.battery = simulate_battery();
            g_table.status.uptime_ms = now - g_start_time;
            g_table.status.error_code = 0;
            
            last_update = now;
        }
        
        sds_platform_delay_ms(10);
    }
    
    const SdsStats* stats = sds_get_stats();
    printf("[%s] Done. Sent: %u, Recv: %u, Config updates: %d\n",
           g_device_id, stats->messages_sent, stats->messages_received, g_config_updates);
    
    sds_shutdown();
    return 0;
}
