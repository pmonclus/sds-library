/*
 * esp32_sensor.ino - SDS ESP32 Sensor Example
 * 
 * Demonstrates a SensorNode device that:
 *   - Receives config from the owner
 *   - Publishes temperature/humidity state
 *   - Publishes device status
 * 
 * Hardware: Any ESP32 board
 * 
 * Configuration:
 *   1. Edit WiFi credentials below
 *   2. Edit MQTT broker IP
 *   3. Run codegen to generate sds_types.h from your schema.sds
 *   4. Upload to ESP32
 */

#include <WiFi.h>
#include <SDS.h>
#include "sds_types.h"  /* Generated from schema.sds */

/* ============== Configuration ============== */

const char* WIFI_SSID = "your-wifi-ssid";
const char* WIFI_PASSWORD = "your-wifi-password";
const char* MQTT_BROKER = "192.168.1.100";
const uint16_t MQTT_PORT = 1883;

/* ============== SDS Client and Tables ============== */

SDSClient sds;
SensorNodeTable sensor_table;

/* ============== Sensor Simulation ============== */

float read_temperature() {
    /* In real application, read from DHT22, BME280, etc. */
    return 22.0f + random(-20, 20) / 10.0f;
}

float read_humidity() {
    return 45.0f + random(-50, 50) / 10.0f;
}

/* ============== Callbacks ============== */

void on_config_update(const char* table_type) {
    Serial.printf("[APP] Config updated for %s\n", table_type);
    Serial.printf("      command=%d threshold=%.1f\n",
                  sensor_table.config.command,
                  sensor_table.config.threshold);
    
    /* React to config changes */
    switch (sensor_table.config.command) {
        case 0:
            Serial.println("      -> Sensor OFF");
            break;
        case 1:
            Serial.println("      -> Sensor ON");
            break;
        case 2:
            Serial.println("      -> Sensor AUTO");
            break;
    }
}

/* ============== Setup ============== */

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== SDS ESP32 Sensor Example ===");
    
    /* Connect to WiFi */
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi connection failed!");
        return;
    }
    
    Serial.println();
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    
    /* Initialize SDS */
    String node_id = "sensor_" + WiFi.macAddress().substring(9);
    node_id.replace(":", "");
    
    if (!sds.begin(node_id.c_str(), MQTT_BROKER, MQTT_PORT)) {
        Serial.println("SDS initialization failed!");
        return;
    }
    
    Serial.printf("SDS initialized: node_id=%s\n", sds.getNodeId());
    
    /* Initialize table with defaults */
    memset(&sensor_table, 0, sizeof(sensor_table));
    sensor_table.status.battery_percent = 100;
    
    /* Register as DEVICE using simple API */
    SdsTableOptions opts = { .sync_interval_ms = SDS_SENSOR_NODE_SYNC_INTERVAL_MS };
    SdsError err = sds_register_table(&sensor_table, "SensorNode", SDS_ROLE_DEVICE, &opts);
    
    if (err != SDS_OK) {
        Serial.printf("Failed to register SensorNode: %s\n", SDSClient::errorString(err));
        return;
    }
    
    /* Set up callbacks */
    sds_on_config_update("SensorNode", on_config_update);
    
    Serial.println("SensorNode registered as DEVICE");
    Serial.println("Waiting for config from owner...\n");
}

/* ============== Loop ============== */

unsigned long last_update = 0;
unsigned long start_time = 0;

void loop() {
    if (!sds.isReady()) {
        return;
    }
    
    if (start_time == 0) {
        start_time = millis();
    }
    
    sds.loop();
    
    /* Update sensor data every second */
    if (millis() - last_update >= 1000) {
        last_update = millis();
        
        /* Only read sensors if command is ON (1) or AUTO (2) */
        if (sensor_table.config.command >= 1) {
            /* Update state */
            sensor_table.state.temperature = read_temperature();
            sensor_table.state.humidity = read_humidity();
            
            /* Check threshold (if in AUTO mode) */
            if (sensor_table.config.command == 2) {
                if (sensor_table.state.temperature > sensor_table.config.threshold) {
                    sensor_table.status.error_code = 1;  /* Over threshold */
                } else {
                    sensor_table.status.error_code = 0;
                }
            }
        }
        
        /* Update status */
        sensor_table.status.uptime_seconds = (millis() - start_time) / 1000;
        
        /* Print status every 5 seconds */
        if ((millis() / 1000) % 5 == 0) {
            Serial.printf("[%lu] temp=%.1f hum=%.1f cmd=%d uptime=%us\n",
                          millis() / 1000,
                          sensor_table.state.temperature,
                          sensor_table.state.humidity,
                          sensor_table.config.command,
                          sensor_table.status.uptime_seconds);
        }
    }
}

