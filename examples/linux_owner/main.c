/*
 * linux_owner example - SDS Owner Node running on Linux/macOS
 * 
 * Demonstrates:
 *   - Using the simple sds_register_table() API
 *   - Registering as OWNER for a table
 *   - Sending config to devices
 *   - Receiving state and status from devices
 * 
 * Usage:
 *   ./example_device [broker_ip]
 */

#include "sds.h"
#include "sds_types.h"  /* Generated header - includes table types & registry */
#include "sds_platform.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>

/* ============== Global State ============== */

static volatile bool g_running = true;
static SensorNodeOwnerTable g_sensor_table;

/* ============== Signal Handler ============== */

static void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down...\n");
    g_running = false;
}

/* ============== Callbacks ============== */

static void on_state_update(const char* table_type, const char* from_node) {
    printf("[State] %s from %s: temp=%.1f humidity=%.1f\n",
           table_type, from_node, 
           g_sensor_table.state.temperature, g_sensor_table.state.humidity);
}

static void on_status_update(const char* table_type, const char* from_node) {
    printf("[Status] %s from %s\n", table_type, from_node);
    /* Status slots would be in g_sensor_table.status_slots[] */
}

/* ============== Main ============== */

int main(int argc, char* argv[]) {
    const char* broker = "localhost";
    if (argc > 1) {
        broker = argv[1];
    }
    
    printf("SDS Owner Example (Simple API)\n");
    printf("==============================\n");
    printf("Broker: %s\n", broker);
    printf("Press Ctrl+C to quit\n\n");
    
    signal(SIGINT, signal_handler);
    
    /* Note: sds_types.h auto-registers table types via constructor */
    
    /* Initialize SDS */
    SdsConfig config = {
        .node_id = "owner_node",
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        printf("Failed to initialize SDS: %s\n", sds_error_string(err));
        return 1;
    }
    
    /* Initialize table */
    memset(&g_sensor_table, 0, sizeof(g_sensor_table));
    g_sensor_table.config.command = 1;
    g_sensor_table.config.threshold = 25.0f;
    
    /* 
     * Register as owner using the SIMPLE API!
     * Just: table pointer, type name, role, options
     * No need for explicit callbacks or offsets.
     */
    err = sds_register_table(&g_sensor_table, "SensorNode", SDS_ROLE_OWNER, NULL);
    if (err != SDS_OK) {
        printf("Failed to register table: %s\n", sds_error_string(err));
        sds_shutdown();
        return 1;
    }
    
    /* Set up callbacks (these are still registered separately) */
    sds_on_state_update("SensorNode", on_state_update);
    sds_on_status_update("SensorNode", on_status_update);
    
    printf("Registered as OWNER for SensorNode\n");
    printf("Config: command=%d threshold=%.1f\n\n", 
           g_sensor_table.config.command, g_sensor_table.config.threshold);
    
    /* Main loop */
    uint32_t last_print = 0;
    while (g_running) {
        sds_loop();
        
        uint32_t now = sds_platform_millis();
        if (now - last_print >= 5000) {
            const SdsStats* stats = sds_get_stats();
            printf("[Stats] msgs_recv=%u msgs_sent=%u reconnects=%u\n",
                   stats->messages_received, stats->messages_sent, 
                   stats->reconnect_count);
            last_print = now;
        }
        
        sds_platform_delay_ms(100);
    }
    
    /* Cleanup */
    sds_unregister_table("SensorNode");
    sds_shutdown();
    
    printf("Done.\n");
    return 0;
}
