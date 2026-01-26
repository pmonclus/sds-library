/*
 * sds_platform.h - Platform Abstraction Layer
 * 
 * Defines the interface that platform-specific code must implement.
 * Compile-time selection via build system (CMake or PlatformIO).
 * 
 * Implementations:
 *   - platform/posix/sds_platform_posix.c (macOS/Linux with Paho MQTT C)
 *   - platform/esp32/sds_platform_esp32.c (ESP32 with PubSubClient)
 */

#ifndef SDS_PLATFORM_H
#define SDS_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== MQTT Callback Type ============== */

/**
 * Callback invoked when an MQTT message is received.
 * 
 * @param topic The topic the message arrived on
 * @param payload Message payload (not null-terminated)
 * @param payload_len Length of payload in bytes
 */
typedef void (*SdsMqttMessageCallback)(
    const char* topic,
    const uint8_t* payload,
    size_t payload_len
);

/* ============== Platform Interface ============== */

/**
 * Initialize the platform layer.
 * Called once from sds_init().
 * 
 * @return true on success
 */
bool sds_platform_init(void);

/**
 * Shutdown the platform layer.
 * Called from sds_shutdown().
 */
void sds_platform_shutdown(void);

/* ============== MQTT Operations ============== */

/**
 * Connect to MQTT broker.
 * 
 * @param broker Hostname or IP address
 * @param port Port number
 * @param client_id Unique client identifier
 * @return true on success
 */
bool sds_platform_mqtt_connect(
    const char* broker,
    uint16_t port,
    const char* client_id
);

/**
 * Connect to MQTT broker with Last Will and Testament (LWT).
 * 
 * If the device disconnects unexpectedly, the broker will publish
 * the will message to the will topic.
 * 
 * @param broker Hostname or IP address
 * @param port Port number
 * @param client_id Unique client identifier
 * @param will_topic Topic for LWT message (NULL to disable LWT)
 * @param will_payload LWT message payload
 * @param will_payload_len Length of will payload
 * @param will_retain Whether LWT should be retained
 * @return true on success
 */
bool sds_platform_mqtt_connect_with_lwt(
    const char* broker,
    uint16_t port,
    const char* client_id,
    const char* will_topic,
    const uint8_t* will_payload,
    size_t will_payload_len,
    bool will_retain
);

/**
 * Disconnect from MQTT broker.
 */
void sds_platform_mqtt_disconnect(void);

/**
 * Check if connected to MQTT broker.
 * 
 * @return true if connected
 */
bool sds_platform_mqtt_connected(void);

/**
 * Publish a message to a topic.
 * 
 * @param topic Topic to publish to
 * @param payload Message payload
 * @param payload_len Length of payload
 * @param retained Whether message should be retained by broker
 * @return true on success
 */
bool sds_platform_mqtt_publish(
    const char* topic,
    const uint8_t* payload,
    size_t payload_len,
    bool retained
);

/**
 * Subscribe to a topic.
 * 
 * @param topic Topic pattern (may include wildcards + and #)
 * @return true on success
 */
bool sds_platform_mqtt_subscribe(const char* topic);

/**
 * Unsubscribe from a topic.
 * 
 * @param topic Topic to unsubscribe from
 * @return true on success
 */
bool sds_platform_mqtt_unsubscribe(const char* topic);

/**
 * Process MQTT events.
 * Must be called regularly (from sds_loop).
 */
void sds_platform_mqtt_loop(void);

/**
 * Set the callback for incoming messages.
 * Called once during initialization.
 * 
 * @param callback Function to call when message arrives
 */
void sds_platform_mqtt_set_callback(SdsMqttMessageCallback callback);

/* ============== Timing ============== */

/**
 * Get milliseconds since startup.
 * 
 * @return Milliseconds (wraps at ~49 days)
 */
uint32_t sds_platform_millis(void);

/**
 * Delay for specified milliseconds.
 * 
 * @param ms Milliseconds to delay
 */
void sds_platform_delay_ms(uint32_t ms);

/* ============== Logging ============== */

/**
 * Log levels for platform logging.
 */
typedef enum {
    SDS_LOG_ERROR = 0,
    SDS_LOG_WARN,
    SDS_LOG_INFO,
    SDS_LOG_DEBUG
} SdsLogLevel;

/**
 * Log a message.
 * 
 * @param level Log level
 * @param format printf-style format string
 * @param ... Format arguments
 */
void sds_platform_log(SdsLogLevel level, const char* format, ...);

/* ============== Convenience Macros ============== */

#define SDS_LOG_E(fmt, ...) sds_platform_log(SDS_LOG_ERROR, fmt, ##__VA_ARGS__)
#define SDS_LOG_W(fmt, ...) sds_platform_log(SDS_LOG_WARN, fmt, ##__VA_ARGS__)
#define SDS_LOG_I(fmt, ...) sds_platform_log(SDS_LOG_INFO, fmt, ##__VA_ARGS__)
#define SDS_LOG_D(fmt, ...) sds_platform_log(SDS_LOG_DEBUG, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* SDS_PLATFORM_H */

