/*
 * sds_platform_mock.c - Mock Platform Implementation for Unit Testing
 * 
 * This implements all functions from sds_platform.h with mock behavior
 * suitable for unit testing without a real MQTT broker.
 */

#include "sds_platform_mock.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ============== Internal State ============== */

/* Default configuration */
static SdsMockConfig g_config = {
    .init_returns_success = true,
    .mqtt_connect_returns_success = true,
    .mqtt_connected = false,
    .mqtt_publish_returns_success = true,
    .mqtt_subscribe_returns_success = true,
};

/* Time simulation */
static uint32_t g_mock_time_ms = 0;

/* Message callback registered by SDS core */
static SdsMqttMessageCallback g_message_callback = NULL;

/* Published messages ring buffer */
static SdsMockPublishedMessage g_published[SDS_MOCK_MAX_PUBLISHED];
static size_t g_publish_count = 0;
static size_t g_publish_write_index = 0;

/* Subscription tracking */
static char g_subscriptions[SDS_MOCK_MAX_SUBSCRIPTIONS][SDS_MOCK_MAX_TOPIC_LEN];
static size_t g_subscription_count = 0;
static size_t g_subscribe_call_count = 0;
static size_t g_unsubscribe_call_count = 0;

/* Connection tracking */
static size_t g_connect_count = 0;
static char g_last_client_id[64] = "";
static char g_last_broker[128] = "";
static uint16_t g_last_port = 0;

/* Log capture */
static SdsMockLogEntry g_logs[SDS_MOCK_MAX_LOG_ENTRIES];
static size_t g_log_count = 0;
static size_t g_log_write_index = 0;

/* ============== Mock Control Functions ============== */

void sds_mock_reset(void) {
    /* Reset configuration to defaults */
    g_config.init_returns_success = true;
    g_config.mqtt_connect_returns_success = true;
    g_config.mqtt_connected = false;
    g_config.mqtt_publish_returns_success = true;
    g_config.mqtt_subscribe_returns_success = true;
    
    /* Reset time */
    g_mock_time_ms = 0;
    
    /* Reset callback */
    g_message_callback = NULL;
    
    /* Reset published messages */
    memset(g_published, 0, sizeof(g_published));
    g_publish_count = 0;
    g_publish_write_index = 0;
    
    /* Reset subscriptions */
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_subscription_count = 0;
    g_subscribe_call_count = 0;
    g_unsubscribe_call_count = 0;
    
    /* Reset connection tracking */
    g_connect_count = 0;
    g_last_client_id[0] = '\0';
    g_last_broker[0] = '\0';
    g_last_port = 0;
    
    /* Reset logs */
    memset(g_logs, 0, sizeof(g_logs));
    g_log_count = 0;
    g_log_write_index = 0;
}

void sds_mock_configure(const SdsMockConfig* config) {
    if (config) {
        g_config = *config;
    }
}

const SdsMockConfig* sds_mock_get_config(void) {
    return &g_config;
}

/* ============== Time Control ============== */

void sds_mock_set_time(uint32_t time_ms) {
    g_mock_time_ms = time_ms;
}

void sds_mock_advance_time(uint32_t delta_ms) {
    g_mock_time_ms += delta_ms;
}

uint32_t sds_mock_get_time(void) {
    return g_mock_time_ms;
}

/* ============== Message Injection ============== */

void sds_mock_inject_message(const char* topic, const uint8_t* payload, size_t payload_len) {
    if (g_message_callback && topic && payload) {
        g_message_callback(topic, payload, payload_len);
    }
}

void sds_mock_inject_message_str(const char* topic, const char* payload_str) {
    if (payload_str) {
        sds_mock_inject_message(topic, (const uint8_t*)payload_str, strlen(payload_str));
    }
}

/* ============== Publish Capture ============== */

const SdsMockPublishedMessage* sds_mock_get_last_publish(void) {
    if (g_publish_count == 0) {
        return NULL;
    }
    
    size_t last_index = (g_publish_write_index + SDS_MOCK_MAX_PUBLISHED - 1) % SDS_MOCK_MAX_PUBLISHED;
    return &g_published[last_index];
}

size_t sds_mock_get_publish_count(void) {
    return g_publish_count;
}

const SdsMockPublishedMessage* sds_mock_get_publish(size_t index) {
    if (index >= g_publish_count) {
        return NULL;
    }
    
    /* Calculate actual array index */
    size_t actual_count = g_publish_count < SDS_MOCK_MAX_PUBLISHED ? g_publish_count : SDS_MOCK_MAX_PUBLISHED;
    if (index >= actual_count) {
        return NULL;
    }
    
    /* Handle ring buffer wraparound */
    size_t start_index;
    if (g_publish_count <= SDS_MOCK_MAX_PUBLISHED) {
        start_index = 0;
    } else {
        start_index = g_publish_write_index;  /* Oldest is at write index (about to be overwritten) */
    }
    
    size_t actual_index = (start_index + index) % SDS_MOCK_MAX_PUBLISHED;
    return &g_published[actual_index];
}

void sds_mock_clear_publishes(void) {
    memset(g_published, 0, sizeof(g_published));
    g_publish_count = 0;
    g_publish_write_index = 0;
}

const SdsMockPublishedMessage* sds_mock_find_publish_by_topic(const char* topic_pattern) {
    if (!topic_pattern) {
        return NULL;
    }
    
    /* Search from newest to oldest */
    for (size_t i = 0; i < g_publish_count && i < SDS_MOCK_MAX_PUBLISHED; i++) {
        size_t index = (g_publish_write_index + SDS_MOCK_MAX_PUBLISHED - 1 - i) % SDS_MOCK_MAX_PUBLISHED;
        if (strcmp(g_published[index].topic, topic_pattern) == 0) {
            return &g_published[index];
        }
    }
    
    return NULL;
}

/* ============== Subscription Tracking ============== */

bool sds_mock_is_subscribed(const char* topic) {
    if (!topic) {
        return false;
    }
    
    for (size_t i = 0; i < g_subscription_count; i++) {
        if (strcmp(g_subscriptions[i], topic) == 0) {
            return true;
        }
    }
    
    return false;
}

size_t sds_mock_get_subscription_count(void) {
    return g_subscription_count;
}

const char* sds_mock_get_subscription(size_t index) {
    if (index >= g_subscription_count) {
        return NULL;
    }
    return g_subscriptions[index];
}

size_t sds_mock_get_subscribe_call_count(void) {
    return g_subscribe_call_count;
}

size_t sds_mock_get_unsubscribe_call_count(void) {
    return g_unsubscribe_call_count;
}

/* ============== Connection Tracking ============== */

size_t sds_mock_get_connect_count(void) {
    return g_connect_count;
}

const char* sds_mock_get_last_client_id(void) {
    return g_last_client_id[0] ? g_last_client_id : NULL;
}

const char* sds_mock_get_last_broker(void) {
    return g_last_broker[0] ? g_last_broker : NULL;
}

uint16_t sds_mock_get_last_port(void) {
    return g_last_port;
}

void sds_mock_simulate_disconnect(void) {
    g_config.mqtt_connected = false;
}

void sds_mock_simulate_reconnect(void) {
    if (g_config.mqtt_connect_returns_success) {
        g_config.mqtt_connected = true;
    }
}

/* ============== Log Capture ============== */

size_t sds_mock_get_log_count(void) {
    return g_log_count;
}

const SdsMockLogEntry* sds_mock_get_log(size_t index) {
    if (index >= g_log_count || index >= SDS_MOCK_MAX_LOG_ENTRIES) {
        return NULL;
    }
    
    /* Handle ring buffer wraparound */
    size_t start_index;
    if (g_log_count <= SDS_MOCK_MAX_LOG_ENTRIES) {
        start_index = 0;
    } else {
        start_index = g_log_write_index;
    }
    
    size_t actual_index = (start_index + index) % SDS_MOCK_MAX_LOG_ENTRIES;
    return &g_logs[actual_index];
}

bool sds_mock_log_contains(const char* substring) {
    if (!substring) {
        return false;
    }
    
    size_t count = g_log_count < SDS_MOCK_MAX_LOG_ENTRIES ? g_log_count : SDS_MOCK_MAX_LOG_ENTRIES;
    for (size_t i = 0; i < count; i++) {
        if (strstr(g_logs[i].message, substring) != NULL) {
            return true;
        }
    }
    
    return false;
}

bool sds_mock_has_errors(void) {
    size_t count = g_log_count < SDS_MOCK_MAX_LOG_ENTRIES ? g_log_count : SDS_MOCK_MAX_LOG_ENTRIES;
    for (size_t i = 0; i < count; i++) {
        if (g_logs[i].level == SDS_LOG_ERROR) {
            return true;
        }
    }
    return false;
}

void sds_mock_clear_logs(void) {
    memset(g_logs, 0, sizeof(g_logs));
    g_log_count = 0;
    g_log_write_index = 0;
}

/* ============== Platform Interface Implementation ============== */

bool sds_platform_init(void) {
    return g_config.init_returns_success;
}

void sds_platform_shutdown(void) {
    g_config.mqtt_connected = false;
}

bool sds_platform_mqtt_connect(const char* broker, uint16_t port, const char* client_id) {
    g_connect_count++;
    
    if (broker) {
        strncpy(g_last_broker, broker, sizeof(g_last_broker) - 1);
        g_last_broker[sizeof(g_last_broker) - 1] = '\0';
    }
    
    g_last_port = port;
    
    if (client_id) {
        strncpy(g_last_client_id, client_id, sizeof(g_last_client_id) - 1);
        g_last_client_id[sizeof(g_last_client_id) - 1] = '\0';
    }
    
    if (g_config.mqtt_connect_returns_success) {
        g_config.mqtt_connected = true;
        return true;
    }
    
    return false;
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
    /* Ignore LWT parameters for mock - just do basic connect */
    (void)will_topic;
    (void)will_payload;
    (void)will_payload_len;
    (void)will_retain;
    
    return sds_platform_mqtt_connect(broker, port, client_id);
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
    /* Ignore auth and LWT parameters for mock */
    (void)username;
    (void)password;
    (void)will_topic;
    (void)will_payload;
    (void)will_payload_len;
    (void)will_retain;
    
    return sds_platform_mqtt_connect(broker, port, client_id);
}

void sds_platform_mqtt_disconnect(void) {
    g_config.mqtt_connected = false;
}

bool sds_platform_mqtt_connected(void) {
    return g_config.mqtt_connected;
}

bool sds_platform_mqtt_publish(
    const char* topic,
    const uint8_t* payload,
    size_t payload_len,
    bool retained
) {
    if (!g_config.mqtt_publish_returns_success) {
        return false;
    }
    
    if (!g_config.mqtt_connected) {
        return false;
    }
    
    /* Capture the published message */
    SdsMockPublishedMessage* msg = &g_published[g_publish_write_index];
    
    if (topic) {
        strncpy(msg->topic, topic, SDS_MOCK_MAX_TOPIC_LEN - 1);
        msg->topic[SDS_MOCK_MAX_TOPIC_LEN - 1] = '\0';
    } else {
        msg->topic[0] = '\0';
    }
    
    if (payload && payload_len > 0) {
        size_t copy_len = payload_len < SDS_MOCK_MAX_PAYLOAD_LEN ? payload_len : SDS_MOCK_MAX_PAYLOAD_LEN;
        memcpy(msg->payload, payload, copy_len);
        msg->payload_len = copy_len;
    } else {
        msg->payload_len = 0;
    }
    
    msg->retained = retained;
    msg->timestamp_ms = g_mock_time_ms;
    
    g_publish_write_index = (g_publish_write_index + 1) % SDS_MOCK_MAX_PUBLISHED;
    g_publish_count++;
    
    return true;
}

bool sds_platform_mqtt_subscribe(const char* topic) {
    g_subscribe_call_count++;
    
    if (!g_config.mqtt_subscribe_returns_success) {
        return false;
    }
    
    if (!g_config.mqtt_connected) {
        return false;
    }
    
    if (!topic) {
        return false;
    }
    
    /* Check if already subscribed */
    for (size_t i = 0; i < g_subscription_count; i++) {
        if (strcmp(g_subscriptions[i], topic) == 0) {
            return true;  /* Already subscribed, success */
        }
    }
    
    /* Add new subscription */
    if (g_subscription_count < SDS_MOCK_MAX_SUBSCRIPTIONS) {
        strncpy(g_subscriptions[g_subscription_count], topic, SDS_MOCK_MAX_TOPIC_LEN - 1);
        g_subscriptions[g_subscription_count][SDS_MOCK_MAX_TOPIC_LEN - 1] = '\0';
        g_subscription_count++;
        return true;
    }
    
    return false;  /* No room for more subscriptions */
}

bool sds_platform_mqtt_unsubscribe(const char* topic) {
    g_unsubscribe_call_count++;
    
    if (!g_config.mqtt_connected) {
        return false;
    }
    
    if (!topic) {
        return false;
    }
    
    /* Find and remove subscription */
    for (size_t i = 0; i < g_subscription_count; i++) {
        if (strcmp(g_subscriptions[i], topic) == 0) {
            /* Shift remaining subscriptions */
            for (size_t j = i; j < g_subscription_count - 1; j++) {
                strcpy(g_subscriptions[j], g_subscriptions[j + 1]);
            }
            g_subscription_count--;
            return true;
        }
    }
    
    return true;  /* Not subscribed, but that's OK */
}

void sds_platform_mqtt_loop(void) {
    /* Mock does nothing here - messages are injected manually */
}

void sds_platform_mqtt_set_callback(SdsMqttMessageCallback callback) {
    g_message_callback = callback;
}

uint32_t sds_platform_millis(void) {
    return g_mock_time_ms;
}

void sds_platform_delay_ms(uint32_t ms) {
    /* In mock, delay just advances time */
    g_mock_time_ms += ms;
}

void sds_platform_log(SdsLogLevel level, const char* format, ...) {
    /* Capture the log message */
    SdsMockLogEntry* entry = &g_logs[g_log_write_index];
    
    entry->level = level;
    entry->timestamp_ms = g_mock_time_ms;
    
    va_list args;
    va_start(args, format);
    vsnprintf(entry->message, SDS_MOCK_MAX_LOG_MSG_LEN, format, args);
    va_end(args);
    
    g_log_write_index = (g_log_write_index + 1) % SDS_MOCK_MAX_LOG_ENTRIES;
    g_log_count++;
    
    /* Optionally print to console for debugging */
#ifdef SDS_MOCK_PRINT_LOGS
    const char* level_str;
    switch (level) {
        case SDS_LOG_ERROR: level_str = "E"; break;
        case SDS_LOG_WARN:  level_str = "W"; break;
        case SDS_LOG_INFO:  level_str = "I"; break;
        case SDS_LOG_DEBUG: level_str = "D"; break;
        default:            level_str = "?"; break;
    }
    printf("[MOCK %s] %s\n", level_str, entry->message);
#endif
}
