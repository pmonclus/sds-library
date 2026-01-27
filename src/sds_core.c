/*
 * sds_core.c - SDS Core Implementation
 * 
 * Handles initialization, table registration, sync, and message handling.
 */

#include "sds.h"
#include "sds_json.h"
#include "sds_platform.h"

#include <string.h>
#include <stdio.h>

/* ============== Shadow Buffer Configuration ============== */

/*
 * Use generated max section size if available (from sds_types.h),
 * otherwise fall back to 256 bytes for manual/standalone usage.
 */
#ifdef SDS_GENERATED_MAX_SECTION_SIZE
    #define SDS_SHADOW_SIZE SDS_GENERATED_MAX_SECTION_SIZE
#else
    #define SDS_SHADOW_SIZE 256
#endif

/* ============== Internal Types ============== */

/* Serialization callback types */
typedef void (*SdsSerializeFunc)(void* table, SdsJsonWriter* w);
typedef void (*SdsDeserializeFunc)(void* table, SdsJsonReader* r);

typedef struct {
    bool active;
    void* table;
    char table_type[SDS_MAX_TABLE_TYPE_LEN];
    SdsRole role;
    uint32_t sync_interval_ms;
    uint32_t last_sync_ms;
    uint32_t liveness_interval_ms;  /* Max time between status publishes */
    uint32_t last_publish_ms;       /* Last time we published anything (for liveness) */
    
    /* Serialization callbacks (set during registration) */
    SdsSerializeFunc serialize_config;
    SdsDeserializeFunc deserialize_config;
    SdsSerializeFunc serialize_state;
    SdsDeserializeFunc deserialize_state;
    SdsSerializeFunc serialize_status;
    SdsDeserializeFunc deserialize_status;
    
    /* Shadow copies for change detection */
    uint8_t shadow_config[SDS_SHADOW_SIZE];
    uint8_t shadow_state[SDS_SHADOW_SIZE];
    uint8_t shadow_status[SDS_SHADOW_SIZE];
    size_t config_size;
    size_t state_size;
    size_t status_size;
    
    /* Offsets within table struct */
    size_t config_offset;
    size_t state_offset;
    size_t status_offset;
    
    /* User callbacks */
    SdsConfigCallback config_callback;
    SdsStateCallback state_callback;
    SdsStatusCallback status_callback;
    
    /* For owner: status slot management */
    size_t status_slots_offset;  /* Offset to status array in owner table */
    size_t status_slot_size;     /* Size of each status slot */
    uint8_t max_status_slots;
    size_t slot_status_offset;      /* Offset to status within a slot */
    size_t status_count_offset;     /* Offset to status_count in owner table */
} SdsTableContext;

/* ============== Log Level ============== */

static SdsLogLevel _log_level = SDS_LOG_INFO;  /* Default to INFO level */

void sds_set_log_level(SdsLogLevel level) {
    _log_level = level;
}

SdsLogLevel sds_get_log_level(void) {
    return _log_level;
}

/* ============== Internal State ============== */

static bool _initialized = false;
static char _node_id[SDS_MAX_NODE_ID_LEN] = "";
static SdsTableContext _tables[SDS_MAX_TABLES];
static uint8_t _table_count = 0;
static SdsStats _stats = {0};

#define SDS_MAX_BROKER_LEN 128
#define SDS_MAX_CREDENTIAL_LEN 64
static char _mqtt_broker_buf[SDS_MAX_BROKER_LEN] = "";
static const char* _mqtt_broker = NULL;
static uint16_t _mqtt_port = SDS_DEFAULT_MQTT_PORT;
static char _mqtt_username_buf[SDS_MAX_CREDENTIAL_LEN] = "";
static char _mqtt_password_buf[SDS_MAX_CREDENTIAL_LEN] = "";
static bool _mqtt_auth_enabled = false;

/* Table metadata registry (set by generated code) */
static const SdsTableMeta* _table_registry = NULL;
static size_t _table_registry_count = 0;

/* Error callback for async error notifications */
static SdsErrorCallback _error_callback = NULL;

/* Version mismatch callback */
static SdsVersionMismatchCallback _version_mismatch_callback = NULL;

/* Schema version (set from generated code via sds_set_table_registry) */
#define SDS_MAX_VERSION_LEN 32
static char _schema_version[SDS_MAX_VERSION_LEN] = "unknown";

/* Reconnection backoff state */
static uint32_t _reconnect_backoff_ms = 0;
static uint32_t _last_reconnect_attempt_ms = 0;
#define SDS_RECONNECT_INITIAL_MS   1000   /* Start with 1 second */
#define SDS_RECONNECT_MAX_MS       60000  /* Max 60 seconds */
#define SDS_RECONNECT_MULTIPLIER   2      /* Double each time */

/* ============== Forward Declarations ============== */

static void on_mqtt_message(const char* topic, const uint8_t* payload, size_t payload_len);
static SdsTableContext* find_table(const char* table_type);
static void notify_error(SdsError error, const char* context);
static void subscribe_table_topics(SdsTableContext* ctx);
static void unsubscribe_table_topics(SdsTableContext* ctx);
static void sync_table(SdsTableContext* ctx);
static void handle_config_message(SdsTableContext* ctx, const uint8_t* payload, size_t len);
static void handle_state_message(SdsTableContext* ctx, const char* from_node, const uint8_t* payload, size_t len);
static void handle_status_message(SdsTableContext* ctx, const char* from_node, const uint8_t* payload, size_t len);

/* ============== Error Strings ============== */

const char* sds_error_string(SdsError error) {
    switch (error) {
        case SDS_OK:                        return "OK";
        case SDS_ERR_NOT_INITIALIZED:       return "Not initialized";
        case SDS_ERR_ALREADY_INITIALIZED:   return "Already initialized";
        case SDS_ERR_INVALID_CONFIG:        return "Invalid configuration";
        case SDS_ERR_MQTT_CONNECT_FAILED:   return "MQTT connect failed";
        case SDS_ERR_MQTT_DISCONNECTED:     return "MQTT disconnected";
        case SDS_ERR_TABLE_NOT_FOUND:       return "Table not found";
        case SDS_ERR_TABLE_ALREADY_REGISTERED: return "Table already registered";
        case SDS_ERR_MAX_TABLES_REACHED:    return "Max tables reached";
        case SDS_ERR_INVALID_TABLE:         return "Invalid table";
        case SDS_ERR_INVALID_ROLE:          return "Invalid role";
        case SDS_ERR_OWNER_EXISTS:          return "Owner already exists";
        case SDS_ERR_MAX_NODES_REACHED:     return "Max nodes reached";
        case SDS_ERR_BUFFER_FULL:           return "Buffer full";
        case SDS_ERR_SECTION_TOO_LARGE:     return "Section too large for shadow buffer";
        case SDS_ERR_PLATFORM_NOT_SET:      return "Platform not set";
        case SDS_ERR_PLATFORM_ERROR:        return "Platform error";
        default:                            return "Unknown error";
    }
}

/* ============== Table Registry ============== */

void sds_set_table_registry(const SdsTableMeta* registry, size_t count) {
    _table_registry = registry;
    _table_registry_count = count;
}

const SdsTableMeta* sds_find_table_meta(const char* table_type) {
    if (!_table_registry || !table_type) {
        return NULL;
    }
    
    for (size_t i = 0; i < _table_registry_count; i++) {
        if (strcmp(_table_registry[i].table_type, table_type) == 0) {
            return &_table_registry[i];
        }
    }
    return NULL;
}

/* ============== Initialization ============== */

SdsError sds_init(const SdsConfig* config) {
    if (_initialized) {
        return SDS_ERR_ALREADY_INITIALIZED;
    }
    
    if (!config || !config->mqtt_broker) {
        return SDS_ERR_INVALID_CONFIG;
    }
    
    /* Initialize platform */
    if (!sds_platform_init()) {
        return SDS_ERR_PLATFORM_ERROR;
    }
    
    /* Set node ID with validation */
    if (config->node_id && config->node_id[0] != '\0') {
        size_t node_id_len = strlen(config->node_id);
        if (node_id_len >= SDS_MAX_NODE_ID_LEN) {
            SDS_LOG_E("Node ID too long (%zu chars, max %d)", node_id_len, SDS_MAX_NODE_ID_LEN - 1);
            sds_platform_shutdown();
            return SDS_ERR_INVALID_CONFIG;
        }
        strncpy(_node_id, config->node_id, SDS_MAX_NODE_ID_LEN - 1);
        _node_id[SDS_MAX_NODE_ID_LEN - 1] = '\0';
    } else {
        snprintf(_node_id, SDS_MAX_NODE_ID_LEN, "node_%08x", (unsigned int)sds_platform_millis());
        SDS_LOG_D("Using auto-generated node_id: %s", _node_id);
    }
    
    /* Store broker info - copy the string to avoid dangling pointer */
    size_t broker_len = strlen(config->mqtt_broker);
    if (broker_len >= SDS_MAX_BROKER_LEN) {
        SDS_LOG_E("Broker hostname too long (%zu chars, max %d)", broker_len, SDS_MAX_BROKER_LEN - 1);
        sds_platform_shutdown();
        return SDS_ERR_INVALID_CONFIG;
    }
    strncpy(_mqtt_broker_buf, config->mqtt_broker, SDS_MAX_BROKER_LEN - 1);
    _mqtt_broker_buf[SDS_MAX_BROKER_LEN - 1] = '\0';
    _mqtt_broker = _mqtt_broker_buf;
    if (config->mqtt_port) {
        _mqtt_port = config->mqtt_port;
    } else {
        _mqtt_port = SDS_DEFAULT_MQTT_PORT;
        SDS_LOG_D("Using default MQTT port: %u", _mqtt_port);
    }
    
    /* Store authentication credentials if provided */
    _mqtt_auth_enabled = false;
    _mqtt_username_buf[0] = '\0';
    _mqtt_password_buf[0] = '\0';
    if (config->mqtt_username && config->mqtt_username[0] != '\0') {
        size_t username_len = strlen(config->mqtt_username);
        if (username_len >= SDS_MAX_CREDENTIAL_LEN) {
            SDS_LOG_E("MQTT username too long (%zu chars, max %d)", username_len, SDS_MAX_CREDENTIAL_LEN - 1);
            sds_platform_shutdown();
            return SDS_ERR_INVALID_CONFIG;
        }
        strncpy(_mqtt_username_buf, config->mqtt_username, SDS_MAX_CREDENTIAL_LEN - 1);
        _mqtt_username_buf[SDS_MAX_CREDENTIAL_LEN - 1] = '\0';
        
        if (config->mqtt_password) {
            size_t password_len = strlen(config->mqtt_password);
            if (password_len >= SDS_MAX_CREDENTIAL_LEN) {
                SDS_LOG_E("MQTT password too long (%zu chars, max %d)", password_len, SDS_MAX_CREDENTIAL_LEN - 1);
                sds_platform_shutdown();
                return SDS_ERR_INVALID_CONFIG;
            }
            strncpy(_mqtt_password_buf, config->mqtt_password, SDS_MAX_CREDENTIAL_LEN - 1);
            _mqtt_password_buf[SDS_MAX_CREDENTIAL_LEN - 1] = '\0';
        }
        _mqtt_auth_enabled = true;
        SDS_LOG_D("MQTT authentication enabled for user: %s", _mqtt_username_buf);
    }
    
    /* Initialize tables */
    memset(_tables, 0, sizeof(_tables));
    _table_count = 0;
    memset(&_stats, 0, sizeof(_stats));
    
    /* Reset reconnect backoff */
    _reconnect_backoff_ms = 0;
    _last_reconnect_attempt_ms = 0;
    
    /* Set MQTT callback */
    sds_platform_mqtt_set_callback(on_mqtt_message);
    
    /* Build LWT topic and payload */
    char lwt_topic[SDS_TOPIC_BUFFER_SIZE];
    char lwt_payload[128];
    snprintf(lwt_topic, sizeof(lwt_topic), "sds/lwt/%s", _node_id);
    snprintf(lwt_payload, sizeof(lwt_payload), "{\"online\":false,\"node\":\"%s\",\"ts\":0}", _node_id);
    
    /* Connect to MQTT with LWT (and auth if configured) */
    bool connect_success;
    if (_mqtt_auth_enabled) {
        connect_success = sds_platform_mqtt_connect_with_auth(
            _mqtt_broker, _mqtt_port, _node_id,
            _mqtt_username_buf, _mqtt_password_buf,
            lwt_topic, (uint8_t*)lwt_payload, strlen(lwt_payload), true);
    } else {
        connect_success = sds_platform_mqtt_connect_with_lwt(
            _mqtt_broker, _mqtt_port, _node_id,
            lwt_topic, (uint8_t*)lwt_payload, strlen(lwt_payload), true);
    }
    
    if (!connect_success) {
        sds_platform_shutdown();
        return SDS_ERR_MQTT_CONNECT_FAILED;
    }
    
    _initialized = true;
    SDS_LOG_I("SDS initialized: node_id=%s", _node_id);
    
    return SDS_OK;
}

void sds_loop(void) {
    if (!_initialized) {
        static bool warned = false;
        if (!warned) {
            SDS_LOG_W("sds_loop called before sds_init()");
            warned = true;
        }
        return;
    }
    
    /* Check MQTT connection */
    if (!sds_platform_mqtt_connected()) {
        uint32_t now = sds_platform_millis();
        
        /* Apply exponential backoff */
        if (_reconnect_backoff_ms > 0) {
            uint32_t elapsed = now - _last_reconnect_attempt_ms;
            if (elapsed < _reconnect_backoff_ms) {
                /* Not yet time to retry */
                return;
            }
        }
        
        /* Initialize or increase backoff */
        if (_reconnect_backoff_ms == 0) {
            _reconnect_backoff_ms = SDS_RECONNECT_INITIAL_MS;
        } else {
            _reconnect_backoff_ms *= SDS_RECONNECT_MULTIPLIER;
            if (_reconnect_backoff_ms > SDS_RECONNECT_MAX_MS) {
                _reconnect_backoff_ms = SDS_RECONNECT_MAX_MS;
            }
        }
        
        _last_reconnect_attempt_ms = now;
        SDS_LOG_W("MQTT disconnected, attempting reconnect (backoff: %u ms)...", _reconnect_backoff_ms);
        
        /* Rebuild LWT for reconnect */
        char lwt_topic[SDS_TOPIC_BUFFER_SIZE];
        char lwt_payload[128];
        snprintf(lwt_topic, sizeof(lwt_topic), "sds/lwt/%s", _node_id);
        snprintf(lwt_payload, sizeof(lwt_payload), "{\"online\":false,\"node\":\"%s\",\"ts\":0}", _node_id);
        
        /* Reconnect with auth if configured */
        bool reconnect_success;
        if (_mqtt_auth_enabled) {
            reconnect_success = sds_platform_mqtt_connect_with_auth(
                _mqtt_broker, _mqtt_port, _node_id,
                _mqtt_username_buf, _mqtt_password_buf,
                lwt_topic, (uint8_t*)lwt_payload, strlen(lwt_payload), true);
        } else {
            reconnect_success = sds_platform_mqtt_connect_with_lwt(
                _mqtt_broker, _mqtt_port, _node_id,
                lwt_topic, (uint8_t*)lwt_payload, strlen(lwt_payload), true);
        }
        
        if (reconnect_success) {
            _stats.reconnect_count++;
            SDS_LOG_I("MQTT reconnected successfully");
            
            /* Reset backoff on success */
            _reconnect_backoff_ms = 0;
            
            /* Re-subscribe to all tables */
            for (int i = 0; i < SDS_MAX_TABLES; i++) {
                if (_tables[i].active) {
                    subscribe_table_topics(&_tables[i]);
                }
            }
        } else {
            notify_error(SDS_ERR_MQTT_DISCONNECTED, "Reconnect failed");
            SDS_LOG_W("Reconnect failed, next attempt in %u ms", _reconnect_backoff_ms);
        }
        return;
    }
    
    /* Process MQTT messages */
    sds_platform_mqtt_loop();
    
    uint32_t now = sds_platform_millis();
    
    /* Sync each table */
    for (int i = 0; i < SDS_MAX_TABLES; i++) {
        SdsTableContext* ctx = &_tables[i];
        if (!ctx->active) continue;
        
        if (now - ctx->last_sync_ms >= ctx->sync_interval_ms) {
            sync_table(ctx);
            ctx->last_sync_ms = now;
        }
    }
}

void sds_shutdown(void) {
    if (!_initialized) {
        return;
    }
    
    /* Publish graceful offline message (prevents broker from sending LWT) */
    if (sds_platform_mqtt_connected()) {
        char lwt_topic[SDS_TOPIC_BUFFER_SIZE];
        char lwt_payload[128];
        snprintf(lwt_topic, sizeof(lwt_topic), "sds/lwt/%s", _node_id);
        snprintf(lwt_payload, sizeof(lwt_payload), "{\"online\":false,\"node\":\"%s\",\"ts\":%u}", 
                 _node_id, sds_platform_millis());
        sds_platform_mqtt_publish(lwt_topic, (uint8_t*)lwt_payload, strlen(lwt_payload), true);
        SDS_LOG_D("Published graceful offline message");
    }
    
    for (int i = 0; i < SDS_MAX_TABLES; i++) {
        if (_tables[i].active) {
            unsubscribe_table_topics(&_tables[i]);
            _tables[i].active = false;
        }
    }
    _table_count = 0;
    
    sds_platform_shutdown();
    _initialized = false;
    SDS_LOG_I("SDS shutdown complete");
}

bool sds_is_ready(void) {
    return _initialized && sds_platform_mqtt_connected();
}

const char* sds_get_node_id(void) {
    if (!_initialized) {
        return NULL;
    }
    return _node_id;
}

/* ============== Table Registration ============== */

/* Internal: allocate and initialize a table slot */
static SdsTableContext* alloc_table_slot(void* table, const char* table_type, SdsRole role, const SdsTableOptions* options) {
    SdsTableContext* ctx = NULL;
    for (int i = 0; i < SDS_MAX_TABLES; i++) {
        if (!_tables[i].active) {
            ctx = &_tables[i];
            break;
        }
    }
    
    if (!ctx) {
        return NULL;  /* No slots available */
    }
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->active = true;
    ctx->table = table;
    strncpy(ctx->table_type, table_type, SDS_MAX_TABLE_TYPE_LEN - 1);
    ctx->role = role;
    ctx->sync_interval_ms = options ? options->sync_interval_ms : SDS_DEFAULT_SYNC_INTERVAL_MS;
    ctx->liveness_interval_ms = SDS_DEFAULT_LIVENESS_INTERVAL_MS;  /* Will be overridden from registry */
    ctx->last_sync_ms = sds_platform_millis();
    ctx->last_publish_ms = sds_platform_millis();  /* Initialize to now */
    
    _table_count++;
    
    return ctx;
}

/* Internal: subscribe to topics for a table */
static void sds_activate_table_subscriptions(SdsTableContext* ctx) {
    if (sds_platform_mqtt_connected()) {
        subscribe_table_topics(ctx);
    }
}

SdsError sds_register_table(void* table, const char* table_type, SdsRole role, const SdsTableOptions* options) {
    if (!_initialized) {
        return SDS_ERR_NOT_INITIALIZED;
    }
    
    if (!table || !table_type || table_type[0] == '\0') {
        return SDS_ERR_INVALID_TABLE;
    }
    
    if (role != SDS_ROLE_OWNER && role != SDS_ROLE_DEVICE) {
        return SDS_ERR_INVALID_ROLE;
    }
    
    /* Look up table metadata in registry */
    const SdsTableMeta* meta = sds_find_table_meta(table_type);
    if (!meta) {
        SDS_LOG_E("Table type '%s' not found in registry. Call sds_set_table_registry() first.", table_type);
        return SDS_ERR_TABLE_NOT_FOUND;
    }
    
    /* Use registry metadata to call sds_register_table_ex */
    if (role == SDS_ROLE_DEVICE) {
        SdsError err = sds_register_table_ex(
            table, table_type, role, options,
            meta->dev_config_offset, meta->dev_config_size,
            meta->dev_state_offset, meta->dev_state_size,
            meta->dev_status_offset, meta->dev_status_size,
            NULL,                         /* Device doesn't serialize config */
            meta->deserialize_config,     /* Device receives config */
            meta->serialize_state,        /* Device sends state */
            NULL,                         /* Device doesn't receive state */
            meta->serialize_status,       /* Device sends status */
            NULL                          /* Device doesn't receive status */
        );
        
        /* Set liveness interval from registry */
        if (err == SDS_OK) {
            SdsTableContext* ctx = find_table(table_type);
            if (ctx) {
                ctx->liveness_interval_ms = meta->liveness_interval_ms;
            }
        }
        
        return err;
    } else {
        /* OWNER role */
        SdsError err = sds_register_table_ex(
            table, table_type, role, options,
            meta->own_config_offset, meta->own_config_size,
            meta->own_state_offset, meta->own_state_size,
            0, 0,                         /* Owner doesn't send status */
            meta->serialize_config,       /* Owner sends config */
            NULL,                         /* Owner doesn't receive config */
            NULL,                         /* Owner doesn't send state */
            meta->deserialize_state,      /* Owner receives state */
            NULL,                         /* Owner doesn't send status */
            meta->deserialize_status      /* Owner receives status */
        );
        
        /* Populate status slot metadata and liveness for owners */
        if (err == SDS_OK) {
            SdsTableContext* ctx = find_table(table_type);
            if (ctx) {
                ctx->liveness_interval_ms = meta->liveness_interval_ms;
                if (meta->own_status_slots_offset > 0) {
                    ctx->status_slots_offset = meta->own_status_slots_offset;
                    ctx->status_slot_size = meta->own_status_slot_size;
                    ctx->max_status_slots = meta->own_max_status_slots;
                    ctx->slot_status_offset = meta->slot_status_offset;
                    ctx->status_count_offset = meta->own_status_count_offset;
                }
            }
        }
        
        return err;
    }
}

SdsError sds_unregister_table(const char* table_type) {
    if (!_initialized) {
        return SDS_ERR_NOT_INITIALIZED;
    }
    
    SdsTableContext* ctx = find_table(table_type);
    if (!ctx) {
        return SDS_ERR_TABLE_NOT_FOUND;
    }
    
    if (sds_platform_mqtt_connected()) {
        unsubscribe_table_topics(ctx);
    }
    
    ctx->active = false;
    _table_count--;
    
    SDS_LOG_I("Table unregistered: %s", table_type);
    
    return SDS_OK;
}

uint8_t sds_get_table_count(void) {
    return _table_count;
}

/* ============== Extended Registration with Serialization ============== */

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
) {
    if (!_initialized) {
        return SDS_ERR_NOT_INITIALIZED;
    }
    
    if (!table || !table_type || table_type[0] == '\0') {
        return SDS_ERR_INVALID_TABLE;
    }
    
    if (role != SDS_ROLE_OWNER && role != SDS_ROLE_DEVICE) {
        return SDS_ERR_INVALID_ROLE;
    }
    
    /* Check if already registered */
    if (find_table(table_type)) {
        return SDS_ERR_TABLE_ALREADY_REGISTERED;
    }
    
    /* Allocate a table slot */
    SdsTableContext* ctx = alloc_table_slot(table, table_type, role, options);
    if (!ctx) {
        return SDS_ERR_MAX_TABLES_REACHED;
    }
    
    /* Validate section sizes against shadow buffer capacity */
    if (config_size > SDS_SHADOW_SIZE || state_size > SDS_SHADOW_SIZE || status_size > SDS_SHADOW_SIZE) {
        SDS_LOG_E("Section size exceeds SDS_SHADOW_SIZE (%d bytes): config=%zu state=%zu status=%zu",
                  (int)SDS_SHADOW_SIZE, config_size, state_size, status_size);
        ctx->active = false;
        _table_count--;
        return SDS_ERR_SECTION_TOO_LARGE;
    }
    
    ctx->config_offset = config_offset;
    ctx->config_size = config_size;
    ctx->state_offset = state_offset;
    ctx->state_size = state_size;
    ctx->status_offset = status_offset;
    ctx->status_size = status_size;
    
    ctx->serialize_config = serialize_config;
    ctx->deserialize_config = deserialize_config;
    ctx->serialize_state = serialize_state;
    ctx->deserialize_state = deserialize_state;
    ctx->serialize_status = serialize_status;
    ctx->deserialize_status = deserialize_status;
    
    /* Initialize shadows to zero (so first sync detects change) */
    memset(ctx->shadow_config, 0, sizeof(ctx->shadow_config));
    memset(ctx->shadow_state, 0, sizeof(ctx->shadow_state));
    memset(ctx->shadow_status, 0, sizeof(ctx->shadow_status));
    
    /* Now that callbacks are set, subscribe to topics */
    sds_activate_table_subscriptions(ctx);
    
    /* Force initial sync for owners to publish config immediately */
    if (role == SDS_ROLE_OWNER && serialize_config && config_size > 0) {
        char topic[SDS_TOPIC_BUFFER_SIZE];
        char buffer[SDS_MSG_BUFFER_SIZE];
        SdsJsonWriter w;
        void* config_ptr = (uint8_t*)table + config_offset;
        
        sds_json_writer_init(&w, buffer, sizeof(buffer));
        sds_json_start_object(&w);
        sds_json_add_uint(&w, "ts", sds_platform_millis());
        sds_json_add_string(&w, "from", _node_id);
        serialize_config(config_ptr, &w);  /* Pass section pointer */
        sds_json_end_object(&w);
        
        snprintf(topic, sizeof(topic), "sds/%s/config", table_type);
        sds_platform_mqtt_publish(topic, (uint8_t*)buffer, sds_json_get_length(&w), true);
        
        /* Update shadow after initial publish */
        memcpy(ctx->shadow_config, config_ptr, ctx->config_size);
        _stats.messages_sent++;
        SDS_LOG_I("Published initial config: %s", table_type);
    }
    
    SDS_LOG_I("Table registered: %s (role=%s)", table_type, 
              role == SDS_ROLE_OWNER ? "OWNER" : "DEVICE");
    
    return SDS_OK;
}

/* ============== Callbacks ============== */

void sds_on_config_update(const char* table_type, SdsConfigCallback callback) {
    SdsTableContext* ctx = find_table(table_type);
    if (ctx) {
        ctx->config_callback = callback;
    }
}

void sds_on_state_update(const char* table_type, SdsStateCallback callback) {
    SdsTableContext* ctx = find_table(table_type);
    if (ctx) {
        ctx->state_callback = callback;
    }
}

void sds_on_status_update(const char* table_type, SdsStatusCallback callback) {
    SdsTableContext* ctx = find_table(table_type);
    if (ctx) {
        ctx->status_callback = callback;
    }
}

void sds_on_error(SdsErrorCallback callback) {
    _error_callback = callback;
}

void sds_on_version_mismatch(SdsVersionMismatchCallback callback) {
    _version_mismatch_callback = callback;
}

const char* sds_get_schema_version(void) {
    return _schema_version;
}

/**
 * Set the schema version (called from generated code or manually).
 * 
 * @param version Schema version string
 */
void sds_set_schema_version(const char* version) {
    if (version) {
        strncpy(_schema_version, version, SDS_MAX_VERSION_LEN - 1);
        _schema_version[SDS_MAX_VERSION_LEN - 1] = '\0';
    }
}

void sds_set_owner_status_slots(
    const char* table_type,
    size_t slots_offset,
    size_t slot_size,
    size_t slot_status_offset,
    size_t count_offset,
    uint8_t max_slots
) {
    SdsTableContext* ctx = find_table(table_type);
    if (!ctx || ctx->role != SDS_ROLE_OWNER) {
        SDS_LOG_W("sds_set_owner_status_slots: table %s not found or not owner", 
                  table_type ? table_type : "(null)");
        return;
    }
    
    ctx->status_slots_offset = slots_offset;
    ctx->status_slot_size = slot_size;
    ctx->slot_status_offset = slot_status_offset;
    ctx->status_count_offset = count_offset;
    ctx->max_status_slots = max_slots;
    
    SDS_LOG_D("Configured status slots for %s: offset=%zu size=%zu max=%d",
              table_type, slots_offset, slot_size, max_slots);
}

/**
 * Internal helper to notify errors through the callback.
 */
static void notify_error(SdsError error, const char* context) {
    _stats.errors++;
    if (_error_callback) {
        _error_callback(error, context);
    }
}

/* ============== Statistics ============== */

const SdsStats* sds_get_stats(void) {
    return &_stats;
}

/* ============== Owner Helpers ============== */

const void* sds_find_node_status(const void* owner_table, const char* table_type, const char* node_id) {
    if (!owner_table || !table_type || !node_id) return NULL;
    
    /* Find the table context to get slot metadata */
    SdsTableContext* ctx = find_table(table_type);
    if (!ctx || ctx->role != SDS_ROLE_OWNER) return NULL;
    if (ctx->status_slots_offset == 0 || ctx->max_status_slots == 0) return NULL;
    
    const uint8_t* slots_base = (const uint8_t*)owner_table + ctx->status_slots_offset;
    
    /* Search for the node */
    for (uint8_t i = 0; i < ctx->max_status_slots; i++) {
        const uint8_t* slot = slots_base + (i * ctx->status_slot_size);
        const char* slot_node_id = (const char*)slot;
        const bool* slot_valid = (const bool*)(slot + SDS_MAX_NODE_ID_LEN);
        
        if (*slot_valid && strcmp(slot_node_id, node_id) == 0) {
            /* Return pointer to status within the slot */
            return slot + ctx->slot_status_offset;
        }
    }
    
    return NULL;
}

void sds_foreach_node(const void* owner_table, const char* table_type, SdsNodeIterator callback, void* user_data) {
    if (!owner_table || !table_type || !callback) return;
    
    /* Find the table context to get slot metadata */
    SdsTableContext* ctx = find_table(table_type);
    if (!ctx || ctx->role != SDS_ROLE_OWNER) return;
    if (ctx->status_slots_offset == 0 || ctx->max_status_slots == 0) return;
    
    const uint8_t* slots_base = (const uint8_t*)owner_table + ctx->status_slots_offset;
    
    /* Iterate over all valid slots */
    for (uint8_t i = 0; i < ctx->max_status_slots; i++) {
        const uint8_t* slot = slots_base + (i * ctx->status_slot_size);
        const char* slot_node_id = (const char*)slot;
        const bool* slot_valid = (const bool*)(slot + SDS_MAX_NODE_ID_LEN);
        
        if (*slot_valid) {
            const void* status = slot + ctx->slot_status_offset;
            callback(slot_node_id, status, user_data);
        }
    }
}

bool sds_is_device_online(const void* owner_table, const char* table_type, const char* node_id, uint32_t timeout_ms) {
    if (!owner_table || !table_type || !node_id) return false;
    
    /* Find the table context */
    SdsTableContext* ctx = find_table(table_type);
    if (!ctx || ctx->role != SDS_ROLE_OWNER) return false;
    if (ctx->status_slots_offset == 0 || ctx->max_status_slots == 0) return false;
    
    const uint8_t* slots_base = (const uint8_t*)owner_table + ctx->status_slots_offset;
    
    /* Find the slot for this node */
    for (uint8_t i = 0; i < ctx->max_status_slots; i++) {
        const uint8_t* slot = slots_base + (i * ctx->status_slot_size);
        const char* slot_node_id = (const char*)slot;
        const bool* slot_valid = (const bool*)(slot + SDS_MAX_NODE_ID_LEN);
        
        if (*slot_valid && strcmp(slot_node_id, node_id) == 0) {
            /* Found the slot - check online flag and last_seen */
            const bool* slot_online = (const bool*)(slot + SDS_MAX_NODE_ID_LEN + sizeof(bool));
            const uint32_t* slot_last_seen = (const uint32_t*)(slot + SDS_MAX_NODE_ID_LEN + 2 * sizeof(bool));
            
            /* Check explicit online flag (false if LWT received) */
            if (!*slot_online) return false;
            
            /* Check if last_seen is within timeout */
            uint32_t now = sds_platform_millis();
            uint32_t age = now - *slot_last_seen;
            return (age < timeout_ms);
        }
    }
    
    return false;  /* Node not found */
}

uint32_t sds_get_liveness_interval(const char* table_type) {
    if (!table_type) return 0;
    
    const SdsTableMeta* meta = sds_find_table_meta(table_type);
    if (meta) {
        return meta->liveness_interval_ms;
    }
    
    /* Also check registered tables (might be manually registered) */
    SdsTableContext* ctx = find_table(table_type);
    if (ctx) {
        return ctx->liveness_interval_ms;
    }
    
    return 0;
}

/* ============== Internal Functions ============== */

static SdsTableContext* find_table(const char* table_type) {
    for (int i = 0; i < SDS_MAX_TABLES; i++) {
        if (_tables[i].active && strcmp(_tables[i].table_type, table_type) == 0) {
            return &_tables[i];
        }
    }
    return NULL;
}

static void subscribe_table_topics(SdsTableContext* ctx) {
    char topic[SDS_TOPIC_BUFFER_SIZE];
    
    if (ctx->role == SDS_ROLE_DEVICE) {
        /* Device subscribes to config */
        snprintf(topic, sizeof(topic), "sds/%s/config", ctx->table_type);
        sds_platform_mqtt_subscribe(topic);
        
    } else if (ctx->role == SDS_ROLE_OWNER) {
        /* Owner subscribes to state and status */
        snprintf(topic, sizeof(topic), "sds/%s/state", ctx->table_type);
        sds_platform_mqtt_subscribe(topic);
        
        snprintf(topic, sizeof(topic), "sds/%s/status/+", ctx->table_type);
        sds_platform_mqtt_subscribe(topic);
    }
}

static void unsubscribe_table_topics(SdsTableContext* ctx) {
    char topic[SDS_TOPIC_BUFFER_SIZE];
    
    if (ctx->role == SDS_ROLE_DEVICE) {
        snprintf(topic, sizeof(topic), "sds/%s/config", ctx->table_type);
        sds_platform_mqtt_unsubscribe(topic);
        
    } else if (ctx->role == SDS_ROLE_OWNER) {
        snprintf(topic, sizeof(topic), "sds/%s/state", ctx->table_type);
        sds_platform_mqtt_unsubscribe(topic);
        
        snprintf(topic, sizeof(topic), "sds/%s/status/+", ctx->table_type);
        sds_platform_mqtt_unsubscribe(topic);
    }
}

static void sync_table(SdsTableContext* ctx) {
    char topic[SDS_TOPIC_BUFFER_SIZE];
    char buffer[SDS_MSG_BUFFER_SIZE];
    SdsJsonWriter w;
    uint32_t now = sds_platform_millis();
    bool published_something = false;
    
    if (ctx->role == SDS_ROLE_OWNER) {
        /* Owner publishes config */
        if (ctx->serialize_config && ctx->config_size > 0) {
            void* config_ptr = (uint8_t*)ctx->table + ctx->config_offset;
            
            /* Check if config changed */
            if (memcmp(config_ptr, ctx->shadow_config, ctx->config_size) != 0) {
                sds_json_writer_init(&w, buffer, sizeof(buffer));
                sds_json_start_object(&w);
                sds_json_add_uint(&w, "ts", now);
                sds_json_add_string(&w, "from", _node_id);
                ctx->serialize_config(config_ptr, &w);  /* Pass section pointer */
                sds_json_end_object(&w);
                
                if (sds_json_has_error(&w)) {
                    notify_error(SDS_ERR_BUFFER_FULL, "Config serialization buffer overflow");
                } else {
                    snprintf(topic, sizeof(topic), "sds/%s/config", ctx->table_type);
                    sds_platform_mqtt_publish(topic, (uint8_t*)buffer, sds_json_get_length(&w), true);
                    
                    memcpy(ctx->shadow_config, config_ptr, ctx->config_size);
                    _stats.messages_sent++;
                    published_something = true;
                    SDS_LOG_D("Published config: %s", ctx->table_type);
                }
            }
        }
    }
    
    /* All nodes can publish state */
    if (ctx->serialize_state && ctx->state_size > 0) {
        void* state_ptr = (uint8_t*)ctx->table + ctx->state_offset;
        
        /* Check if state changed */
        if (memcmp(state_ptr, ctx->shadow_state, ctx->state_size) != 0) {
            sds_json_writer_init(&w, buffer, sizeof(buffer));
            sds_json_start_object(&w);
            sds_json_add_uint(&w, "ts", now);
            sds_json_add_string(&w, "node", _node_id);
            ctx->serialize_state(state_ptr, &w);  /* Pass section pointer */
            sds_json_end_object(&w);
            
            if (sds_json_has_error(&w)) {
                notify_error(SDS_ERR_BUFFER_FULL, "State serialization buffer overflow");
            } else {
                snprintf(topic, sizeof(topic), "sds/%s/state", ctx->table_type);
                sds_platform_mqtt_publish(topic, (uint8_t*)buffer, sds_json_get_length(&w), false);
                
                memcpy(ctx->shadow_state, state_ptr, ctx->state_size);
                _stats.messages_sent++;
                published_something = true;
                SDS_LOG_D("Published state: %s", ctx->table_type);
            }
        }
    }
    
    /* Devices publish status */
    if (ctx->role == SDS_ROLE_DEVICE && ctx->serialize_status && ctx->status_size > 0) {
        void* status_ptr = (uint8_t*)ctx->table + ctx->status_offset;
        
        /* Check if status changed OR liveness timer expired */
        bool status_changed = (memcmp(status_ptr, ctx->shadow_status, ctx->status_size) != 0);
        bool liveness_expired = (ctx->liveness_interval_ms > 0) && 
                                (now - ctx->last_publish_ms >= ctx->liveness_interval_ms);
        
        if (status_changed || liveness_expired) {
            sds_json_writer_init(&w, buffer, sizeof(buffer));
            sds_json_start_object(&w);
            sds_json_add_uint(&w, "ts", now);
            sds_json_add_bool(&w, "online", true);  /* Always include online=true for heartbeat */
            sds_json_add_string(&w, "sv", _schema_version);  /* Schema version */
            ctx->serialize_status(status_ptr, &w);  /* Pass section pointer */
            sds_json_end_object(&w);
            
            if (sds_json_has_error(&w)) {
                notify_error(SDS_ERR_BUFFER_FULL, "Status serialization buffer overflow");
            } else {
                snprintf(topic, sizeof(topic), "sds/%s/status/%s", ctx->table_type, _node_id);
                sds_platform_mqtt_publish(topic, (uint8_t*)buffer, sds_json_get_length(&w), false);
                
                memcpy(ctx->shadow_status, status_ptr, ctx->status_size);
                _stats.messages_sent++;
                published_something = true;
                
                if (liveness_expired && !status_changed) {
                    SDS_LOG_D("Published heartbeat: %s", ctx->table_type);
                } else {
                    SDS_LOG_D("Published status: %s", ctx->table_type);
                }
            }
        }
    }
    
    /* Update last publish time if we sent anything */
    if (published_something) {
        ctx->last_publish_ms = now;
    }
}

static void handle_config_message(SdsTableContext* ctx, const uint8_t* payload, size_t len) {
    if (ctx->role != SDS_ROLE_DEVICE) return;
    if (!ctx->deserialize_config) return;
    
    SdsJsonReader r;
    sds_json_reader_init(&r, (const char*)payload, len);
    
    /* Pass pointer to config section, not full table */
    void* config_ptr = (uint8_t*)ctx->table + ctx->config_offset;
    ctx->deserialize_config(config_ptr, &r);
    
    /* Update shadow */
    if (ctx->config_size > 0) {
        memcpy(ctx->shadow_config, config_ptr, ctx->config_size);
    }
    
    SDS_LOG_I("Config applied: %s", ctx->table_type);
    
    if (ctx->config_callback) {
        ctx->config_callback(ctx->table_type);
    }
}

static void handle_state_message(SdsTableContext* ctx, const char* from_node, const uint8_t* payload, size_t len) {
    if (ctx->role != SDS_ROLE_OWNER) return;
    if (!ctx->deserialize_state) return;
    
    /* Don't process our own state messages */
    if (strcmp(from_node, _node_id) == 0) return;
    
    SdsJsonReader r;
    sds_json_reader_init(&r, (const char*)payload, len);
    
    /* Pass pointer to state section */
    void* state_ptr = (uint8_t*)ctx->table + ctx->state_offset;
    ctx->deserialize_state(state_ptr, &r);
    
    /* Update shadow (owner's merged state) */
    if (ctx->state_size > 0) {
        memcpy(ctx->shadow_state, state_ptr, ctx->state_size);
    }
    
    SDS_LOG_I("State received from %s: %s", from_node, ctx->table_type);
    
    if (ctx->state_callback) {
        ctx->state_callback(ctx->table_type, from_node);
    }
}

/**
 * Find or allocate a status slot for a device node.
 * 
 * @param ctx Table context (must be OWNER role)
 * @param node_id Device node ID to find/allocate
 * @return Pointer to status slot, or NULL if slots are full/not configured
 */
static void* find_or_alloc_status_slot(SdsTableContext* ctx, const char* node_id) {
    if (ctx->status_slots_offset == 0 || ctx->max_status_slots == 0) {
        return NULL;  /* No slots configured */
    }
    
    uint8_t* slots_base = (uint8_t*)ctx->table + ctx->status_slots_offset;
    
    /* Search for existing slot with this node_id */
    for (uint8_t i = 0; i < ctx->max_status_slots; i++) {
        uint8_t* slot = slots_base + (i * ctx->status_slot_size);
        char* slot_node_id = (char*)slot;  /* node_id is first field */
        bool* slot_valid = (bool*)(slot + SDS_MAX_NODE_ID_LEN);
        
        if (*slot_valid && strcmp(slot_node_id, node_id) == 0) {
            return slot;  /* Found existing slot */
        }
    }
    
    /* Find first empty slot */
    for (uint8_t i = 0; i < ctx->max_status_slots; i++) {
        uint8_t* slot = slots_base + (i * ctx->status_slot_size);
        bool* slot_valid = (bool*)(slot + SDS_MAX_NODE_ID_LEN);
        
        if (!*slot_valid) {
            /* Initialize new slot */
            strncpy((char*)slot, node_id, SDS_MAX_NODE_ID_LEN - 1);
            ((char*)slot)[SDS_MAX_NODE_ID_LEN - 1] = '\0';
            *slot_valid = true;
            
            /* Initialize online flag to true (will be updated when status received) */
            bool* slot_online = (bool*)(slot + SDS_MAX_NODE_ID_LEN + sizeof(bool));
            *slot_online = true;
            
            /* Initialize last_seen to now */
            uint32_t* last_seen = (uint32_t*)(slot + SDS_MAX_NODE_ID_LEN + 2 * sizeof(bool));
            *last_seen = sds_platform_millis();
            
            /* Increment status_count in owner table */
            if (ctx->status_count_offset > 0) {
                uint8_t* count_ptr = (uint8_t*)ctx->table + ctx->status_count_offset;
                (*count_ptr)++;
            }
            
            SDS_LOG_D("Allocated status slot %d for node: %s", i, node_id);
            return slot;
        }
    }
    
    SDS_LOG_W("Status slots full (%d max), dropping status from %s", 
              ctx->max_status_slots, node_id);
    return NULL;
}

static void handle_status_message(SdsTableContext* ctx, const char* from_node, const uint8_t* payload, size_t len) {
    if (ctx->role != SDS_ROLE_OWNER) return;
    if (!ctx->deserialize_status) return;
    
    /* Parse JSON to check schema version first */
    SdsJsonReader r;
    sds_json_reader_init(&r, (const char*)payload, len);
    
    /* Check schema version */
    char remote_version[SDS_MAX_VERSION_LEN] = "";
    sds_json_get_string_field(&r, "sv", remote_version, sizeof(remote_version));
    
    if (remote_version[0] != '\0' && strcmp(remote_version, _schema_version) != 0) {
        /* Version mismatch detected */
        if (_version_mismatch_callback) {
            bool accept = _version_mismatch_callback(
                ctx->table_type, from_node, _schema_version, remote_version);
            if (!accept) {
                SDS_LOG_W("Schema mismatch rejected: local=%s remote=%s from=%s",
                          _schema_version, remote_version, from_node);
                return;  /* Reject message */
            }
            SDS_LOG_D("Schema mismatch accepted: local=%s remote=%s from=%s",
                      _schema_version, remote_version, from_node);
        } else {
            /* No callback - log warning and accept (backward compatible) */
            SDS_LOG_W("Schema version mismatch: local=%s remote=%s from=%s (accepting)",
                      _schema_version, remote_version, from_node);
        }
    }
    
    /* Find or allocate a slot for this device */
    void* slot = find_or_alloc_status_slot(ctx, from_node);
    if (!slot) {
        /* Log and invoke callback anyway - slot might not be configured */
        SDS_LOG_D("Status from %s (no slot available): %s", from_node, ctx->table_type);
        if (ctx->status_callback) {
            ctx->status_callback(ctx->table_type, from_node);
        }
        return;
    }
    
    /* Re-init reader and parse "online" field */
    sds_json_reader_init(&r, (const char*)payload, len);
    
    /* Parse "online" field (defaults to true - if we got a message, device is online) */
    bool online = true;
    sds_json_get_bool_field(&r, "online", &online);
    
    /* Slot structure: node_id[32], valid(bool), online(bool), last_seen_ms(uint32_t), status */
    bool* slot_online = (bool*)((uint8_t*)slot + SDS_MAX_NODE_ID_LEN + sizeof(bool));
    uint32_t* last_seen = (uint32_t*)((uint8_t*)slot + SDS_MAX_NODE_ID_LEN + 2 * sizeof(bool));
    
    *slot_online = online;
    *last_seen = sds_platform_millis();
    
    /* Only deserialize status data if device is online */
    if (online) {
        /* Get pointer to status data within the slot */
        void* status_ptr = (uint8_t*)slot + ctx->slot_status_offset;
        
        /* Re-init reader and deserialize status */
        sds_json_reader_init(&r, (const char*)payload, len);
        ctx->deserialize_status(status_ptr, &r);
        
        SDS_LOG_D("Status updated from %s: %s", from_node, ctx->table_type);
    } else {
        SDS_LOG_I("Device %s went offline: %s", from_node, ctx->table_type);
    }
    
    if (ctx->status_callback) {
        ctx->status_callback(ctx->table_type, from_node);
    }
}

static void on_mqtt_message(const char* topic, const uint8_t* payload, size_t payload_len) {
    _stats.messages_received++;
    
    SDS_LOG_D("Message: topic=%s len=%zu", topic, payload_len);
    
    if (strncmp(topic, "sds/", 4) != 0) {
        return;
    }
    
    /* Extract table_type */
    const char* table_start = topic + 4;
    const char* table_end = strchr(table_start, '/');
    if (!table_end) return;
    
    char table_type[SDS_MAX_TABLE_TYPE_LEN];
    size_t table_len = table_end - table_start;
    
    /* Validate table_type length */
    if (table_len == 0) {
        SDS_LOG_D("Malformed topic (empty table type): %s", topic);
        return;
    }
    if (table_len >= SDS_MAX_TABLE_TYPE_LEN) {
        SDS_LOG_D("Malformed topic (table type too long): %s", topic);
        return;
    }
    
    strncpy(table_type, table_start, table_len);
    table_type[table_len] = '\0';
    
    SdsTableContext* ctx = find_table(table_type);
    if (!ctx) {
        SDS_LOG_D("Message for unregistered table: %s", table_type);
        return;
    }
    
    const char* section = table_end + 1;
    
    if (strncmp(section, "config", 6) == 0) {
        handle_config_message(ctx, payload, payload_len);
        
    } else if (strncmp(section, "state", 5) == 0) {
        /* Extract node from JSON payload */
        SdsJsonReader r;
        sds_json_reader_init(&r, (const char*)payload, payload_len);
        char from_node[SDS_MAX_NODE_ID_LEN] = "";
        sds_json_get_string_field(&r, "node", from_node, sizeof(from_node));
        
        handle_state_message(ctx, from_node, payload, payload_len);
        
    } else if (strncmp(section, "status/", 7) == 0) {
        const char* from_node = section + 7;
        handle_status_message(ctx, from_node, payload, payload_len);
    }
}
