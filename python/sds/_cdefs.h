/*
 * _cdefs.h - C declarations for CFFI Python bindings
 * 
 * This file contains the C function declarations that CFFI uses to generate
 * Python bindings for the SDS library.
 * 
 * Note: This is a simplified version of the headers, containing only the
 * declarations needed for the Python wrapper (no preprocessor macros).
 */

/* ============== Error Codes ============== */

typedef enum {
    SDS_OK = 0,
    
    /* Initialization errors */
    SDS_ERR_NOT_INITIALIZED,
    SDS_ERR_ALREADY_INITIALIZED,
    SDS_ERR_INVALID_CONFIG,
    
    /* Connection errors */
    SDS_ERR_MQTT_CONNECT_FAILED,
    SDS_ERR_MQTT_DISCONNECTED,
    
    /* Table errors */
    SDS_ERR_TABLE_NOT_FOUND,
    SDS_ERR_TABLE_ALREADY_REGISTERED,
    SDS_ERR_MAX_TABLES_REACHED,
    SDS_ERR_INVALID_TABLE,
    
    /* Role errors */
    SDS_ERR_INVALID_ROLE,
    SDS_ERR_OWNER_EXISTS,
    
    /* Capacity errors */
    SDS_ERR_MAX_NODES_REACHED,
    SDS_ERR_BUFFER_FULL,
    SDS_ERR_SECTION_TOO_LARGE,
    
    /* Platform errors */
    SDS_ERR_PLATFORM_NOT_SET,
    SDS_ERR_PLATFORM_ERROR,
} SdsError;

const char* sds_error_string(SdsError error);

/* ============== Role ============== */

typedef enum {
    SDS_ROLE_OWNER,
    SDS_ROLE_DEVICE
} SdsRole;

/* ============== Log Level ============== */

typedef enum {
    SDS_LOG_NONE = 0,
    SDS_LOG_ERROR = 1,
    SDS_LOG_WARN = 2,
    SDS_LOG_INFO = 3,
    SDS_LOG_DEBUG = 4
} SdsLogLevel;

void sds_set_log_level(SdsLogLevel level);
SdsLogLevel sds_get_log_level(void);

/* ============== Configuration ============== */

typedef struct {
    const char* node_id;
    const char* mqtt_broker;
    uint16_t mqtt_port;
    const char* mqtt_username;
    const char* mqtt_password;
    uint32_t eviction_grace_ms;
    bool enable_delta_sync;
    float delta_float_tolerance;
} SdsConfig;

typedef struct {
    uint32_t sync_interval_ms;
} SdsTableOptions;

/* ============== Statistics ============== */

typedef struct {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t reconnect_count;
    uint32_t errors;
} SdsStats;

/* ============== Callback Types ============== */

/* Note: We use "extern Python" callbacks for CFFI */
typedef void (*SdsConfigCallback)(const char* table_type, void* user_data);
typedef void (*SdsStateCallback)(const char* table_type, const char* from_node, void* user_data);
typedef void (*SdsStatusCallback)(const char* table_type, const char* from_node, void* user_data);
typedef void (*SdsErrorCallback)(SdsError error, const char* context);
typedef void (*SdsNodeIterator)(const char* node_id, const void* status, void* user_data);
typedef bool (*SdsVersionMismatchCallback)(
    const char* table_type,
    const char* device_node_id,
    const char* local_version,
    const char* remote_version
);
typedef void (*SdsDeviceEvictedCallback)(const char* table_type, const char* node_id, void* user_data);

/* ============== JSON Writer/Reader (opaque for Python) ============== */

typedef struct {
    char* buffer;
    size_t size;
    size_t pos;
    bool error;
} SdsJsonWriter;

typedef struct {
    const char* json;
    size_t len;
    size_t pos;
} SdsJsonReader;

/* Serialization function pointers (used internally) */
typedef void (*SdsSerializeFunc)(void* section, SdsJsonWriter* w);
typedef void (*SdsDeserializeFunc)(void* section, SdsJsonReader* r);

/* ============== Field Metadata (for delta sync) ============== */

typedef enum {
    SDS_FIELD_BOOL,
    SDS_FIELD_UINT8,
    SDS_FIELD_INT8,
    SDS_FIELD_UINT16,
    SDS_FIELD_INT16,
    SDS_FIELD_UINT32,
    SDS_FIELD_INT32,
    SDS_FIELD_FLOAT,
    SDS_FIELD_STRING,
} SdsFieldType;

typedef struct {
    const char* name;
    SdsFieldType type;
    uint16_t offset;
    uint16_t size;
} SdsFieldMeta;

/* ============== Table Metadata ============== */

typedef struct {
    const char* table_type;
    uint32_t sync_interval_ms;
    uint32_t liveness_interval_ms;
    
    size_t device_table_size;
    size_t owner_table_size;
    
    size_t dev_config_offset;
    size_t dev_config_size;
    size_t dev_state_offset;
    size_t dev_state_size;
    size_t dev_status_offset;
    size_t dev_status_size;
    
    size_t own_config_offset;
    size_t own_config_size;
    size_t own_state_offset;
    size_t own_state_size;
    
    size_t own_status_slots_offset;
    size_t own_status_slot_size;
    size_t own_status_count_offset;
    size_t slot_valid_offset;
    size_t slot_online_offset;
    size_t slot_eviction_pending_offset;
    size_t slot_last_seen_offset;
    size_t slot_eviction_deadline_offset;
    size_t slot_status_offset;
    uint8_t own_max_status_slots;
    
    SdsSerializeFunc serialize_config;
    SdsSerializeFunc serialize_state;
    SdsSerializeFunc serialize_status;
    
    SdsDeserializeFunc deserialize_config;
    SdsDeserializeFunc deserialize_state;
    SdsDeserializeFunc deserialize_status;
    
    /* Field metadata for delta serialization */
    const SdsFieldMeta* config_fields;
    uint8_t config_field_count;
    const SdsFieldMeta* state_fields;
    uint8_t state_field_count;
    const SdsFieldMeta* status_fields;
    uint8_t status_field_count;
} SdsTableMeta;

const SdsTableMeta* sds_find_table_meta(const char* table_type);
void sds_set_table_registry(const SdsTableMeta* registry, size_t count);

/* ============== Initialization API ============== */

SdsError sds_init(const SdsConfig* config);
void sds_loop(void);
void sds_shutdown(void);
bool sds_is_ready(void);
const char* sds_get_node_id(void);
const SdsStats* sds_get_stats(void);

/* ============== Table Registration API ============== */

SdsError sds_register_table(
    void* table,
    const char* table_type,
    SdsRole role,
    const SdsTableOptions* options
);

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

SdsError sds_unregister_table(const char* table_type);
uint8_t sds_get_table_count(void);

/* ============== Event Callbacks ============== */

void sds_on_config_update(const char* table_type, SdsConfigCallback callback, void* user_data);
void sds_on_state_update(const char* table_type, SdsStateCallback callback, void* user_data);
void sds_on_status_update(const char* table_type, SdsStatusCallback callback, void* user_data);
void sds_on_error(SdsErrorCallback callback);
void sds_on_version_mismatch(SdsVersionMismatchCallback callback);

const char* sds_get_schema_version(void);
void sds_set_schema_version(const char* version);

/* ============== Owner Helper Functions ============== */

const void* sds_find_node_status(
    const void* owner_table,
    const char* table_type,
    const char* node_id
);

void sds_foreach_node(
    const void* owner_table,
    const char* table_type,
    SdsNodeIterator callback,
    void* user_data
);

void sds_set_owner_status_slots(
    const char* table_type,
    size_t slots_offset,
    size_t slot_size,
    size_t slot_status_offset,
    size_t count_offset,
    uint8_t max_slots
);

void sds_set_owner_slot_offsets(
    const char* table_type,
    size_t valid_offset,
    size_t online_offset,
    size_t last_seen_offset
);

bool sds_is_device_online(
    const void* owner_table,
    const char* table_type,
    const char* node_id,
    uint32_t timeout_ms
);

uint32_t sds_get_liveness_interval(const char* table_type);

uint32_t sds_get_eviction_grace(const char* table_type);
void sds_on_device_evicted(const char* table_type, SdsDeviceEvictedCallback callback, void* user_data);
void sds_set_owner_eviction_offsets(
    const char* table_type,
    size_t eviction_pending_offset,
    size_t eviction_deadline_offset
);

/* ============== JSON API (for advanced usage) ============== */

void sds_json_writer_init(SdsJsonWriter* w, char* buffer, size_t size);
void sds_json_start_object(SdsJsonWriter* w);
void sds_json_end_object(SdsJsonWriter* w);
void sds_json_add_string(SdsJsonWriter* w, const char* key, const char* value);
void sds_json_add_int(SdsJsonWriter* w, const char* key, int32_t value);
void sds_json_add_uint(SdsJsonWriter* w, const char* key, uint32_t value);
void sds_json_add_float(SdsJsonWriter* w, const char* key, float value);
void sds_json_add_bool(SdsJsonWriter* w, const char* key, bool value);
const char* sds_json_get_string(SdsJsonWriter* w);
size_t sds_json_get_length(SdsJsonWriter* w);
bool sds_json_has_error(SdsJsonWriter* w);

void sds_json_reader_init(SdsJsonReader* r, const char* json, size_t len);
const char* sds_json_find_field(SdsJsonReader* r, const char* key);
bool sds_json_parse_string(const char* value, char* out, size_t out_size);
bool sds_json_parse_int(const char* value, int32_t* out);
bool sds_json_parse_uint(const char* value, uint32_t* out);
bool sds_json_parse_float(const char* value, float* out);
bool sds_json_parse_bool(const char* value, bool* out);
bool sds_json_get_string_field(SdsJsonReader* r, const char* key, char* out, size_t out_size);
bool sds_json_get_int_field(SdsJsonReader* r, const char* key, int32_t* out);
bool sds_json_get_uint_field(SdsJsonReader* r, const char* key, uint32_t* out);
bool sds_json_get_float_field(SdsJsonReader* r, const char* key, float* out);
bool sds_json_get_bool_field(SdsJsonReader* r, const char* key, bool* out);
bool sds_json_get_uint8_field(SdsJsonReader* r, const char* key, uint8_t* out);
