/*
 * test_generated.c - Test using generated sds_types.h
 * 
 * This test demonstrates how clean the application code becomes
 * when using the generated types and registration helpers.
 * 
 * Usage:
 *   ./test_generated node1 [broker]   # SensorNode=OWNER, ActuatorNode=DEVICE
 *   ./test_generated node2 [broker]   # SensorNode=DEVICE, ActuatorNode=OWNER
 *   ./test_generated node3 [broker]   # SensorNode=DEVICE, ActuatorNode=DEVICE
 */

#include "sds.h"
#include "sds_types.h"  /* Generated header - brings in all the table types */
#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* ============== Global State ============== */

static volatile bool g_running = true;
static const char* g_node_id;
static int g_node_num;

/* Tables - we use unions since each node has different roles */
static union {
    SensorNodeTable device;
    SensorNodeOwnerTable owner;
} g_sensor;

static union {
    ActuatorNodeTable device;
    ActuatorNodeOwnerTable owner;
} g_actuator;

/* Stats */
static int g_config_rx = 0;
static int g_state_rx = 0;
static int g_status_rx = 0;

/* ============== Signal Handler ============== */

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

/* ============== Callbacks ============== */

static void on_sensor_config(const char* table_type) {
    (void)table_type;
    g_config_rx++;
    printf("[%s] SensorNode config received: command=%d threshold=%.1f\n",
           g_node_id, g_sensor.device.config.command, g_sensor.device.config.threshold);
}

static void on_sensor_state(const char* table_type, const char* from_node) {
    (void)table_type;
    g_state_rx++;
    printf("[%s] SensorNode state from %s: temp=%.1f humidity=%.1f\n",
           g_node_id, from_node, g_sensor.owner.state.temperature, g_sensor.owner.state.humidity);
}

static void on_sensor_status(const char* table_type, const char* from_node) {
    (void)table_type;
    g_status_rx++;
    printf("[%s] SensorNode status from %s: error=%d battery=%d%%\n",
           g_node_id, from_node, 0, 0);  /* Status slots not fully implemented yet */
}

static void on_actuator_config(const char* table_type) {
    (void)table_type;
    g_config_rx++;
    printf("[%s] ActuatorNode config received: pos=%d speed=%d\n",
           g_node_id, g_actuator.device.config.target_position, g_actuator.device.config.speed);
}

static void on_actuator_state(const char* table_type, const char* from_node) {
    (void)table_type;
    g_state_rx++;
    printf("[%s] ActuatorNode state from %s: pos=%d\n",
           g_node_id, from_node, g_actuator.owner.state.current_position);
}

static void on_actuator_status(const char* table_type, const char* from_node) {
    (void)table_type;
    g_status_rx++;
    printf("[%s] ActuatorNode status from %s\n", g_node_id, from_node);
}

/* ============== Node Setup ============== */

static SdsError setup_node1(void) {
    SdsError err;
    SdsTableOptions opts;
    
    /* SensorNode as OWNER */
    memset(&g_sensor.owner, 0, sizeof(g_sensor.owner));
    g_sensor.owner.config.command = 2;
    g_sensor.owner.config.threshold = 25.5f;
    
    opts.sync_interval_ms = SDS_SENSOR_NODE_SYNC_INTERVAL_MS;
    err = sds_register_table(&g_sensor.owner, "SensorNode", SDS_ROLE_OWNER, &opts);
    if (err != SDS_OK) return err;
    
    sds_on_state_update("SensorNode", on_sensor_state);
    sds_on_status_update("SensorNode", on_sensor_status);
    
    /* ActuatorNode as DEVICE */
    memset(&g_actuator.device, 0, sizeof(g_actuator.device));
    g_actuator.device.state.current_position = 0;
    g_actuator.device.status.motor_status = 0;
    
    opts.sync_interval_ms = SDS_ACTUATOR_NODE_SYNC_INTERVAL_MS;
    err = sds_register_table(&g_actuator.device, "ActuatorNode", SDS_ROLE_DEVICE, &opts);
    if (err != SDS_OK) return err;
    
    sds_on_config_update("ActuatorNode", on_actuator_config);
    
    printf("[%s] Setup: SensorNode=OWNER, ActuatorNode=DEVICE\n", g_node_id);
    return SDS_OK;
}

static SdsError setup_node2(void) {
    SdsError err;
    SdsTableOptions opts;
    
    /* SensorNode as DEVICE */
    memset(&g_sensor.device, 0, sizeof(g_sensor.device));
    g_sensor.device.state.temperature = 23.5f;
    g_sensor.device.state.humidity = 45.0f;
    g_sensor.device.status.battery_percent = 85;
    
    opts.sync_interval_ms = SDS_SENSOR_NODE_SYNC_INTERVAL_MS;
    err = sds_register_table(&g_sensor.device, "SensorNode", SDS_ROLE_DEVICE, &opts);
    if (err != SDS_OK) return err;
    
    sds_on_config_update("SensorNode", on_sensor_config);
    
    /* ActuatorNode as OWNER */
    memset(&g_actuator.owner, 0, sizeof(g_actuator.owner));
    g_actuator.owner.config.target_position = 75;
    g_actuator.owner.config.speed = 50;
    
    opts.sync_interval_ms = SDS_ACTUATOR_NODE_SYNC_INTERVAL_MS;
    err = sds_register_table(&g_actuator.owner, "ActuatorNode", SDS_ROLE_OWNER, &opts);
    if (err != SDS_OK) return err;
    
    sds_on_state_update("ActuatorNode", on_actuator_state);
    sds_on_status_update("ActuatorNode", on_actuator_status);
    
    printf("[%s] Setup: SensorNode=DEVICE, ActuatorNode=OWNER\n", g_node_id);
    return SDS_OK;
}

static SdsError setup_node3(void) {
    SdsError err;
    SdsTableOptions opts;
    
    /* SensorNode as DEVICE */
    memset(&g_sensor.device, 0, sizeof(g_sensor.device));
    g_sensor.device.state.temperature = 24.8f;
    g_sensor.device.state.humidity = 52.0f;
    g_sensor.device.status.battery_percent = 92;
    
    opts.sync_interval_ms = SDS_SENSOR_NODE_SYNC_INTERVAL_MS;
    err = sds_register_table(&g_sensor.device, "SensorNode", SDS_ROLE_DEVICE, &opts);
    if (err != SDS_OK) return err;
    
    sds_on_config_update("SensorNode", on_sensor_config);
    
    /* ActuatorNode as DEVICE */
    memset(&g_actuator.device, 0, sizeof(g_actuator.device));
    g_actuator.device.state.current_position = 0;
    g_actuator.device.status.motor_status = 0;
    
    opts.sync_interval_ms = SDS_ACTUATOR_NODE_SYNC_INTERVAL_MS;
    err = sds_register_table(&g_actuator.device, "ActuatorNode", SDS_ROLE_DEVICE, &opts);
    if (err != SDS_OK) return err;
    
    sds_on_config_update("ActuatorNode", on_actuator_config);
    
    printf("[%s] Setup: SensorNode=DEVICE, ActuatorNode=DEVICE\n", g_node_id);
    return SDS_OK;
}

/* ============== Simulation ============== */

static void simulate(uint32_t elapsed) {
    switch (g_node_num) {
        case 1:
            /* ActuatorNode device simulation */
            g_actuator.device.state.current_position = (elapsed / 100) % 100;
            g_actuator.device.status.motor_status = (elapsed % 2000) < 1000 ? 1 : 0;
            break;
            
        case 2:
            /* SensorNode device simulation */
            g_sensor.device.state.temperature = 23.5f + (float)(elapsed % 1000) / 500.0f;
            g_sensor.device.state.humidity = 45.0f + (float)(elapsed % 500) / 100.0f;
            g_sensor.device.status.uptime_seconds = elapsed / 1000;
            break;
            
        case 3:
            /* Both devices */
            g_sensor.device.state.temperature = 24.8f + (float)(elapsed % 800) / 400.0f;
            g_sensor.device.state.humidity = 52.0f;
            g_sensor.device.status.uptime_seconds = elapsed / 1000;
            
            g_actuator.device.state.current_position = (elapsed / 200) % 100;
            g_actuator.device.status.motor_status = (elapsed % 3000) < 1500 ? 1 : 0;
            break;
    }
}

/* ============== Main ============== */

static void print_usage(const char* prog) {
    printf("Usage: %s <node1|node2|node3> [broker]\n", prog);
    printf("\n");
    printf("  node1: SensorNode=OWNER, ActuatorNode=DEVICE\n");
    printf("  node2: SensorNode=DEVICE, ActuatorNode=OWNER\n");
    printf("  node3: SensorNode=DEVICE, ActuatorNode=DEVICE\n");
}

int main(int argc, char* argv[]) {
    const char* broker = "localhost";
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "node1") == 0) {
        g_node_num = 1;
        g_node_id = "node1";
    } else if (strcmp(argv[1], "node2") == 0) {
        g_node_num = 2;
        g_node_id = "node2";
    } else if (strcmp(argv[1], "node3") == 0) {
        g_node_num = 3;
        g_node_id = "node3";
    } else {
        print_usage(argv[0]);
        return 1;
    }
    
    if (argc > 2) broker = argv[2];
    
    printf("=== SDS Generated Types Test ===\n");
    printf("Node: %s  Broker: %s\n\n", g_node_id, broker);
    
    signal(SIGINT, signal_handler);
    
    SdsConfig config = {
        .node_id = g_node_id,
        .mqtt_broker = broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        printf("sds_init failed: %s\n", sds_error_string(err));
        return 1;
    }
    
    switch (g_node_num) {
        case 1: err = setup_node1(); break;
        case 2: err = setup_node2(); break;
        case 3: err = setup_node3(); break;
    }
    
    if (err != SDS_OK) {
        printf("Setup failed: %s\n", sds_error_string(err));
        sds_shutdown();
        return 1;
    }
    
    uint32_t start = sds_platform_millis();
    uint32_t duration = 15000;
    uint32_t last_stats = 0;
    
    printf("[%s] Running for %u seconds...\n\n", g_node_id, duration / 1000);
    
    while (g_running) {
        uint32_t now = sds_platform_millis();
        uint32_t elapsed = now - start;
        
        simulate(elapsed);
        sds_loop();
        
        if (now - last_stats >= 3000) {
            printf("[%s] @%us: cfg_rx=%d state_rx=%d status_rx=%d\n",
                   g_node_id, elapsed / 1000, g_config_rx, g_state_rx, g_status_rx);
            last_stats = now;
        }
        
        if (elapsed >= duration) break;
        sds_platform_delay_ms(50);
    }
    
    /* Results */
    printf("\n=== [%s] Results ===\n", g_node_id);
    printf("Config: %d  State: %d  Status: %d\n", g_config_rx, g_state_rx, g_status_rx);
    
    const SdsStats* stats = sds_get_stats();
    printf("Messages: sent=%u received=%u\n", stats->messages_sent, stats->messages_received);
    
    bool passed = true;
    switch (g_node_num) {
        case 1:
            /* Check ActuatorNode config by value (retained message may arrive before callback) */
            if (g_actuator.device.config.target_position == 75) {
                printf("✓ ActuatorNode config received: pos=%d\n", g_actuator.device.config.target_position);
            } else if (g_config_rx > 0) {
                printf("✓ ActuatorNode config callback fired\n");
            } else {
                printf("✗ No ActuatorNode config\n"); passed = false;
            }
            if (g_state_rx == 0) { printf("✗ No SensorNode state received\n"); passed = false; }
            else printf("✓ SensorNode state received (%d)\n", g_state_rx);
            break;
        case 2:
            /* Check SensorNode config by value */
            if (g_sensor.device.config.command == 2) {
                printf("✓ SensorNode config received: cmd=%d\n", g_sensor.device.config.command);
            } else if (g_config_rx > 0) {
                printf("✓ SensorNode config callback fired\n");
            } else {
                printf("✗ No SensorNode config\n"); passed = false;
            }
            if (g_state_rx == 0) { printf("✗ No ActuatorNode state received\n"); passed = false; }
            else printf("✓ ActuatorNode state received (%d)\n", g_state_rx);
            break;
        case 3:
            /* Check both configs by value */
            if (g_sensor.device.config.command == 2 && g_sensor.device.config.threshold > 25.0f) {
                printf("✓ SensorNode config: cmd=%d thresh=%.1f\n", 
                       g_sensor.device.config.command, g_sensor.device.config.threshold);
            } else {
                printf("✗ SensorNode config not received\n"); passed = false;
            }
            if (g_actuator.device.config.target_position == 75 && g_actuator.device.config.speed == 50) {
                printf("✓ ActuatorNode config: pos=%d speed=%d\n",
                       g_actuator.device.config.target_position, g_actuator.device.config.speed);
            } else {
                printf("✗ ActuatorNode config not received\n"); passed = false;
            }
            break;
    }
    
    printf("\nOverall: %s\n", passed ? "PASSED" : "FAILED");
    
    sds_shutdown();
    return passed ? 0 : 1;
}

