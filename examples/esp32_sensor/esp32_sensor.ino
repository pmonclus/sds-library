/*
 * esp32_sensor.ino - SDS ESP32/ESP8266 Sensor Example
 * 
 * Demonstrates a SensorData device that:
 *   - Receives config from the owner
 *   - Publishes temperature/humidity state
 *   - Publishes device status
 * 
 * Hardware: ESP32 or ESP8266 board
 * 
 * Setup:
 *   1. Copy config.h.example to config.h
 *   2. Edit config.h with your WiFi/MQTT settings
 *   3. Run codegen to generate sds_types.h from your schema.sds
 *   4. Upload to board: pio run -e esp32dev -t upload (or esp8266)
 */

#include "config.h"
#include <SDS.h>
#include "sds_types.h"  /* Generated from schema.sds */

/* WiFi library - auto-selected based on platform */
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

/* ============== Forward Declarations ============== */

bool connect_wifi();
float read_temperature();
float read_humidity();
void on_config_update(const char* table_type);

/* ============== SDS Client and Tables ============== */

SDSClient sds;
SensorDataTable sensor_table;

/* ============== Setup ============== */

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    
    Serial.println("\n========================================");
    Serial.printf("  %s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
    Serial.println("========================================\n");
    
    #if STATUS_LED_PIN >= 0
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    #endif
    
    /* Connect to WiFi */
    if (!connect_wifi()) {
        return;
    }
    
    /* Build node ID from prefix + MAC */
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String node_id = String(NODE_ID_PREFIX) + "_" + mac.substring(6);
    
    /* Initialize SDS */
    if (!sds.begin(node_id.c_str(), MQTT_BROKER, MQTT_PORT)) {
        Serial.println("SDS initialization failed!");
        return;
    }
    
    Serial.printf("SDS initialized: node_id=%s\n", sds.getNodeId());
    Serial.printf("MQTT broker: %s:%d\n", MQTT_BROKER, MQTT_PORT);
    
    /* Initialize table with defaults */
    memset(&sensor_table, 0, sizeof(sensor_table));
    sensor_table.status.battery_percent = 100;
    
    /* Register as DEVICE using simple API */
    SdsTableOptions opts = { .sync_interval_ms = SDS_SENSOR_DATA_SYNC_INTERVAL_MS };
    SdsError err = sds_register_table(&sensor_table, "SensorData", SDS_ROLE_DEVICE, &opts);
    
    if (err != SDS_OK) {
        Serial.printf("Failed to register SensorData: %s\n", SDSClient::errorString(err));
        return;
    }
    
    /* Set up callbacks */
    sds_on_config_update("SensorData", on_config_update);
    
    Serial.println("\nSensorData registered as DEVICE");
    Serial.println("Waiting for config from owner...\n");
    
    #if STATUS_LED_PIN >= 0
    digitalWrite(STATUS_LED_PIN, HIGH);  /* LED on = connected */
    #endif
}

/* ============== Loop ============== */

void loop() {
    static unsigned long last_sensor_update = 0;
    static unsigned long start_time = 0;
    
    /* Always call sds.loop() - it handles MQTT reconnection internally */
    sds.loop();
    
    /* Check if connected - if not, blink LED and skip sensor work */
    if (!sds.isReady()) {
        #if STATUS_LED_PIN >= 0
        /* Blink slowly when disconnected */
        static unsigned long last_blink = 0;
        if (millis() - last_blink > 1000) {
            last_blink = millis();
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
        }
        #endif
        return;
    }
    
    if (start_time == 0) {
        start_time = millis();
    }
    
    /* Update sensor data at configured interval */
    if (millis() - last_sensor_update >= SENSOR_UPDATE_MS) {
        last_sensor_update = millis();
        
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
        
        /* Debug output */
        #if DEBUG_ENABLED && DEBUG_PRINT_INTERVAL_SEC > 0
        static unsigned long last_print = 0;
        if (millis() - last_print >= DEBUG_PRINT_INTERVAL_SEC * 1000) {
            last_print = millis();
            Serial.printf("[%lu] temp=%.1f hum=%.1f cmd=%d uptime=%us\n",
                          millis() / 1000,
                          sensor_table.state.temperature,
                          sensor_table.state.humidity,
                          sensor_table.config.command,
                          sensor_table.status.uptime_seconds);
        }
        #endif
    }
}

/* ============== WiFi Connection ============== */

bool connect_wifi() {
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_TIMEOUT_SEC * 2) {
        delay(500);
        Serial.print(".");
        attempts++;
        
        #if STATUS_LED_PIN >= 0
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
        #endif
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi connection failed!");
        return false;
    }
    
    Serial.println();
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

/* ============== Sensor Functions ============== */

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
    
    #if STATUS_LED_PIN >= 0
    /* Blink LED to indicate config received */
    for (int i = 0; i < 3; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH);
        delay(100);
        digitalWrite(STATUS_LED_PIN, LOW);
        delay(100);
    }
    #endif
}
