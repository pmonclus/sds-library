/*
 * sds_platform_posix.c - POSIX Platform Implementation
 * 
 * For macOS/Linux using Paho MQTT C library.
 * 
 * Dependencies:
 *   - paho-mqtt3c (Paho MQTT C client, synchronous API)
 *   
 * Install on macOS: brew install eclipse-paho-mqtt-c
 * Install on Ubuntu: apt-get install libpaho-mqtt-dev
 */

#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include <MQTTClient.h>

/* ============== Configuration ============== */

#define MQTT_QOS            0
#define MQTT_TIMEOUT_MS     10000
#define MQTT_KEEPALIVE_SEC  60

/* ============== Internal State ============== */

static MQTTClient _mqtt_client = NULL;
static bool _initialized = false;
static bool _connected = false;
static SdsMqttMessageCallback _message_callback = NULL;
static struct timespec _start_time;

/* ============== MQTT Message Handler ============== */

static int mqtt_message_arrived(void* context, char* topic, int topic_len, MQTTClient_message* message) {
    (void)context;
    (void)topic_len;
    
    if (_message_callback && message->payload && message->payloadlen > 0) {
        _message_callback(topic, (const uint8_t*)message->payload, message->payloadlen);
    }
    
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic);
    return 1;
}

static void mqtt_connection_lost(void* context, char* cause) {
    (void)context;
    SDS_LOG_W("MQTT connection lost: %s", cause ? cause : "unknown");
    _connected = false;
}

/* ============== Platform Init/Shutdown ============== */

bool sds_platform_init(void) {
    if (_initialized) {
        return true;
    }
    
    /* Record start time for millis() */
    clock_gettime(CLOCK_MONOTONIC, &_start_time);
    
    _initialized = true;
    SDS_LOG_I("Platform initialized (POSIX)");
    return true;
}

void sds_platform_shutdown(void) {
    if (!_initialized) {
        return;
    }
    
    if (_mqtt_client) {
        sds_platform_mqtt_disconnect();
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
    }
    
    _initialized = false;
    SDS_LOG_I("Platform shutdown");
}

/* ============== MQTT Operations ============== */

bool sds_platform_mqtt_connect(const char* broker, uint16_t port, const char* client_id) {
    if (!_initialized) {
        SDS_LOG_E("Platform not initialized");
        return false;
    }
    
    if (_mqtt_client) {
        sds_platform_mqtt_disconnect();
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
    }
    
    /* Build connection string: tcp://host:port */
    char address[256];
    snprintf(address, sizeof(address), "tcp://%s:%u", broker, port);
    
    /* Create client */
    int rc = MQTTClient_create(
        &_mqtt_client,
        address,
        client_id,
        MQTTCLIENT_PERSISTENCE_NONE,
        NULL
    );
    
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to create MQTT client: %d", rc);
        return false;
    }
    
    /* Set callbacks */
    rc = MQTTClient_setCallbacks(_mqtt_client, NULL, mqtt_connection_lost, mqtt_message_arrived, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to set MQTT callbacks: %d", rc);
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
        return false;
    }
    
    /* Connect */
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = MQTT_KEEPALIVE_SEC;
    conn_opts.cleansession = 1;
    
    rc = MQTTClient_connect(_mqtt_client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to connect to MQTT broker %s: %d", address, rc);
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
        return false;
    }
    
    _connected = true;
    SDS_LOG_I("Connected to MQTT broker: %s", address);
    return true;
}

bool sds_platform_mqtt_connect_with_lwt(
    const char* broker,
    uint16_t port,
    const char* client_id,
    const char* will_topic,
    const uint8_t* will_payload,
    size_t will_payload_len,
    bool will_retain
) {
    if (!_initialized) {
        SDS_LOG_E("Platform not initialized");
        return false;
    }
    
    if (_mqtt_client) {
        sds_platform_mqtt_disconnect();
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
    }
    
    /* Build connection string: tcp://host:port */
    char address[256];
    snprintf(address, sizeof(address), "tcp://%s:%u", broker, port);
    
    /* Create client */
    int rc = MQTTClient_create(
        &_mqtt_client,
        address,
        client_id,
        MQTTCLIENT_PERSISTENCE_NONE,
        NULL
    );
    
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to create MQTT client: %d", rc);
        return false;
    }
    
    /* Set callbacks */
    rc = MQTTClient_setCallbacks(_mqtt_client, NULL, mqtt_connection_lost, mqtt_message_arrived, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to set MQTT callbacks: %d", rc);
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
        return false;
    }
    
    /* Connect with LWT */
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = MQTT_KEEPALIVE_SEC;
    conn_opts.cleansession = 1;
    
    /* Configure LWT if topic is provided */
    MQTTClient_willOptions will_opts = MQTTClient_willOptions_initializer;
    if (will_topic && will_payload && will_payload_len > 0) {
        will_opts.topicName = will_topic;
        will_opts.message = (const char*)will_payload;
        will_opts.retained = will_retain ? 1 : 0;
        will_opts.qos = 1;  /* Use QoS 1 for LWT to ensure delivery */
        conn_opts.will = &will_opts;
        SDS_LOG_D("LWT configured: topic=%s", will_topic);
    }
    
    rc = MQTTClient_connect(_mqtt_client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to connect to MQTT broker %s: %d", address, rc);
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
        return false;
    }
    
    _connected = true;
    SDS_LOG_I("Connected to MQTT broker: %s (with LWT)", address);
    return true;
}

bool sds_platform_mqtt_connect_with_auth(
    const char* broker,
    uint16_t port,
    const char* client_id,
    const char* username,
    const char* password,
    const char* will_topic,
    const uint8_t* will_payload,
    size_t will_payload_len,
    bool will_retain
) {
    if (!_initialized) {
        SDS_LOG_E("Platform not initialized");
        return false;
    }
    
    if (_mqtt_client) {
        sds_platform_mqtt_disconnect();
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
    }
    
    /* Build connection string: tcp://host:port */
    char address[256];
    snprintf(address, sizeof(address), "tcp://%s:%u", broker, port);
    
    /* Create client */
    int rc = MQTTClient_create(
        &_mqtt_client,
        address,
        client_id,
        MQTTCLIENT_PERSISTENCE_NONE,
        NULL
    );
    
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to create MQTT client: %d", rc);
        return false;
    }
    
    /* Set callbacks */
    rc = MQTTClient_setCallbacks(_mqtt_client, NULL, mqtt_connection_lost, mqtt_message_arrived, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to set MQTT callbacks: %d", rc);
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
        return false;
    }
    
    /* Connect with LWT and authentication */
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = MQTT_KEEPALIVE_SEC;
    conn_opts.cleansession = 1;
    
    /* Configure authentication if provided */
    if (username && username[0] != '\0') {
        conn_opts.username = username;
        conn_opts.password = password;
        SDS_LOG_D("MQTT auth configured for user: %s", username);
    }
    
    /* Configure LWT if topic is provided */
    MQTTClient_willOptions will_opts = MQTTClient_willOptions_initializer;
    if (will_topic && will_payload && will_payload_len > 0) {
        will_opts.topicName = will_topic;
        will_opts.message = (const char*)will_payload;
        will_opts.retained = will_retain ? 1 : 0;
        will_opts.qos = 1;  /* Use QoS 1 for LWT to ensure delivery */
        conn_opts.will = &will_opts;
        SDS_LOG_D("LWT configured: topic=%s", will_topic);
    }
    
    rc = MQTTClient_connect(_mqtt_client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to connect to MQTT broker %s: %d", address, rc);
        MQTTClient_destroy(&_mqtt_client);
        _mqtt_client = NULL;
        return false;
    }
    
    _connected = true;
    SDS_LOG_I("Connected to MQTT broker: %s (with auth + LWT)", address);
    return true;
}

void sds_platform_mqtt_disconnect(void) {
    if (_mqtt_client && _connected) {
        MQTTClient_disconnect(_mqtt_client, MQTT_TIMEOUT_MS);
        _connected = false;
        SDS_LOG_I("Disconnected from MQTT broker");
    }
}

bool sds_platform_mqtt_connected(void) {
    if (!_mqtt_client) {
        return false;
    }
    _connected = MQTTClient_isConnected(_mqtt_client);
    return _connected;
}

bool sds_platform_mqtt_publish(const char* topic, const uint8_t* payload, size_t payload_len, bool retained) {
    if (!_mqtt_client || !_connected) {
        return false;
    }
    
    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload = (void*)payload;
    msg.payloadlen = (int)payload_len;
    msg.qos = MQTT_QOS;
    msg.retained = retained ? 1 : 0;
    
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(_mqtt_client, topic, &msg, &token);
    
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to publish to %s: %d", topic, rc);
        return false;
    }
    
    /* For QoS 0, no need to wait for delivery */
    return true;
}

bool sds_platform_mqtt_subscribe(const char* topic) {
    if (!_mqtt_client || !_connected) {
        return false;
    }
    
    int rc = MQTTClient_subscribe(_mqtt_client, topic, MQTT_QOS);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to subscribe to %s: %d", topic, rc);
        return false;
    }
    
    SDS_LOG_D("Subscribed to: %s", topic);
    return true;
}

bool sds_platform_mqtt_unsubscribe(const char* topic) {
    if (!_mqtt_client || !_connected) {
        return false;
    }
    
    int rc = MQTTClient_unsubscribe(_mqtt_client, topic);
    if (rc != MQTTCLIENT_SUCCESS) {
        SDS_LOG_E("Failed to unsubscribe from %s: %d", topic, rc);
        return false;
    }
    
    SDS_LOG_D("Unsubscribed from: %s", topic);
    return true;
}

void sds_platform_mqtt_loop(void) {
    /* Paho C synchronous client handles this internally via callbacks */
    /* Just yield briefly to allow message processing */
    if (_mqtt_client && _connected) {
        MQTTClient_yield();
    }
}

void sds_platform_mqtt_set_callback(SdsMqttMessageCallback callback) {
    _message_callback = callback;
}

/* ============== Timing ============== */

uint32_t sds_platform_millis(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    uint64_t start_ms = (uint64_t)_start_time.tv_sec * 1000 + _start_time.tv_nsec / 1000000;
    uint64_t now_ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    
    return (uint32_t)(now_ms - start_ms);
}

void sds_platform_delay_ms(uint32_t ms) {
    usleep(ms * 1000);
}

/* ============== Logging ============== */

static const char* log_level_str(SdsLogLevel level) {
    switch (level) {
        case SDS_LOG_ERROR: return "ERROR";
        case SDS_LOG_WARN:  return "WARN";
        case SDS_LOG_INFO:  return "INFO";
        case SDS_LOG_DEBUG: return "DEBUG";
        default:            return "?";
    }
}

void sds_platform_log(SdsLogLevel level, const char* format, ...) {
    /* Get timestamp */
    uint32_t ms = sds_platform_millis();
    
    /* Print prefix */
    fprintf(stderr, "[%8u] [%s] ", ms, log_level_str(level));
    
    /* Print message */
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

