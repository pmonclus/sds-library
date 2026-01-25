/*
 * sds_platform_esp32.cpp - ESP32/Arduino Platform Implementation
 * 
 * For ESP32/ESP8266 using Arduino framework and PubSubClient.
 * 
 * Dependencies (PlatformIO lib_deps):
 *   - knolleary/PubSubClient
 *   - WiFi (built-in for ESP32)
 */

#include "sds_platform.h"

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

/* ============== Configuration ============== */

#define SDS_MQTT_BUFFER_SIZE    512
#define SDS_MQTT_KEEPALIVE      60
#define SDS_RECONNECT_DELAY_MS  5000

/* ============== Internal State ============== */

static WiFiClient _wifi_client;
static PubSubClient _mqtt_client(_wifi_client);

static bool _initialized = false;
static char _client_id[64] = "";
static char _broker[128] = "";
static uint16_t _port = 1883;

static SdsMqttMessageCallback _message_callback = nullptr;

/* ============== MQTT Callback ============== */

static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
    if (_message_callback && payload && length > 0) {
        _message_callback(topic, payload, length);
    }
}

/* ============== Platform Init/Shutdown ============== */

extern "C" bool sds_platform_init(void) {
    if (_initialized) {
        return true;
    }
    
    _mqtt_client.setBufferSize(SDS_MQTT_BUFFER_SIZE);
    _mqtt_client.setKeepAlive(SDS_MQTT_KEEPALIVE);
    _mqtt_client.setCallback(mqtt_callback);
    
    _initialized = true;
    SDS_LOG_I("Platform initialized (ESP32)");
    return true;
}

extern "C" void sds_platform_shutdown(void) {
    if (!_initialized) {
        return;
    }
    
    if (_mqtt_client.connected()) {
        _mqtt_client.disconnect();
    }
    
    _initialized = false;
    SDS_LOG_I("Platform shutdown");
}

/* ============== MQTT Operations ============== */

extern "C" bool sds_platform_mqtt_connect(const char* broker, uint16_t port, const char* client_id) {
    if (!_initialized) {
        SDS_LOG_E("Platform not initialized");
        return false;
    }
    
    /* Check WiFi connection */
    if (WiFi.status() != WL_CONNECTED) {
        SDS_LOG_E("WiFi not connected");
        return false;
    }
    
    /* Store connection info for reconnects */
    strncpy(_broker, broker, sizeof(_broker) - 1);
    _broker[sizeof(_broker) - 1] = '\0';
    strncpy(_client_id, client_id, sizeof(_client_id) - 1);
    _client_id[sizeof(_client_id) - 1] = '\0';
    _port = port;
    
    /* Configure and connect */
    _mqtt_client.setServer(_broker, _port);
    
    if (!_mqtt_client.connect(_client_id)) {
        SDS_LOG_E("Failed to connect to MQTT broker %s:%u", _broker, _port);
        return false;
    }
    
    SDS_LOG_I("Connected to MQTT broker: %s:%u", _broker, _port);
    return true;
}

extern "C" void sds_platform_mqtt_disconnect(void) {
    if (_mqtt_client.connected()) {
        _mqtt_client.disconnect();
        SDS_LOG_I("Disconnected from MQTT broker");
    }
}

extern "C" bool sds_platform_mqtt_connected(void) {
    return _mqtt_client.connected();
}

extern "C" bool sds_platform_mqtt_publish(
    const char* topic,
    const uint8_t* payload,
    size_t payload_len,
    bool retained
) {
    if (!_mqtt_client.connected()) {
        return false;
    }
    
    bool success = _mqtt_client.publish(topic, payload, payload_len, retained);
    if (!success) {
        SDS_LOG_E("Failed to publish to %s", topic);
    }
    return success;
}

extern "C" bool sds_platform_mqtt_subscribe(const char* topic) {
    if (!_mqtt_client.connected()) {
        return false;
    }
    
    bool success = _mqtt_client.subscribe(topic);
    if (!success) {
        SDS_LOG_E("Failed to subscribe to %s", topic);
    } else {
        SDS_LOG_D("Subscribed to: %s", topic);
    }
    return success;
}

extern "C" bool sds_platform_mqtt_unsubscribe(const char* topic) {
    if (!_mqtt_client.connected()) {
        return false;
    }
    
    bool success = _mqtt_client.unsubscribe(topic);
    if (!success) {
        SDS_LOG_E("Failed to unsubscribe from %s", topic);
    } else {
        SDS_LOG_D("Unsubscribed from: %s", topic);
    }
    return success;
}

extern "C" void sds_platform_mqtt_loop(void) {
    _mqtt_client.loop();
}

extern "C" void sds_platform_mqtt_set_callback(SdsMqttMessageCallback callback) {
    _message_callback = callback;
}

/* ============== Timing ============== */

extern "C" uint32_t sds_platform_millis(void) {
    return millis();
}

extern "C" void sds_platform_delay_ms(uint32_t ms) {
    delay(ms);
}

/* ============== Logging ============== */

static const char* log_level_str(SdsLogLevel level) {
    switch (level) {
        case SDS_LOG_ERROR: return "E";
        case SDS_LOG_WARN:  return "W";
        case SDS_LOG_INFO:  return "I";
        case SDS_LOG_DEBUG: return "D";
        default:            return "?";
    }
}

extern "C" void sds_platform_log(SdsLogLevel level, const char* format, ...) {
    /* Build message */
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    /* Print to Serial */
    Serial.printf("[%8lu] [%s] %s\n", millis(), log_level_str(level), buffer);
}

