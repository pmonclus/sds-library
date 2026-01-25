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
    
    /* Serialization callbacks (set during registration) */
    SdsSerializeFunc serialize_config;
    SdsDeserializeFunc deserialize_config;
    SdsSerializeFunc serialize_state;
    SdsDeserializeFunc deserialize_state;
    SdsSerializeFunc serialize_status;
    SdsDeserializeFunc deserialize_status;
    
    /* Shadow copies for change detection */
    uint8_t shadow_config[256];
    uint8_t shadow_state[256];
    uint8_t shadow_status[256];
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
} SdsTableContext;

/* ============== Internal State ============== */

static bool _initialized = false;
static char _node_id[SDS_MAX_NODE_ID_LEN] = "";
static SdsTableContext _tables[SDS_MAX_TABLES];
static uint8_t _table_count = 0;
static SdsStats _stats = {0};

static const char* _mqtt_broker = NULL;
static uint16_t _mqtt_port = SDS_DEFAULT_MQTT_PORT;

/* Table metadata registry (set by generated code) */
static const SdsTableMeta* _table_registry = NULL;
static size_t _table_registry_count = 0;

/* ============== Forward Declarations ============== */

static void on_mqtt_message(const char* topic, const uint8_t* payload, size_t payload_len);
static SdsTableContext* find_table(const char* table_type);
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
    
    /* Set node ID */
    if (config->node_id && config->node_id[0] != '\0') {
        strncpy(_node_id, config->node_id, SDS_MAX_NODE_ID_LEN - 1);
        _node_id[SDS_MAX_NODE_ID_LEN - 1] = '\0';
    } else {
        snprintf(_node_id, SDS_MAX_NODE_ID_LEN, "node_%08x", (unsigned int)sds_platform_millis());
    }
    
    /* Store broker info */
    _mqtt_broker = config->mqtt_broker;
    _mqtt_port = config->mqtt_port ? config->mqtt_port : SDS_DEFAULT_MQTT_PORT;
    
    /* Initialize tables */
    memset(_tables, 0, sizeof(_tables));
    _table_count = 0;
    memset(&_stats, 0, sizeof(_stats));
    
    /* Set MQTT callback */
    sds_platform_mqtt_set_callback(on_mqtt_message);
    
    /* Connect to MQTT */
    if (!sds_platform_mqtt_connect(_mqtt_broker, _mqtt_port, _node_id)) {
        sds_platform_shutdown();
        return SDS_ERR_MQTT_CONNECT_FAILED;
    }
    
    _initialized = true;
    SDS_LOG_I("SDS initialized: node_id=%s", _node_id);
    
    return SDS_OK;
}

void sds_loop(void) {
    if (!_initialized) {
        return;
    }
    
    /* Check MQTT connection */
    if (!sds_platform_mqtt_connected()) {
        SDS_LOG_W("MQTT disconnected, attempting reconnect...");
        if (sds_platform_mqtt_connect(_mqtt_broker, _mqtt_port, _node_id)) {
            _stats.reconnect_count++;
            for (int i = 0; i < SDS_MAX_TABLES; i++) {
                if (_tables[i].active) {
                    subscribe_table_topics(&_tables[i]);
                }
            }
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
    ctx->last_sync_ms = sds_platform_millis();
    
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
        return sds_register_table_ex(
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
    } else {
        /* OWNER role */
        return sds_register_table_ex(
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
    
    ctx->config_offset = config_offset;
    ctx->config_size = config_size < 256 ? config_size : 256;
    ctx->state_offset = state_offset;
    ctx->state_size = state_size < 256 ? state_size : 256;
    ctx->status_offset = status_offset;
    ctx->status_size = status_size < 256 ? status_size : 256;
    
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

/* ============== Statistics ============== */

const SdsStats* sds_get_stats(void) {
    return &_stats;
}

/* ============== Owner Helpers ============== */

const void* sds_find_node_status(const void* owner_table, const char* table_type, const char* node_id) {
    (void)owner_table;
    (void)table_type;
    (void)node_id;
    return NULL;
}

void sds_foreach_node(const void* owner_table, const char* table_type, SdsNodeIterator callback, void* user_data) {
    (void)owner_table;
    (void)table_type;
    (void)callback;
    (void)user_data;
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
    
    if (ctx->role == SDS_ROLE_OWNER) {
        /* Owner publishes config */
        if (ctx->serialize_config && ctx->config_size > 0) {
            void* config_ptr = (uint8_t*)ctx->table + ctx->config_offset;
            
            /* Check if config changed */
            if (memcmp(config_ptr, ctx->shadow_config, ctx->config_size) != 0) {
                sds_json_writer_init(&w, buffer, sizeof(buffer));
                sds_json_start_object(&w);
                sds_json_add_uint(&w, "ts", sds_platform_millis());
                sds_json_add_string(&w, "from", _node_id);
                ctx->serialize_config(config_ptr, &w);  /* Pass section pointer */
                sds_json_end_object(&w);
                
                snprintf(topic, sizeof(topic), "sds/%s/config", ctx->table_type);
                sds_platform_mqtt_publish(topic, (uint8_t*)buffer, sds_json_get_length(&w), true);
                
                memcpy(ctx->shadow_config, config_ptr, ctx->config_size);
                _stats.messages_sent++;
                SDS_LOG_D("Published config: %s", ctx->table_type);
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
            sds_json_add_uint(&w, "ts", sds_platform_millis());
            sds_json_add_string(&w, "node", _node_id);
            ctx->serialize_state(state_ptr, &w);  /* Pass section pointer */
            sds_json_end_object(&w);
            
            snprintf(topic, sizeof(topic), "sds/%s/state", ctx->table_type);
            sds_platform_mqtt_publish(topic, (uint8_t*)buffer, sds_json_get_length(&w), false);
            
            memcpy(ctx->shadow_state, state_ptr, ctx->state_size);
            _stats.messages_sent++;
            SDS_LOG_D("Published state: %s", ctx->table_type);
        }
    }
    
    /* Devices publish status */
    if (ctx->role == SDS_ROLE_DEVICE && ctx->serialize_status && ctx->status_size > 0) {
        void* status_ptr = (uint8_t*)ctx->table + ctx->status_offset;
        
        /* Check if status changed */
        if (memcmp(status_ptr, ctx->shadow_status, ctx->status_size) != 0) {
            sds_json_writer_init(&w, buffer, sizeof(buffer));
            sds_json_start_object(&w);
            sds_json_add_uint(&w, "ts", sds_platform_millis());
            ctx->serialize_status(status_ptr, &w);  /* Pass section pointer */
            sds_json_end_object(&w);
            
            snprintf(topic, sizeof(topic), "sds/%s/status/%s", ctx->table_type, _node_id);
            sds_platform_mqtt_publish(topic, (uint8_t*)buffer, sds_json_get_length(&w), false);
            
            memcpy(ctx->shadow_status, status_ptr, ctx->status_size);
            _stats.messages_sent++;
            SDS_LOG_D("Published status: %s", ctx->table_type);
        }
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

static void handle_status_message(SdsTableContext* ctx, const char* from_node, const uint8_t* payload, size_t len) {
    if (ctx->role != SDS_ROLE_OWNER) return;
    if (!ctx->deserialize_status) return;
    
    /* TODO: Find or create status slot for from_node */
    /* For now, just invoke callback */
    
    SDS_LOG_I("Status received from %s: %s", from_node, ctx->table_type);
    
    if (ctx->status_callback) {
        ctx->status_callback(ctx->table_type, from_node);
    }
    
    (void)payload;
    (void)len;
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
    if (table_len >= SDS_MAX_TABLE_TYPE_LEN) return;
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
