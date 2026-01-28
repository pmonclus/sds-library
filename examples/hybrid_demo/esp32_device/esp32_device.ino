/**
 * Hybrid Demo - ESP32 Device
 * 
 * A simulated sensor device on ESP32 that:
 * - Responds to LED control from owner (using built-in LED)
 * - Publishes temperature/humidity if it's the "active" device
 * - Always publishes power consumption and rotating log messages
 * 
 * Configuration:
 *   Copy config.h.example to config.h and edit with your WiFi/MQTT settings.
 */

#include <WiFi.h>
#include <PubSubClient.h>

// Configuration - copy config.h.example to config.h and edit
#include "config.h"

// SDS Library (extern "C" for C++ compatibility)
extern "C" {
#include "sds.h"
#include "sds_platform.h"
#include "demo_types.h"
}

// ============== Constants ==============

#define LED_PIN 2  // Built-in LED on most ESP32 boards

// Log message rotation
static const char* log_messages[] = {
    "I am %s and I feel good!",
    "I feel gloomy, %s here",
    "I need help, help %s!",
    "All systems nominal, %s reporting",
    "Running smoothly, %s out"
};
static int log_index = 0;
#define NUM_LOG_MESSAGES (sizeof(log_messages) / sizeof(log_messages[0]))

// ============== Global State ==============

static DeviceDemoTable table = {0};
static int led_state = 0;

static unsigned long last_status_time = 0;
static unsigned long last_state_time = 0;

// ============== Sensor Simulation ==============

float read_temperature() {
    static float base_temp = 22.0f;
    float variation = (random(100) / 100.0f) * 2.0f - 1.0f;
    base_temp += variation * 0.1f;
    base_temp = constrain(base_temp, 18.0f, 28.0f);
    return base_temp;
}

float read_humidity() {
    static float base_humidity = 50.0f;
    float variation = (random(100) / 100.0f) * 4.0f - 2.0f;
    base_humidity += variation * 0.2f;
    base_humidity = constrain(base_humidity, 30.0f, 70.0f);
    return base_humidity;
}

float read_power_consumption() {
    float base_power = 0.15f;  // 150mW idle for ESP32
    if (led_state) {
        base_power += 0.02f;   // LED adds 20mW
    }
    float variation = (random(100) / 100.0f) * 0.01f;
    return base_power + variation;
}

void get_log_message(char* buffer, size_t size) {
    snprintf(buffer, size, log_messages[log_index], NODE_ID);
    log_index = (log_index + 1) % NUM_LOG_MESSAGES;
}

// ============== Callbacks ==============

void on_config_update(const char* table_type, void* user_data) {
    (void)table_type;
    DeviceDemoTable* tbl = (DeviceDemoTable*)user_data;
    
    if (!tbl) return;
    
    // Handle LED control
    int new_led_state = tbl->config.led_control;
    if (new_led_state != led_state) {
        led_state = new_led_state;
        digitalWrite(LED_PIN, led_state ? HIGH : LOW);
        Serial.print("[LED] ");
        Serial.println(led_state ? "ON" : "OFF");
    }
    
    // Handle active device change
    static char prev_active[32] = "";
    if (strcmp(tbl->config.active_device, prev_active) != 0) {
        strncpy(prev_active, tbl->config.active_device, sizeof(prev_active) - 1);
        prev_active[sizeof(prev_active) - 1] = '\0';
        
        bool is_active = (strcmp(tbl->config.active_device, NODE_ID) == 0);
        Serial.print("[ACTIVE] ");
        Serial.print(prev_active[0] ? prev_active : "none");
        Serial.print(" -> I am ");
        Serial.println(is_active ? "ACTIVE (will publish state)" : "INACTIVE");
    }
}

// ============== Setup ==============

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("============================================================");
    Serial.println("  Hybrid Demo - ESP32 Device");
    Serial.println("============================================================");
    Serial.print("  Node ID: ");
    Serial.println(NODE_ID);
    Serial.print("  MQTT Broker: ");
    Serial.println(MQTT_BROKER);
    Serial.println("============================================================");
    Serial.println();
    
    // Set up LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Seed random
    randomSeed(analogRead(0) ^ micros());
    
    // Configure SDS
    SdsConfig config = {
        .node_id = NODE_ID,
        .mqtt_broker = MQTT_BROKER,
        .mqtt_port = MQTT_PORT,
        .mqtt_username = MQTT_USERNAME[0] ? MQTT_USERNAME : NULL,
        .mqtt_password = MQTT_PASSWORD[0] ? MQTT_PASSWORD : NULL
    };
    
    // Initialize SDS
    SdsError err = sds_init(&config);
    if (err != SDS_OK) {
        Serial.print("Failed to initialize SDS: ");
        Serial.println(sds_error_string(err));
        while (1) delay(1000);
    }
    Serial.println("Connected to MQTT broker");
    
    // Register table as DEVICE
    err = sds_register_table(&table, "DeviceDemo", SDS_ROLE_DEVICE, NULL);
    if (err != SDS_OK) {
        Serial.print("Failed to register table: ");
        Serial.println(sds_error_string(err));
        sds_shutdown();
        while (1) delay(1000);
    }
    Serial.println("Registered as DEVICE for DeviceDemo table");
    
    // Set up config callback
    sds_on_config_update("DeviceDemo", on_config_update, &table);
    
    Serial.println();
    Serial.println("Device running...");
    Serial.println();
}

// ============== Main Loop ==============

void loop() {
    // Process MQTT messages
    sds_loop();
    
    unsigned long now = millis();
    
    // Publish state if we are the active device (every 1 second)
    if (now - last_state_time >= 1000) {
        last_state_time = now;
        
        if (strcmp(table.config.active_device, NODE_ID) == 0) {
            table.state.temperature = read_temperature();
            table.state.humidity = read_humidity();
            Serial.print("[STATE] temp=");
            Serial.print(table.state.temperature, 1);
            Serial.print("C, humidity=");
            Serial.print(table.state.humidity, 1);
            Serial.println("%");
        }
    }
    
    // Publish status (every 5 seconds)
    if (now - last_status_time >= 5000) {
        last_status_time = now;
        
        table.status.power_consumption = read_power_consumption();
        get_log_message(table.status.latest_log, sizeof(table.status.latest_log));
        
        Serial.print("[STATUS] power=");
        Serial.print(table.status.power_consumption, 2);
        Serial.print("W, log=\"");
        Serial.print(table.status.latest_log);
        Serial.println("\"");
    }
    
    delay(100);
}
