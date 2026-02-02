/**
 * @file sds.h
 * @brief SDS (Simple DDS) Core API Header
 * 
 * Simple DDS (SDS) is a lightweight hub-and-spoke state synchronization library
 * designed for embedded IoT systems. It uses MQTT as the transport layer and
 * provides automatic serialization via schema-driven code generation.
 * 
 * @section usage Basic Usage
 * 
 * @code{.c}
 * #include "sds.h"
 * #include "sds_types.h"  // Generated from your schema.sds
 * 
 * SdsConfig config = {
 *     .node_id = "sensor_01",
 *     .mqtt_broker = "192.168.1.100",
 *     .mqtt_port = 1883
 * };
 * 
 * if (sds_init(&config) == SDS_OK) {
 *     SensorDataTable table = {0};
 *     sds_register_table(&table, "SensorData", SDS_ROLE_DEVICE, NULL);
 *     
 *     while (running) {
 *         sds_loop();
 *         table.state.temperature = read_sensor();
 *     }
 *     
 *     sds_shutdown();
 * }
 * @endcode
 * 
 * @section roles Device vs Owner Roles
 * 
 * - **SDS_ROLE_DEVICE**: Receives config from owner, publishes state and status
 * - **SDS_ROLE_OWNER**: Publishes config, receives state and status from devices
 * 
 * @author SDS Team
 * @version 1.0.0
 */

#ifndef SDS_H
#define SDS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "sds_error.h"
#include "sds_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup config Configuration Constants
 * @brief Compile-time configuration for SDS library.
 * 
 * These constants can be overridden by defining them before including sds.h.
 * @{
 */

/** @brief Maximum number of tables per node */
#define SDS_MAX_TABLES           8

/** @brief Maximum length of node ID string (including null terminator) */
#define SDS_MAX_NODE_ID_LEN      32

/** @brief Maximum length of table type name (including null terminator) */
#define SDS_MAX_TABLE_TYPE_LEN   32

/** @brief Default MQTT broker port */
#define SDS_DEFAULT_MQTT_PORT    1883

/** @brief Default sync interval in milliseconds */
#define SDS_DEFAULT_SYNC_INTERVAL_MS 1000

/** @brief Default liveness/heartbeat interval in milliseconds */
#define SDS_DEFAULT_LIVENESS_INTERVAL_MS 30000

/** @brief Buffer size for MQTT topic strings */
#define SDS_TOPIC_BUFFER_SIZE    128

/** @brief Buffer size for JSON message serialization (supports 1KB sections) */
#ifndef SDS_MSG_BUFFER_SIZE
#define SDS_MSG_BUFFER_SIZE      2048
#endif

/** @} */ // end of config group

/**
 * @defgroup types Core Types
 * @brief Core type definitions for SDS.
 * @{
 */

/**
 * @brief Role of a node for a particular table.
 * 
 * Each table registration specifies a role that determines:
 * - Which sections the node can write to
 * - Which MQTT topics it subscribes to and publishes on
 */
typedef enum {
    SDS_ROLE_OWNER,     /**< Owner: publishes config, receives state/status from devices */
    SDS_ROLE_DEVICE     /**< Device: receives config, publishes state/status */
} SdsRole;

/**
 * @brief Configuration for SDS initialization.
 * 
 * This structure is passed to sds_init() to configure the SDS library.
 * All string fields are copied internally, so they can be stack-allocated.
 * 
 * @note The mqtt_broker field is required; all others have defaults.
 */
typedef struct {
    const char* node_id;        /**< Unique node identifier (NULL = auto-generate) */
    const char* mqtt_broker;    /**< MQTT broker hostname/IP (required) */
    uint16_t mqtt_port;         /**< MQTT port (default: 1883) */
    const char* mqtt_username;  /**< MQTT username (NULL = no authentication) */
    const char* mqtt_password;  /**< MQTT password (NULL = no authentication) */
    uint32_t eviction_grace_ms; /**< Grace period before evicting offline devices (0 = disabled) */
    bool enable_delta_sync;     /**< Enable delta updates - only changed fields (default: false) */
    float delta_float_tolerance; /**< Float comparison tolerance for delta sync (default: 0.001) */
} SdsConfig;

/**
 * @brief Options for table registration.
 * 
 * Optional parameters that can be passed to sds_register_table().
 * Pass NULL to use defaults.
 */
typedef struct {
    uint32_t sync_interval_ms;  /**< Sync check frequency in ms (default: 1000) */
} SdsTableOptions;

/**
 * @brief Runtime statistics.
 * 
 * Use sds_get_stats() to retrieve current statistics.
 * Counters are reset on sds_init() and accumulate until sds_shutdown().
 */
typedef struct {
    uint32_t messages_sent;     /**< Total MQTT messages published */
    uint32_t messages_received; /**< Total MQTT messages received */
    uint32_t reconnect_count;   /**< Number of MQTT reconnection attempts */
    uint32_t errors;            /**< Total error count */
} SdsStats;

/** @} */ // end of types group

/**
 * @defgroup callbacks Callback Types
 * @brief Callback function types for SDS events.
 * @{
 */

/**
 * @brief Callback for config updates (device role only).
 * 
 * Called when a device receives new configuration from the owner.
 * The config data has already been deserialized into the table struct
 * when this callback is invoked.
 * 
 * @param table_type Name of the table that received the update
 * @param user_data User-provided context from sds_on_config_update()
 * 
 * @see sds_on_config_update
 */
typedef void (*SdsConfigCallback)(const char* table_type, void* user_data);

/**
 * @brief Callback for state updates (owner role only).
 * 
 * Called when the owner receives state data from a device.
 * 
 * @param table_type Name of the table that received the update
 * @param from_node Node ID of the device that sent the state
 * @param user_data User-provided context from sds_on_state_update()
 * 
 * @see sds_on_state_update
 */
typedef void (*SdsStateCallback)(const char* table_type, const char* from_node, void* user_data);

/**
 * @brief Callback for status updates (owner role only).
 * 
 * Called when the owner receives status data from a device.
 * Use sds_find_node_status() to retrieve the status data.
 * 
 * @param table_type Name of the table that received the update
 * @param from_node Node ID of the device that sent the status
 * @param user_data User-provided context from sds_on_status_update()
 * 
 * @see sds_on_status_update, sds_find_node_status
 */
typedef void (*SdsStatusCallback)(const char* table_type, const char* from_node, void* user_data);

/**
 * @brief Callback for device eviction (owner role only).
 * 
 * Called when a device is evicted from the status table after the grace period
 * expires following an LWT (device offline) message.
 * 
 * @param table_type The table type (e.g., "SensorData")
 * @param node_id The evicted device's node ID
 * @param user_data User-provided context from sds_on_device_evicted()
 * 
 * @see sds_on_device_evicted
 */
typedef void (*SdsDeviceEvictedCallback)(const char* table_type, const char* node_id, void* user_data);

/**
 * @brief Iterator callback for sds_foreach_node().
 * 
 * Called once for each known device node when iterating.
 * 
 * @param node_id The device's node ID
 * @param status Pointer to the device's status data
 * @param user_data User-provided context from sds_foreach_node()
 * 
 * @see sds_foreach_node
 */
typedef void (*SdsNodeIterator)(const char* node_id, const void* status, void* user_data);

/**
 * @brief Callback for asynchronous error notifications.
 * 
 * Called when an error occurs during sds_loop() or other async operations.
 * 
 * @param error The error code
 * @param context Human-readable context string describing where the error occurred
 * 
 * @see sds_on_error, sds_error_string
 */
typedef void (*SdsErrorCallback)(SdsError error, const char* context);

/**
 * @brief Callback for schema version mismatch detection (owner role only).
 * 
 * Called when an owner receives a message from a device running a different
 * schema version. The callback can decide whether to accept or reject
 * the message.
 * 
 * @param table_type The table type where the mismatch was detected
 * @param device_node_id The node ID of the device with different version
 * @param local_version Our schema version string
 * @param remote_version The device's schema version string
 * @return true to accept the message anyway, false to reject it
 * 
 * @see sds_on_version_mismatch, sds_get_schema_version
 */
typedef bool (*SdsVersionMismatchCallback)(
    const char* table_type,
    const char* device_node_id,
    const char* local_version,
    const char* remote_version
);

/** @} */ // end of callbacks group

/**
 * @defgroup serialization Serialization Types
 * @brief Function types for JSON serialization.
 * 
 * These are typically generated by the code generator from your schema.sds file.
 * @{
 */

/**
 * @brief Function to serialize a table section to JSON.
 * 
 * @param section Pointer to the section struct (config, state, or status)
 * @param w JSON writer to append fields to
 */
typedef void (*SdsSerializeFunc)(void* section, SdsJsonWriter* w);

/**
 * @brief Function to deserialize a table section from JSON.
 * 
 * @param section Pointer to the section struct to populate
 * @param r JSON reader to read fields from
 */
typedef void (*SdsDeserializeFunc)(void* section, SdsJsonReader* r);

/** @} */ // end of serialization group

/**
 * @defgroup registry Table Registry
 * @brief Metadata structures for schema-driven table registration.
 * 
 * The code generator creates an SDS_TABLE_REGISTRY array containing
 * SdsTableMeta entries for each table defined in your schema.sds file.
 * This enables the simple sds_register_table() API.
 * @{
 */

/**
 * @brief Field data types for delta serialization.
 * 
 * Used by SdsFieldMeta to describe field types for per-field
 * change detection and serialization.
 */
typedef enum {
    SDS_FIELD_BOOL = 0,   /**< Boolean (stored as uint8_t) */
    SDS_FIELD_UINT8,      /**< Unsigned 8-bit integer */
    SDS_FIELD_INT8,       /**< Signed 8-bit integer */
    SDS_FIELD_UINT16,     /**< Unsigned 16-bit integer */
    SDS_FIELD_INT16,      /**< Signed 16-bit integer */
    SDS_FIELD_UINT32,     /**< Unsigned 32-bit integer */
    SDS_FIELD_INT32,      /**< Signed 32-bit integer */
    SDS_FIELD_FLOAT,      /**< 32-bit floating point */
    SDS_FIELD_STRING,     /**< Null-terminated string (char array) */
} SdsFieldType;

/**
 * @brief Field descriptor for delta serialization.
 * 
 * Describes a single field within a section (config, state, or status).
 * Used for per-field change detection and selective JSON serialization.
 * 
 * The code generator creates arrays of these descriptors for each section.
 */
typedef struct {
    const char* name;     /**< Field name in JSON */
    SdsFieldType type;    /**< Field data type */
    uint16_t offset;      /**< Offset within section struct */
    uint16_t size;        /**< Size in bytes (for strings: buffer size) */
} SdsFieldMeta;

/**
 * @brief Complete metadata for a table type.
 * 
 * This structure is auto-generated by codegen and contains all information
 * needed to register a table without explicit callbacks. It includes:
 * - Struct sizes and field offsets for both device and owner roles
 * - Serialization/deserialization function pointers
 * - Timing configuration (sync interval, liveness interval)
 * 
 * The SDS_TABLE_REGISTRY array in sds_types.h contains one entry per
 * table type defined in the schema.
 * 
 * @note This struct should not be created manually; it's generated by codegen.
 */
typedef struct {
    const char* table_type;             /**< Table type name (e.g., "SensorData") */
    uint32_t sync_interval_ms;          /**< Default sync check interval in ms */
    uint32_t liveness_interval_ms;      /**< Max time between heartbeats (device role) */
    /* Note: eviction_grace_ms is now global in SdsConfig, not per-table */
    
    /* Struct sizes */
    size_t device_table_size;           /**< sizeof({Table}Table) for device role */
    size_t owner_table_size;            /**< sizeof({Table}OwnerTable) for owner role */
    
    /* Section offsets and sizes for DEVICE role */
    size_t dev_config_offset;           /**< offsetof(DeviceTable, config) */
    size_t dev_config_size;             /**< sizeof(Config) */
    size_t dev_state_offset;            /**< offsetof(DeviceTable, state) */
    size_t dev_state_size;              /**< sizeof(State) */
    size_t dev_status_offset;           /**< offsetof(DeviceTable, status) */
    size_t dev_status_size;             /**< sizeof(Status) */
    
    /* Section offsets and sizes for OWNER role */
    size_t own_config_offset;           /**< offsetof(OwnerTable, config) */
    size_t own_config_size;             /**< sizeof(Config) */
    size_t own_state_offset;            /**< offsetof(OwnerTable, state) */
    size_t own_state_size;              /**< sizeof(State) */
    
    /* Owner status slot management (for per-device tracking) */
    size_t own_status_slots_offset;     /**< offsetof(OwnerTable, status_slots) */
    size_t own_status_slot_size;        /**< sizeof(StatusSlot) */
    size_t own_status_count_offset;     /**< offsetof(OwnerTable, status_count) */
    size_t slot_valid_offset;           /**< offsetof(StatusSlot, valid) */
    size_t slot_online_offset;          /**< offsetof(StatusSlot, online) */
    size_t slot_eviction_pending_offset; /**< offsetof(StatusSlot, eviction_pending) */
    size_t slot_last_seen_offset;       /**< offsetof(StatusSlot, last_seen_ms) */
    size_t slot_eviction_deadline_offset; /**< offsetof(StatusSlot, eviction_deadline) */
    size_t slot_status_offset;          /**< offsetof(StatusSlot, status) */
    uint8_t own_max_status_slots;       /**< Maximum device slots (SDS_GENERATED_MAX_NODES) */
    
    /* Serialization callbacks */
    SdsSerializeFunc serialize_config;   /**< Config section serializer (owner) */
    SdsSerializeFunc serialize_state;    /**< State section serializer (device) */
    SdsSerializeFunc serialize_status;   /**< Status section serializer (device) */
    
    /* Deserialization callbacks */
    SdsDeserializeFunc deserialize_config; /**< Config section deserializer (device) */
    SdsDeserializeFunc deserialize_state;  /**< State section deserializer (owner) */
    SdsDeserializeFunc deserialize_status; /**< Status section deserializer (owner) */
    
    /* Field metadata for delta serialization (optional, NULL if not available) */
    const SdsFieldMeta* config_fields;     /**< Config field descriptors */
    uint8_t config_field_count;            /**< Number of config fields */
    const SdsFieldMeta* state_fields;      /**< State field descriptors */
    uint8_t state_field_count;             /**< Number of state fields */
    const SdsFieldMeta* status_fields;     /**< Status field descriptors */
    uint8_t status_field_count;            /**< Number of status fields */
} SdsTableMeta;

/**
 * Look up table metadata by type name.
 * 
 * This function searches the registered table metadata for a matching entry.
 * 
 * @param table_type Name of table type to find
 * @return Pointer to metadata, or NULL if not found
 */
const SdsTableMeta* sds_find_table_meta(const char* table_type);

/**
 * @brief Set the table registry (called from generated code).
 * 
 * This function is automatically called by the generated code's constructor.
 * You typically don't need to call this manually.
 * 
 * @param registry Array of table metadata
 * @param count Number of entries in the array
 */
void sds_set_table_registry(const SdsTableMeta* registry, size_t count);

/** @} */ // end of registry group

/**
 * @defgroup init Initialization API
 * @brief Functions for initializing and shutting down SDS.
 * @{
 */

/**
 * @brief Initialize SDS and connect to MQTT broker.
 * 
 * This function must be called before any other SDS functions (except
 * sds_set_log_level() which can be called anytime).
 * 
 * @param config Configuration parameters (see SdsConfig)
 * @return SDS_OK on success, error code otherwise
 * 
 * @note Call sds_shutdown() to clean up resources when done.
 * 
 * @see SdsConfig, sds_shutdown, sds_error_string
 */
SdsError sds_init(const SdsConfig* config);

/**
 * @brief Process SDS events.
 * 
 * This function must be called regularly (ideally every loop iteration)
 * to process MQTT messages and sync table changes. It handles:
 * - MQTT message reception and dispatching
 * - Table change detection and publishing
 * - Liveness heartbeats
 * - Automatic reconnection with exponential backoff
 * 
 * @note This function is non-blocking and returns quickly.
 * 
 * @see sds_is_ready
 */
void sds_loop(void);

/**
 * @brief Shutdown SDS and disconnect from MQTT broker.
 * 
 * Publishes a graceful offline message, unsubscribes from all topics,
 * and releases resources. Safe to call even if not initialized.
 * 
 * @see sds_init
 */
void sds_shutdown(void);

/**
 * @brief Check if SDS is initialized and connected.
 * 
 * @return true if SDS is initialized and MQTT is connected
 * 
 * @see sds_init, sds_loop
 */
bool sds_is_ready(void);

/**
 * @brief Get the node ID.
 * 
 * Returns the node ID from config, or the auto-generated ID if none
 * was provided.
 * 
 * @return Node ID string, or NULL if not initialized
 * 
 * @see SdsConfig
 */
const char* sds_get_node_id(void);

/**
 * @brief Get runtime statistics.
 * 
 * Returns a pointer to the internal statistics structure. The pointer
 * remains valid until sds_shutdown() is called.
 * 
 * @return Pointer to stats structure
 * 
 * @see SdsStats
 */
const SdsStats* sds_get_stats(void);

/** @} */ // end of init group

/**
 * @defgroup registration Table Registration API
 * @brief Functions for registering and unregistering tables.
 * @{
 */

/**
 * @brief Register a table using the metadata registry.
 * 
 * This is the simple, user-friendly registration API. It looks up the table
 * type in the generated metadata registry and automatically provides all
 * serialization callbacks.
 * 
 * @code{.c}
 * SensorDataTable sensor_table = {0};
 * SdsError err = sds_register_table(&sensor_table, "SensorData", SDS_ROLE_DEVICE, NULL);
 * if (err != SDS_OK) {
 *     printf("Failed: %s\n", sds_error_string(err));
 * }
 * @endcode
 * 
 * @param table Pointer to table structure (DeviceTable or OwnerTable)
 * @param table_type Name of table type (must match schema.sds)
 * @param role SDS_ROLE_OWNER or SDS_ROLE_DEVICE
 * @param options Optional parameters (NULL for defaults)
 * @return SDS_OK on success, error code otherwise
 * 
 * @see sds_unregister_table, SdsRole, SdsTableOptions
 */
SdsError sds_register_table(
    void* table,
    const char* table_type,
    SdsRole role,
    const SdsTableOptions* options
);

/**
 * Register a table with explicit serialization callbacks.
 * 
 * This is the extended registration that provides automatic serialization.
 * The generated helper functions (sds_register_{table}_device/owner) call this.
 * 
 * @param table Pointer to table structure
 * @param table_type Name of table type (must match schema)
 * @param role SDS_ROLE_OWNER or SDS_ROLE_DEVICE
 * @param options Optional parameters (NULL for defaults)
 * @param config_offset Offset of config section in table struct
 * @param config_size Size of config section
 * @param state_offset Offset of state section in table struct
 * @param state_size Size of state section
 * @param status_offset Offset of status section in table struct
 * @param status_size Size of status section
 * @param serialize_config Function to serialize config (owner only)
 * @param deserialize_config Function to deserialize config (device only)
 * @param serialize_state Function to serialize state
 * @param deserialize_state Function to deserialize state (owner only)
 * @param serialize_status Function to serialize status (device only)
 * @param deserialize_status Function to deserialize status (owner only)
 * @return SDS_OK on success, error code otherwise
 */
SdsError sds_register_table_ex(
    void* table,
    const char* table_type,
    SdsRole role,
    const SdsTableOptions* options,
    size_t config_offset, size_t config_size,
    size_t state_offset, size_t state_size,
    size_t status_offset, size_t status_size,
    SdsSerializeFunc serialize_config,
    SdsDeserializeFunc deserialize_config,
    SdsSerializeFunc serialize_state,
    SdsDeserializeFunc deserialize_state,
    SdsSerializeFunc serialize_status,
    SdsDeserializeFunc deserialize_status
);

/**
 * @brief Unregister a table.
 * 
 * Unsubscribes from related MQTT topics and frees the table slot.
 * The table data structure is not modified and can be reused.
 * 
 * @param table_type Name of table type to unregister
 * @return SDS_OK on success, SDS_ERR_TABLE_NOT_FOUND if not registered
 * 
 * @see sds_register_table
 */
SdsError sds_unregister_table(const char* table_type);

/**
 * @brief Get the number of registered tables.
 * 
 * @return Number of currently active tables (0 to SDS_MAX_TABLES)
 */
uint8_t sds_get_table_count(void);

/** @} */ // end of registration group

/**
 * @defgroup event_callbacks Event Callbacks
 * @brief Functions for registering event callbacks.
 * @{
 */

/**
 * @brief Set callback for config updates (device role only).
 * 
 * Called when a device receives new configuration from the owner.
 * 
 * @param table_type Table type to monitor
 * @param callback Function to call on config change (NULL to disable)
 * @param user_data User context passed to callback (can be NULL)
 * 
 * @see SdsConfigCallback
 */
void sds_on_config_update(const char* table_type, SdsConfigCallback callback, void* user_data);

/**
 * @brief Set callback for state updates (owner role only).
 * 
 * Called when the owner receives state data from a device.
 * 
 * @param table_type Table type to monitor
 * @param callback Function to call on state change (NULL to disable)
 * @param user_data User context passed to callback (can be NULL)
 * 
 * @see SdsStateCallback
 */
void sds_on_state_update(const char* table_type, SdsStateCallback callback, void* user_data);

/**
 * @brief Set callback for status updates (owner role only).
 * 
 * Called when the owner receives status data from a device.
 * 
 * @param table_type Table type to monitor
 * @param callback Function to call on status change (NULL to disable)
 * @param user_data User context passed to callback (can be NULL)
 * 
 * @see SdsStatusCallback, sds_find_node_status
 */
void sds_on_status_update(const char* table_type, SdsStatusCallback callback, void* user_data);

/**
 * @brief Set callback for async error notifications.
 * 
 * Errors that occur during sds_loop() or other async operations
 * will be reported through this callback.
 * 
 * @param callback Function to call on error (NULL to disable)
 * 
 * @see SdsErrorCallback, sds_error_string
 */
void sds_on_error(SdsErrorCallback callback);

/**
 * @brief Set callback for schema version mismatch detection (owner role only).
 * 
 * When an owner receives a message from a device with a different schema
 * version, this callback is invoked. The callback can decide whether to
 * accept or reject the message.
 * 
 * If no callback is registered, mismatches are logged as warnings and
 * messages are accepted (backward compatible behavior).
 * 
 * @param callback Function to call on version mismatch (NULL to use default)
 * 
 * @see SdsVersionMismatchCallback, sds_get_schema_version
 */
void sds_on_version_mismatch(SdsVersionMismatchCallback callback);

/**
 * @brief Get the local schema version string.
 * 
 * This returns the SDS_SCHEMA_VERSION defined in sds_types.h, or "unknown"
 * if no schema is registered.
 * 
 * @return Schema version string
 * 
 * @see sds_set_schema_version
 */
const char* sds_get_schema_version(void);

/**
 * @brief Set the schema version.
 * 
 * This is typically called automatically from generated code. You only
 * need to call this manually if you're not using the code generator.
 * 
 * @param version Schema version string
 */
void sds_set_schema_version(const char* version);

/** @} */ // end of event_callbacks group

/**
 * @defgroup owner_helpers Owner Helper Functions
 * @brief Utility functions for the owner role.
 * @{
 */

/**
 * @brief Find status for a specific device (owner role only).
 * 
 * Looks up the status slot for a device by its node ID.
 * 
 * @code{.c}
 * const SensorDataStatus* status = sds_find_node_status(&owner_table, "SensorData", "sensor_01");
 * if (status) {
 *     printf("Temperature: %.1f\n", status->temperature);
 * }
 * @endcode
 * 
 * @param owner_table Pointer to owner table structure
 * @param table_type Table type name
 * @param node_id Node ID to find
 * @return Pointer to status struct, or NULL if not found
 * 
 * @see sds_foreach_node, sds_is_device_online
 */
const void* sds_find_node_status(
    const void* owner_table,
    const char* table_type,
    const char* node_id
);

/**
 * @brief Iterate over all known device nodes (owner role only).
 * 
 * Calls the callback function once for each device that has sent status.
 * 
 * @code{.c}
 * void print_node(const char* node_id, const void* status, void* user_data) {
 *     printf("Node: %s\n", node_id);
 * }
 * sds_foreach_node(&owner_table, "SensorData", print_node, NULL);
 * @endcode
 * 
 * @param owner_table Pointer to owner table structure
 * @param table_type Table type name
 * @param callback Function to call for each node
 * @param user_data User-provided context passed to callback
 * 
 * @see SdsNodeIterator, sds_find_node_status
 */
void sds_foreach_node(
    const void* owner_table,
    const char* table_type,
    SdsNodeIterator callback,
    void* user_data
);

/**
 * @brief Configure status slot metadata for manual table registration.
 * 
 * When using sds_register_table_ex() directly (not via codegen registry),
 * call this function to enable per-device status tracking for owner tables.
 * 
 * @note This is not needed when using sds_register_table() with generated code.
 * 
 * @param table_type Table type name
 * @param slots_offset offsetof(OwnerTable, status_slots)
 * @param slot_size sizeof(StatusSlot)
 * @param slot_status_offset offsetof(StatusSlot, status)
 * @param count_offset offsetof(OwnerTable, status_count)
 * @param max_slots Maximum number of device slots
 * 
 * @see sds_register_table_ex
 */
void sds_set_owner_status_slots(
    const char* table_type,
    size_t slots_offset,
    size_t slot_size,
    size_t slot_status_offset,
    size_t count_offset,
    uint8_t max_slots
);

/**
 * @brief Configure slot field offsets for online detection (owner role only).
 * 
 * This function sets the offsets of the valid, online, and last_seen_ms fields
 * within each status slot. Required for sds_is_device_online() to work.
 * 
 * @code{.c}
 * // After sds_set_owner_status_slots():
 * sds_set_owner_slot_offsets(
 *     "SensorData",
 *     offsetof(SensorDataStatusSlot, valid),
 *     offsetof(SensorDataStatusSlot, online),
 *     offsetof(SensorDataStatusSlot, last_seen_ms)
 * );
 * @endcode
 * 
 * @param table_type Table type name
 * @param valid_offset offsetof(StatusSlot, valid)
 * @param online_offset offsetof(StatusSlot, online)
 * @param last_seen_offset offsetof(StatusSlot, last_seen_ms)
 * 
 * @see sds_set_owner_status_slots
 * @see sds_is_device_online
 */
void sds_set_owner_slot_offsets(
    const char* table_type,
    size_t valid_offset,
    size_t online_offset,
    size_t last_seen_offset
);

/**
 * @brief Configure eviction slot offsets for an owner table.
 * 
 * This function configures the slot field offsets needed for eviction tracking.
 * The eviction grace period is set globally via SdsConfig.eviction_grace_ms.
 * 
 * @code{.c}
 * // After sds_set_owner_slot_offsets():
 * sds_set_owner_eviction_offsets(
 *     "SensorData",
 *     offsetof(SensorDataStatusSlot, eviction_pending),
 *     offsetof(SensorDataStatusSlot, eviction_deadline)
 * );
 * @endcode
 * 
 * @param table_type Table type name
 * @param eviction_pending_offset offsetof(StatusSlot, eviction_pending)
 * @param eviction_deadline_offset offsetof(StatusSlot, eviction_deadline)
 * 
 * @note The eviction grace period is configured in SdsConfig, not per-table.
 * 
 * @see SdsConfig::eviction_grace_ms
 * @see sds_on_device_evicted
 */
void sds_set_owner_eviction_offsets(
    const char* table_type,
    size_t eviction_pending_offset,
    size_t eviction_deadline_offset
);

/**
 * @brief Check if a device is currently online (owner role only).
 * 
 * A device is considered online if all of the following are true:
 * - We have a valid status slot for it
 * - The "online" flag is true (not cleared by LWT or graceful disconnect)
 * - Last message was received within the specified timeout
 * 
 * @code{.c}
 * uint32_t timeout = sds_get_liveness_interval("SensorData") * 3 / 2;  // 1.5×
 * if (sds_is_device_online(&owner_table, "SensorData", "sensor_01", timeout)) {
 *     printf("Device is online\n");
 * }
 * @endcode
 * 
 * @param owner_table Pointer to owner table structure
 * @param table_type Table type name
 * @param node_id Device node ID to check
 * @param timeout_ms Liveness timeout (typically 1.5x the liveness interval)
 * @return true if device is online
 * 
 * @see sds_get_liveness_interval
 */
bool sds_is_device_online(
    const void* owner_table,
    const char* table_type,
    const char* node_id,
    uint32_t timeout_ms
);

/**
 * @brief Get the liveness interval for a table type.
 * 
 * Returns the liveness interval (from \@liveness in schema) for the given table.
 * This is useful for calculating timeout values for sds_is_device_online().
 * 
 * @param table_type Table type name
 * @return Liveness interval in milliseconds, or 0 if not found
 * 
 * @see sds_is_device_online
 */
uint32_t sds_get_liveness_interval(const char* table_type);

/**
 * @brief Get the global eviction grace period.
 * 
 * Returns the eviction grace period configured in SdsConfig.
 * After an LWT (device offline) message, the device has this long to reconnect
 * before being evicted from all status tables.
 * 
 * @param table_type Ignored (kept for backward compatibility)
 * @return Eviction grace period in milliseconds, or 0 if eviction is disabled
 * 
 * @see SdsConfig::eviction_grace_ms
 */
uint32_t sds_get_eviction_grace(const char* table_type);

/**
 * @brief Set global callback for device eviction notifications.
 * 
 * Called when a device is evicted from any status table after the eviction
 * grace period expires following an LWT message. Since eviction is global
 * (one grace period for all tables), this callback is invoked once per table
 * that the device was registered in.
 * 
 * @code{.c}
 * void on_device_evicted(const char* table_type, const char* node_id, void* user_data) {
 *     printf("Device %s was evicted from %s\n", node_id, table_type);
 * }
 * 
 * // table_type parameter is ignored (global callback)
 * sds_on_device_evicted(NULL, on_device_evicted, NULL);
 * @endcode
 * 
 * @param table_type Ignored (kept for backward compatibility)
 * @param callback Function to call on eviction
 * @param user_data User context passed to callback
 * 
 * @see SdsDeviceEvictedCallback, SdsConfig::eviction_grace_ms
 */
void sds_on_device_evicted(const char* table_type, SdsDeviceEvictedCallback callback, void* user_data);

/** @} */ // end of owner_helpers group

#ifdef __cplusplus
}

/**
 * @defgroup arduino Arduino C++ Wrapper
 * @brief C++ wrapper class for Arduino/ESP sketches.
 * @{
 */

/**
 * @brief Arduino-style wrapper class for SDS.
 * 
 * Provides a simpler, Arduino-friendly interface for ESP32/ESP8266 sketches.
 * 
 * @code{.cpp}
 * SDSClient sds;
 * 
 * void setup() {
 *     if (!sds.begin("sensor_01", "192.168.1.100")) {
 *         Serial.println("SDS init failed");
 *         return;
 *     }
 *     sds_register_table(&sensor_table, "SensorData", SDS_ROLE_DEVICE, NULL);
 * }
 * 
 * void loop() {
 *     sds.loop();
 *     if (sds.isReady()) {
 *         sensor_table.state.temperature = readSensor();
 *     }
 * }
 * @endcode
 */
class SDSClient {
public:
    /**
     * @brief Initialize SDS and connect to MQTT broker.
     * @param node_id Unique identifier for this node
     * @param broker MQTT broker hostname or IP
     * @param port MQTT broker port (default: 1883)
     * @param eviction_grace_ms Grace period before evicting offline devices (0 = disabled)
     * @return true on success
     */
    bool begin(const char* node_id, const char* broker, uint16_t port = 1883, 
               uint32_t eviction_grace_ms = 0) {
        SdsConfig config = {
            .node_id = node_id,
            .mqtt_broker = broker,
            .mqtt_port = port,
            .mqtt_username = nullptr,
            .mqtt_password = nullptr,
            .eviction_grace_ms = eviction_grace_ms
        };
        return sds_init(&config) == SDS_OK;
    }
    
    /**
     * @brief Initialize SDS with authentication.
     * @param node_id Unique identifier for this node
     * @param broker MQTT broker hostname or IP
     * @param port MQTT broker port
     * @param username MQTT username
     * @param password MQTT password
     * @param eviction_grace_ms Grace period before evicting offline devices (0 = disabled)
     * @return true on success
     */
    bool beginWithAuth(const char* node_id, const char* broker, uint16_t port,
                       const char* username, const char* password,
                       uint32_t eviction_grace_ms = 0) {
        SdsConfig config = {
            .node_id = node_id,
            .mqtt_broker = broker,
            .mqtt_port = port,
            .mqtt_username = username,
            .mqtt_password = password,
            .eviction_grace_ms = eviction_grace_ms
        };
        return sds_init(&config) == SDS_OK;
    }
    
    /** @brief Process SDS events. Call in loop(). */
    void loop() { sds_loop(); }
    
    /** @brief Shutdown SDS and disconnect. */
    void end() { sds_shutdown(); }
    
    /** @brief Check if connected and ready. */
    bool isReady() { return sds_is_ready(); }
    
    /** @brief Get the node ID. */
    const char* getNodeId() { return sds_get_node_id(); }
    
    /** @brief Get runtime statistics. */
    const SdsStats* getStats() { return sds_get_stats(); }
    
    /** @brief Convert error code to string. */
    static const char* errorString(SdsError err) {
        return sds_error_string(err);
    }
};

/** @} */ // end of arduino group

#endif /* __cplusplus */

#endif /* SDS_H */
