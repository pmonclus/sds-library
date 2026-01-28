/**
 * Hybrid Demo - Linux Device
 * 
 * A simulated sensor device that:
 * - Responds to LED control from owner
 * - Publishes temperature/humidity if it's the "active" device
 * - Always publishes power consumption and rotating log messages
 * 
 * Usage: ./device <node_id> [broker_host]
 * Example: ./device linux_dev_01 localhost
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#include "../lib/include/sds.h"
#include "../lib/include/demo_types.h"

/* Global state */
static volatile int running = 1;
static const char* node_id = NULL;

/* Simulated LED state */
static int led_state = 0;

/* Log message rotation */
static const char* log_messages[] = {
    "I am %s and I feel good!",
    "I feel gloomy, %s here",
    "I need help, help %s!",
    "All systems nominal, %s reporting",
    "Running smoothly, %s out"
};
static int log_index = 0;
#define NUM_LOG_MESSAGES (sizeof(log_messages) / sizeof(log_messages[0]))

/* Signal handler */
static void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down...\n");
    running = 0;
}

/* Simulate temperature reading */
static float read_temperature(void) {
    /* Base temperature with some variation */
    static float base_temp = 22.0f;
    float variation = ((float)(rand() % 100) / 100.0f) * 2.0f - 1.0f; /* -1 to +1 */
    base_temp += variation * 0.1f;
    if (base_temp < 18.0f) base_temp = 18.0f;
    if (base_temp > 28.0f) base_temp = 28.0f;
    return base_temp;
}

/* Simulate humidity reading */
static float read_humidity(void) {
    static float base_humidity = 50.0f;
    float variation = ((float)(rand() % 100) / 100.0f) * 4.0f - 2.0f; /* -2 to +2 */
    base_humidity += variation * 0.2f;
    if (base_humidity < 30.0f) base_humidity = 30.0f;
    if (base_humidity > 70.0f) base_humidity = 70.0f;
    return base_humidity;
}

/* Simulate power consumption */
static float read_power_consumption(void) {
    /* Base power + LED power if on + random variation */
    float base_power = 2.5f;  /* 2.5W idle */
    if (led_state) {
        base_power += 0.5f;   /* LED adds 0.5W */
    }
    float variation = ((float)(rand() % 100) / 100.0f) * 0.2f; /* 0 to 0.2 */
    return base_power + variation;
}

/* Get next log message */
static void get_log_message(char* buffer, size_t size) {
    snprintf(buffer, size, log_messages[log_index], node_id);
    log_index = (log_index + 1) % NUM_LOG_MESSAGES;
}

/* Config update callback - handles LED and active_device changes */
static void on_config_update(const char* table_type, void* user_data) {
    (void)table_type;
    DeviceDemoTable* table = (DeviceDemoTable*)user_data;
    
    if (!table) return;
    
    /* Handle LED control */
    int new_led_state = table->config.led_control;
    if (new_led_state != led_state) {
        led_state = new_led_state;
        printf("[LED] %s\n", led_state ? "ON" : "OFF");
    }
    
    /* Handle active device change */
    static char prev_active[32] = "";
    if (strcmp(table->config.active_device, prev_active) != 0) {
        strncpy(prev_active, table->config.active_device, sizeof(prev_active) - 1);
        prev_active[sizeof(prev_active) - 1] = '\0';
        
        int is_active = (strcmp(table->config.active_device, node_id) == 0);
        printf("[ACTIVE] %s -> I am %s\n", 
               prev_active[0] ? prev_active : "none",
               is_active ? "ACTIVE (will publish state)" : "INACTIVE");
    }
}

int main(int argc, char* argv[]) {
    /* Parse arguments */
    if (argc < 2) {
        printf("Usage: %s <node_id> [broker_host]\n", argv[0]);
        printf("Example: %s linux_dev_01 localhost\n", argv[0]);
        return 1;
    }
    
    node_id = argv[1];
    const char* broker_host = argc > 2 ? argv[2] : "localhost";
    
    printf("============================================================\n");
    printf("  Hybrid Demo - Linux Device\n");
    printf("============================================================\n");
    printf("  Node ID: %s\n", node_id);
    printf("  Broker:  %s:1883\n", broker_host);
    printf("============================================================\n\n");
    
    /* Set up signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Seed random number generator */
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    
    /* Configure SDS */
    SdsConfig config = {
        .node_id = node_id,
        .mqtt_broker = broker_host,
        .mqtt_port = 1883,
        .mqtt_username = NULL,
        .mqtt_password = NULL
    };
    
    /* Initialize SDS */
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        printf("Failed to initialize SDS: %s\n", sds_error_string(err));
        return 1;
    }
    printf("Connected to MQTT broker\n");
    
    /* Allocate and register table as DEVICE */
    static DeviceDemoTable table = {0};
    
    err = sds_register_table(&table, "DeviceDemo", SDS_ROLE_DEVICE, NULL);
    if (err != SDS_OK) {
        printf("Failed to register table: %s\n", sds_error_string(err));
        sds_shutdown();
        return 1;
    }
    printf("Registered as DEVICE for DeviceDemo table\n\n");
    
    /* Set up config callback */
    sds_on_config_update("DeviceDemo", on_config_update, &table);
    
    /* Timing variables */
    time_t last_status_time = 0;
    time_t last_state_time = 0;
    
    printf("Device running. Press Ctrl+C to stop.\n\n");
    
    /* Main loop */
    while (running) {
        /* Process MQTT messages */
        sds_loop();
        
        time_t now = time(NULL);
        
        /* Publish state if we are the active device (every 1 second) */
        if (now - last_state_time >= 1) {
            last_state_time = now;
            
            if (strcmp(table.config.active_device, node_id) == 0) {
                table.state.temperature = read_temperature();
                table.state.humidity = read_humidity();
                printf("[STATE] temp=%.1fC, humidity=%.1f%%\n", 
                       table.state.temperature, table.state.humidity);
            }
        }
        
        /* Publish status (every 5 seconds) */
        if (now - last_status_time >= 5) {
            last_status_time = now;
            
            table.status.power_consumption = read_power_consumption();
            get_log_message(table.status.latest_log, sizeof(table.status.latest_log));
            
            printf("[STATUS] power=%.1fW, log=\"%s\"\n",
                   table.status.power_consumption, table.status.latest_log);
        }
        
        /* Small delay to prevent CPU spinning */
        usleep(100000);  /* 100ms */
    }
    
    /* Cleanup */
    sds_unregister_table("DeviceDemo");
    sds_shutdown();
    printf("Device stopped.\n");
    
    return 0;
}
