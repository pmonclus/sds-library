/*
 * test_simple_api.c - Test the simplified sds_register_table() API
 * 
 * This test demonstrates that users can use the simple form:
 *   sds_register_table(&table, "SensorNode", SDS_ROLE_DEVICE, NULL);
 * 
 * instead of the verbose sds_register_table_ex() or the generated helpers.
 * 
 * Usage:
 *   ./test_simple_api [broker]
 */

#include "sds.h"
#include "sds_types.h"  /* Generated header with types and registry */
#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* ============== Global State ============== */

static volatile bool g_running = true;
static const char* g_broker = "localhost";

/* ============== Signal Handler ============== */

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

/* ============== Test: Simple Registration ============== */

static bool test_simple_registration(void) {
    printf("\n=== Test: Simple Table Registration ===\n");
    
    bool passed = true;
    
    /* Initialize SDS */
    SdsConfig config = {
        .node_id = "test_simple",
        .mqtt_broker = g_broker,
        .mqtt_port = 1883
    };
    
    /* Note: sds_types.h auto-registers via constructor attribute */
    
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        printf("FAIL: sds_init failed: %s\n", sds_error_string(err));
        return false;
    }
    
    /* Wait for connection */
    for (int i = 0; i < 20 && !sds_is_ready(); i++) {
        sds_loop();
        sds_platform_delay_ms(100);
    }
    
    if (!sds_is_ready()) {
        printf("SKIP: MQTT not connected (broker may not be running)\n");
        sds_shutdown();
        return true;  /* Not a failure, just skip */
    }
    
    /* Test 1: Register SensorNode as DEVICE using simple API */
    printf("Test 1: Register SensorNode (DEVICE)...\n");
    
    SensorNodeTable sensor_device = {0};
    sensor_device.state.temperature = 22.5f;
    sensor_device.state.humidity = 55.0f;
    sensor_device.status.battery_percent = 80;
    
    err = sds_register_table(&sensor_device, "SensorNode", SDS_ROLE_DEVICE, NULL);
    if (err != SDS_OK) {
        printf("  FAIL: Registration failed: %s\n", sds_error_string(err));
        passed = false;
    } else {
        printf("  PASS: Registered successfully\n");
    }
    
    /* Test 2: Try to register same table again (should fail) */
    printf("Test 2: Double registration (should fail)...\n");
    
    SensorNodeTable sensor_device2 = {0};
    err = sds_register_table(&sensor_device2, "SensorNode", SDS_ROLE_DEVICE, NULL);
    if (err == SDS_ERR_TABLE_ALREADY_REGISTERED) {
        printf("  PASS: Correctly rejected double registration\n");
    } else {
        printf("  FAIL: Expected TABLE_ALREADY_REGISTERED, got %s\n", sds_error_string(err));
        passed = false;
    }
    
    /* Test 3: Register ActuatorNode as OWNER using simple API */
    printf("Test 3: Register ActuatorNode (OWNER)...\n");
    
    ActuatorNodeOwnerTable actuator_owner = {0};
    actuator_owner.config.target_position = 50;
    actuator_owner.config.speed = 25;
    
    err = sds_register_table(&actuator_owner, "ActuatorNode", SDS_ROLE_OWNER, NULL);
    if (err != SDS_OK) {
        printf("  FAIL: Registration failed: %s\n", sds_error_string(err));
        passed = false;
    } else {
        printf("  PASS: Registered successfully\n");
    }
    
    /* Test 4: Try to register unknown table type (should fail) */
    printf("Test 4: Register unknown table type (should fail)...\n");
    
    char dummy[64] = {0};
    err = sds_register_table(&dummy, "UnknownTable", SDS_ROLE_DEVICE, NULL);
    if (err == SDS_ERR_TABLE_NOT_FOUND) {
        printf("  PASS: Correctly rejected unknown table type\n");
    } else {
        printf("  FAIL: Expected TABLE_NOT_FOUND, got %s\n", sds_error_string(err));
        passed = false;
    }
    
    /* Test 5: Verify table count */
    printf("Test 5: Verify table count...\n");
    
    uint8_t count = sds_get_table_count();
    if (count == 2) {
        printf("  PASS: Table count is 2\n");
    } else {
        printf("  FAIL: Expected 2 tables, got %d\n", count);
        passed = false;
    }
    
    /* Test 6: Run a few loop iterations */
    printf("Test 6: Run sync loop...\n");
    
    for (int i = 0; i < 30; i++) {
        sds_loop();
        sds_platform_delay_ms(50);
        
        if (!g_running) break;
    }
    
    const SdsStats* stats = sds_get_stats();
    if (stats->messages_sent > 0) {
        printf("  PASS: Messages sent=%u received=%u\n", 
               stats->messages_sent, stats->messages_received);
    } else {
        printf("  INFO: No messages sent (may be expected)\n");
    }
    
    /* Cleanup */
    sds_unregister_table("SensorNode");
    sds_unregister_table("ActuatorNode");
    sds_shutdown();
    
    return passed;
}

/* ============== Main ============== */

int main(int argc, char* argv[]) {
    if (argc > 1) {
        g_broker = argv[1];
    }
    
    printf("SDS Simple API Test\n");
    printf("===================\n");
    printf("Broker: %s\n", g_broker);
    
    signal(SIGINT, signal_handler);
    
    int passed = 0;
    int total = 1;
    
    if (test_simple_registration()) {
        passed++;
    }
    
    printf("\n=== Results: %d/%d tests passed ===\n", passed, total);
    
    return (passed == total) ? 0 : 1;
}

