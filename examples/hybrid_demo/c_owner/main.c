/**
 * Hybrid Demo - C Owner
 * 
 * An owner node that controls devices and displays their data.
 * 
 * Commands:
 *   led on/off      - Toggle LED on all devices
 *   active <id>     - Set which device publishes state
 *   active none     - Disable state publishing
 *   status          - Show all device statuses
 *   verbose on/off  - Toggle live state/status messages
 *   help            - Show available commands
 *   quit            - Exit
 * 
 * Usage: ./owner [broker_host] [node_id]
 * Example: ./owner localhost c_owner
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <ctype.h>

#include "../lib/include/sds.h"
#include "../lib/include/demo_types.h"

/* Global state */
static volatile int running = 1;
static int verbose = 0;  /* Control live state/status printing */

/* Track last state update */
static char last_state_from[32] = "";
static time_t last_state_time = 0;
static float last_temperature = 0.0f;
static float last_humidity = 0.0f;

/* Signal handler */
static void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down...\n");
    running = 0;
}

/* Print help */
static void print_help(void) {
    printf("\nCommands:\n");
    printf("  led on/off      - Toggle LED on all devices\n");
    printf("  active <id>     - Set active device (e.g., 'active c_dev_01')\n");
    printf("  active none     - Disable state publishing\n");
    printf("  status          - Show all device statuses\n");
    printf("  verbose on/off  - Toggle live state/status messages\n");
    printf("  help            - Show this help\n");
    printf("  quit            - Exit\n");
    printf("\n");
}

/* State update callback */
static void on_state_update(const char* table_type, const char* from_node, void* user_data) {
    (void)table_type;
    DeviceDemoOwnerTable* table = (DeviceDemoOwnerTable*)user_data;
    
    if (!table) return;
    
    /* Store last state info */
    strncpy(last_state_from, from_node, sizeof(last_state_from) - 1);
    last_state_from[sizeof(last_state_from) - 1] = '\0';
    last_state_time = time(NULL);
    last_temperature = table->state.temperature;
    last_humidity = table->state.humidity;
    
    if (verbose) {
        printf("\n[STATE] from %s: temp=%.1fC, humidity=%.1f%%\n", 
               from_node, table->state.temperature, table->state.humidity);
        printf("> ");
        fflush(stdout);
    }
}

/* Status update callback */
static void on_status_update(const char* table_type, const char* from_node, void* user_data) {
    (void)table_type;
    (void)user_data;
    
    if (verbose) {
        printf("\n[STATUS] from %s\n", from_node);
        printf("> ");
        fflush(stdout);
    }
}

/* Lowercase a string in place */
static void str_tolower(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = (char)tolower((unsigned char)str[i]);
    }
}

/* Trim whitespace from both ends */
static char* str_trim(char* str) {
    /* Trim leading */
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    
    /* Trim trailing */
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    
    return str;
}


/* Process a command */
static void process_command(char* cmd, DeviceDemoOwnerTable* table) {
    cmd = str_trim(cmd);
    str_tolower(cmd);
    
    if (strlen(cmd) == 0) return;
    
    /* Parse command and arguments */
    char* command = strtok(cmd, " \t");
    char* arg = strtok(NULL, " \t");
    
    if (strcmp(command, "led") == 0) {
        if (!arg) {
            printf("Usage: led on/off\n");
        } else if (strcmp(arg, "on") == 0) {
            table->config.led_control = 1;
            printf("LED set to ON\n");
        } else if (strcmp(arg, "off") == 0) {
            table->config.led_control = 0;
            printf("LED set to OFF\n");
        } else {
            printf("Usage: led on/off\n");
        }
    }
    else if (strcmp(command, "active") == 0) {
        if (!arg) {
            printf("Current active device: '%s'\n", 
                   table->config.active_device[0] ? table->config.active_device : "none");
            printf("Usage: active <device_id> or active none\n");
        } else if (strcmp(arg, "none") == 0) {
            table->config.active_device[0] = '\0';
            printf("Active device cleared - no device will publish state\n");
        } else {
            strncpy(table->config.active_device, arg, sizeof(table->config.active_device) - 1);
            table->config.active_device[sizeof(table->config.active_device) - 1] = '\0';
            printf("Active device set to: %s\n", table->config.active_device);
        }
    }
    else if (strcmp(command, "status") == 0) {
        printf("\n--- Device Status ---\n");
        printf("Config: LED=%s, active='%s'\n",
               table->config.led_control ? "ON" : "OFF",
               table->config.active_device[0] ? table->config.active_device : "none");
        
        if (table->status_count > 0) {
            printf("Known devices: %d\n", table->status_count);
            for (int i = 0; i < table->status_count && i < 8; i++) {
                DeviceDemoStatusSlot* slot = &table->status_slots[i];
                if (slot->valid) {
                    printf("  - %s: %s, power=%.1fW, log=\"%s\"\n",
                           slot->node_id,
                           slot->online ? "ONLINE" : "OFFLINE",
                           slot->status.power_consumption,
                           slot->status.latest_log);
                }
            }
        } else {
            printf("No devices connected yet.\n");
        }
        
        if (last_state_from[0]) {
            time_t age = time(NULL) - last_state_time;
            printf("\nLast state from %s (%lds ago):\n", last_state_from, age);
            printf("  temperature=%.1fC\n", last_temperature);
            printf("  humidity=%.1f%%\n", last_humidity);
        }
        printf("\n");
    }
    else if (strcmp(command, "verbose") == 0) {
        if (!arg) {
            printf("Verbose mode: %s\n", verbose ? "ON" : "OFF");
            printf("Usage: verbose on/off\n");
        } else if (strcmp(arg, "on") == 0) {
            verbose = 1;
            printf("Verbose mode ON - live state/status messages enabled\n");
        } else if (strcmp(arg, "off") == 0) {
            verbose = 0;
            printf("Verbose mode OFF - live messages disabled\n");
        } else {
            printf("Usage: verbose on/off\n");
        }
    }
    else if (strcmp(command, "help") == 0) {
        print_help();
    }
    else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
        running = 0;
    }
    else {
        printf("Unknown command: %s. Type 'help' for commands.\n", command);
    }
}

int main(int argc, char* argv[]) {
    /* Parse arguments */
    const char* broker_host = argc > 1 ? argv[1] : "localhost";
    const char* node_id = argc > 2 ? argv[2] : "c_owner";
    
    printf("============================================================\n");
    printf("  Hybrid Demo - C Owner\n");
    printf("============================================================\n");
    printf("  Node ID: %s\n", node_id);
    printf("  Broker:  %s:1883\n", broker_host);
    printf("============================================================\n\n");
    
    /* Set up signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
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
    
    /* Allocate and register table as OWNER */
    static DeviceDemoOwnerTable table = {0};
    
    err = sds_register_table(&table, "DeviceDemo", SDS_ROLE_OWNER, NULL);
    if (err != SDS_OK) {
        printf("Failed to register table: %s\n", sds_error_string(err));
        sds_shutdown();
        return 1;
    }
    printf("Registered as OWNER for DeviceDemo table\n");
    
    /* Set up callbacks */
    sds_on_state_update("DeviceDemo", on_state_update, &table);
    sds_on_status_update("DeviceDemo", on_status_update, &table);
    
    /* Set initial config */
    table.config.led_control = 0;
    table.config.active_device[0] = '\0';
    printf("\nInitial config: LED=OFF, active_device=none\n");
    
    print_help();
    printf("Waiting for devices to connect...\n\n");
    
    /* Command buffer */
    char cmd_buffer[256];
    
    printf("> ");
    fflush(stdout);
    
    /* Main loop */
    while (running) {
        /* Use select() to wait for stdin OR timeout for MQTT processing */
        fd_set fds;
        struct timeval tv = {0, 50000};  /* 50ms timeout */
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        
        int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        
        /* Check for command input */
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            if (fgets(cmd_buffer, sizeof(cmd_buffer), stdin)) {
                process_command(cmd_buffer, &table);
                if (running) {
                    printf("> ");
                    fflush(stdout);
                }
            } else {
                /* EOF */
                running = 0;
            }
        }
        
        /* Process MQTT messages */
        sds_loop();
    }
    
    /* Cleanup */
    sds_unregister_table("DeviceDemo");
    sds_shutdown();
    printf("Owner stopped.\n");
    
    return 0;
}
