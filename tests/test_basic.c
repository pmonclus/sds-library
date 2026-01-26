/*
 * test_basic.c - Basic SDS connectivity test
 * 
 * Tests:
 *   1. Platform initialization
 *   2. MQTT connection to broker
 *   3. Table registration
 *   4. Basic message flow
 * 
 * Usage:
 *   ./test_sds_basic [broker_ip]
 *   
 *   Default broker: localhost
 */

#include "sds.h"
#include "sds_types.h"  /* Generated types and registry */
#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* ============== Test State ============== */

static volatile bool g_running = true;
static int g_test_pass = 0;
static int g_test_fail = 0;

/* ============== Test Macros ============== */

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  ✓ PASS: %s\n", msg); \
        g_test_pass++; \
    } else { \
        printf("  ✗ FAIL: %s\n", msg); \
        g_test_fail++; \
    } \
} while(0)

/* ============== Signal Handler ============== */

static void signal_handler(int sig) {
    (void)sig;
    printf("\nInterrupted, shutting down...\n");
    g_running = false;
}

/* ============== Callbacks ============== */

static void on_config_update(const char* table_type) {
    printf("  [Callback] Config update for: %s\n", table_type);
}

static void on_state_update(const char* table_type, const char* from_node) {
    printf("  [Callback] State update for %s from: %s\n", table_type, from_node);
}

static void on_status_update(const char* table_type, const char* from_node) {
    printf("  [Callback] Status update for %s from: %s\n", table_type, from_node);
}

/* ============== Tests ============== */

static void test_init(const char* broker) {
    printf("\n=== Test: Initialization ===\n");
    
    SdsConfig config = {
        .node_id = "test_node_1",
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    TEST_ASSERT(err == SDS_OK, "sds_init() succeeds");
    TEST_ASSERT(sds_is_ready(), "sds_is_ready() returns true");
    TEST_ASSERT(strcmp(sds_get_node_id(), "test_node_1") == 0, "Node ID matches");
    
    /* Test double init */
    err = sds_init(&config);
    TEST_ASSERT(err == SDS_ERR_ALREADY_INITIALIZED, "Double init returns error");
}

static SensorDataTable g_device_table;

static void test_table_registration_device(void) {
    printf("\n=== Test: Device Table Registration ===\n");
    
    memset(&g_device_table, 0, sizeof(g_device_table));
    g_device_table.state.temperature = 22.5f;
    g_device_table.state.humidity = 55.0f;
    g_device_table.status.battery_percent = 80;
    
    SdsTableOptions opts = { .sync_interval_ms = 500 };
    
    SdsError err = sds_register_table(&g_device_table, "SensorData", SDS_ROLE_DEVICE, &opts);
    TEST_ASSERT(err == SDS_OK, "Register device table");
    TEST_ASSERT(sds_get_table_count() == 1, "Table count is 1");
    
    /* Set up callbacks */
    sds_on_config_update("SensorData", on_config_update);
    
    /* Test duplicate registration */
    SensorDataTable dup_table = {0};
    err = sds_register_table(&dup_table, "SensorData", SDS_ROLE_DEVICE, NULL);
    TEST_ASSERT(err == SDS_ERR_TABLE_ALREADY_REGISTERED, "Duplicate registration fails");
}

static ActuatorDataOwnerTable g_owner_table;

static void test_table_registration_owner(void) {
    printf("\n=== Test: Owner Table Registration ===\n");
    
    memset(&g_owner_table, 0, sizeof(g_owner_table));
    g_owner_table.config.target_position = 50;
    g_owner_table.config.speed = 25;
    
    /* Register as owner for ActuatorData */
    SdsError err = sds_register_table(&g_owner_table, "ActuatorData", SDS_ROLE_OWNER, NULL);
    TEST_ASSERT(err == SDS_OK, "Register owner table");
    TEST_ASSERT(sds_get_table_count() == 2, "Table count is 2");
    
    /* Set up callbacks */
    sds_on_state_update("ActuatorData", on_state_update);
    sds_on_status_update("ActuatorData", on_status_update);
}

static void test_loop(int iterations) {
    printf("\n=== Test: Main Loop (%d iterations) ===\n", iterations);
    
    for (int i = 0; i < iterations && g_running; i++) {
        sds_loop();
        sds_platform_delay_ms(100);
        
        if ((i + 1) % 10 == 0) {
            const SdsStats* stats = sds_get_stats();
            printf("  [%d] msgs_recv=%u msgs_sent=%u\n", 
                   i + 1, stats->messages_received, stats->messages_sent);
        }
    }
    
    TEST_ASSERT(sds_is_ready(), "Still connected after loop");
}

static void test_unregister(void) {
    printf("\n=== Test: Table Unregistration ===\n");
    
    SdsError err = sds_unregister_table("SensorData");
    TEST_ASSERT(err == SDS_OK, "Unregister SensorData");
    TEST_ASSERT(sds_get_table_count() == 1, "Table count is 1");
    
    err = sds_unregister_table("ActuatorData");
    TEST_ASSERT(err == SDS_OK, "Unregister ActuatorData");
    TEST_ASSERT(sds_get_table_count() == 0, "Table count is 0");
    
    err = sds_unregister_table("NonExistent");
    TEST_ASSERT(err == SDS_ERR_TABLE_NOT_FOUND, "Unregister nonexistent fails");
}

static void test_shutdown(void) {
    printf("\n=== Test: Shutdown ===\n");
    
    sds_shutdown();
    TEST_ASSERT(!sds_is_ready(), "sds_is_ready() returns false after shutdown");
}

/* ============== Main ============== */

int main(int argc, char* argv[]) {
    const char* broker = "localhost";
    
    if (argc > 1) {
        broker = argv[1];
    }
    
    printf("SDS Basic Test\n");
    printf("==============\n");
    printf("Broker: %s\n", broker);
    
    signal(SIGINT, signal_handler);
    
    /* Run tests */
    test_init(broker);
    
    if (sds_is_ready()) {
        test_table_registration_device();
        test_table_registration_owner();
        test_loop(30);  /* Run for 3 seconds */
        test_unregister();
    }
    
    test_shutdown();
    
    /* Summary */
    printf("\n==============\n");
    printf("Results: %d passed, %d failed\n", g_test_pass, g_test_fail);
    
    return g_test_fail > 0 ? 1 : 0;
}
