/*
 * sds_platform_mock.h - Mock Platform for Unit Testing
 * 
 * Provides a mock implementation of sds_platform.h that allows:
 *   - Deterministic time control
 *   - Message injection (simulating MQTT receives)
 *   - Publish capture (verifying outgoing messages)
 *   - Subscription tracking
 *   - Configurable failure injection
 * 
 * Usage:
 *   1. Link test with sds_platform_mock.c instead of real platform
 *   2. Call sds_mock_reset() before each test
 *   3. Use sds_mock_inject_message() to simulate incoming messages
 *   4. Use sds_mock_get_last_publish() to verify outgoing messages
 */

#ifndef SDS_PLATFORM_MOCK_H
#define SDS_PLATFORM_MOCK_H

#include "sds_platform.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Mock Configuration ============== */

/**
 * Configuration for mock behavior.
 * Set these before running tests to control failure injection.
 */
typedef struct {
    bool init_returns_success;          /* sds_platform_init() return value */
    bool mqtt_connect_returns_success;  /* sds_platform_mqtt_connect() return value */
    bool mqtt_connected;                /* sds_platform_mqtt_connected() return value */
    bool mqtt_publish_returns_success;  /* sds_platform_mqtt_publish() return value */
    bool mqtt_subscribe_returns_success;/* sds_platform_mqtt_subscribe() return value */
} SdsMockConfig;

/**
 * Reset all mock state to defaults.
 * Call this at the start of each test.
 */
void sds_mock_reset(void);

/**
 * Configure mock behavior.
 * 
 * @param config Configuration to apply (NULL = use defaults)
 */
void sds_mock_configure(const SdsMockConfig* config);

/**
 * Get current mock configuration.
 * 
 * @return Pointer to current configuration
 */
const SdsMockConfig* sds_mock_get_config(void);

/* ============== Time Control ============== */

/**
 * Set the mock time to a specific value.
 * 
 * @param time_ms Time in milliseconds
 */
void sds_mock_set_time(uint32_t time_ms);

/**
 * Advance mock time by a delta.
 * 
 * @param delta_ms Milliseconds to advance
 */
void sds_mock_advance_time(uint32_t delta_ms);

/**
 * Get current mock time.
 * 
 * @return Current mock time in milliseconds
 */
uint32_t sds_mock_get_time(void);

/* ============== Message Injection ============== */

/**
 * Inject an MQTT message as if received from broker.
 * This will invoke the callback registered via sds_platform_mqtt_set_callback().
 * 
 * @param topic Topic string
 * @param payload Message payload
 * @param payload_len Length of payload
 */
void sds_mock_inject_message(const char* topic, const uint8_t* payload, size_t payload_len);

/**
 * Inject an MQTT message from a string payload.
 * Convenience wrapper that calculates length via strlen.
 * 
 * @param topic Topic string
 * @param payload_str Null-terminated payload string
 */
void sds_mock_inject_message_str(const char* topic, const char* payload_str);

/* ============== Publish Capture ============== */

/**
 * Maximum captured published messages.
 */
#define SDS_MOCK_MAX_PUBLISHED 64

/**
 * Maximum topic length for captured messages.
 */
#define SDS_MOCK_MAX_TOPIC_LEN 128

/**
 * Maximum payload length for captured messages.
 */
#define SDS_MOCK_MAX_PAYLOAD_LEN 1024

/**
 * Captured published message.
 */
typedef struct {
    char topic[SDS_MOCK_MAX_TOPIC_LEN];
    uint8_t payload[SDS_MOCK_MAX_PAYLOAD_LEN];
    size_t payload_len;
    bool retained;
    uint32_t timestamp_ms;  /* Mock time when published */
} SdsMockPublishedMessage;

/**
 * Get the last published message.
 * 
 * @return Pointer to last published message, or NULL if none
 */
const SdsMockPublishedMessage* sds_mock_get_last_publish(void);

/**
 * Get total number of publish calls.
 * 
 * @return Number of publish calls since reset
 */
size_t sds_mock_get_publish_count(void);

/**
 * Get a specific published message by index.
 * Index 0 is the oldest, index (count-1) is the newest.
 * 
 * @param index Message index (0-based)
 * @return Pointer to message, or NULL if index out of range
 */
const SdsMockPublishedMessage* sds_mock_get_publish(size_t index);

/**
 * Clear all captured published messages.
 */
void sds_mock_clear_publishes(void);

/**
 * Find a published message by topic (most recent match).
 * 
 * @param topic_pattern Topic to search for (exact match)
 * @return Pointer to message, or NULL if not found
 */
const SdsMockPublishedMessage* sds_mock_find_publish_by_topic(const char* topic_pattern);

/* ============== Subscription Tracking ============== */

/**
 * Maximum tracked subscriptions.
 */
#define SDS_MOCK_MAX_SUBSCRIPTIONS 32

/**
 * Check if a topic is currently subscribed.
 * 
 * @param topic Topic to check (exact match)
 * @return true if subscribed
 */
bool sds_mock_is_subscribed(const char* topic);

/**
 * Get total number of subscriptions.
 * 
 * @return Number of active subscriptions
 */
size_t sds_mock_get_subscription_count(void);

/**
 * Get a subscription by index.
 * 
 * @param index Subscription index (0-based)
 * @return Subscription topic string, or NULL if index out of range
 */
const char* sds_mock_get_subscription(size_t index);

/**
 * Get total number of subscribe calls (including duplicates).
 * 
 * @return Total subscribe() calls since reset
 */
size_t sds_mock_get_subscribe_call_count(void);

/**
 * Get total number of unsubscribe calls.
 * 
 * @return Total unsubscribe() calls since reset
 */
size_t sds_mock_get_unsubscribe_call_count(void);

/* ============== Connection Tracking ============== */

/**
 * Get total number of connect attempts.
 * 
 * @return Number of mqtt_connect() calls since reset
 */
size_t sds_mock_get_connect_count(void);

/**
 * Get the client ID from the last connect call.
 * 
 * @return Client ID string, or NULL if never connected
 */
const char* sds_mock_get_last_client_id(void);

/**
 * Get the broker from the last connect call.
 * 
 * @return Broker string, or NULL if never connected
 */
const char* sds_mock_get_last_broker(void);

/**
 * Get the port from the last connect call.
 * 
 * @return Port number
 */
uint16_t sds_mock_get_last_port(void);

/**
 * Simulate a disconnect event.
 * Sets mqtt_connected to false.
 */
void sds_mock_simulate_disconnect(void);

/**
 * Simulate a reconnect event.
 * Sets mqtt_connected to true (if connect_returns_success is true).
 */
void sds_mock_simulate_reconnect(void);

/* ============== Logging Capture ============== */

/**
 * Maximum captured log entries.
 */
#define SDS_MOCK_MAX_LOG_ENTRIES 128

/**
 * Maximum log message length.
 */
#define SDS_MOCK_MAX_LOG_MSG_LEN 256

/**
 * Captured log entry.
 */
typedef struct {
    SdsLogLevel level;
    char message[SDS_MOCK_MAX_LOG_MSG_LEN];
    uint32_t timestamp_ms;
} SdsMockLogEntry;

/**
 * Get total number of log entries.
 * 
 * @return Number of log entries since reset
 */
size_t sds_mock_get_log_count(void);

/**
 * Get a log entry by index.
 * 
 * @param index Log index (0-based, 0 = oldest)
 * @return Pointer to log entry, or NULL if index out of range
 */
const SdsMockLogEntry* sds_mock_get_log(size_t index);

/**
 * Check if any log entry contains a substring.
 * 
 * @param substring String to search for
 * @return true if any log message contains substring
 */
bool sds_mock_log_contains(const char* substring);

/**
 * Check if any error-level log was recorded.
 * 
 * @return true if at least one ERROR log exists
 */
bool sds_mock_has_errors(void);

/**
 * Clear all captured logs.
 */
void sds_mock_clear_logs(void);

#ifdef __cplusplus
}
#endif

#endif /* SDS_PLATFORM_MOCK_H */
