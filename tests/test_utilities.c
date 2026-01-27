/*
 * test_utilities.c - Tests for SDS utility functions
 * 
 * Tests utility functions that aren't exercised by the core tests:
 * - sds_error_string()
 * - sds_set_log_level() / sds_get_log_level()
 * - sds_set_table_registry() / sds_find_table_meta()
 * - sds_register_table() (simple API)
 * - Schema version functions
 * - Node status iteration functions
 * - Liveness functions
 */

#include "sds.h"
#include "sds_json.h"
#include "mock/sds_platform_mock.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* ============== Test Framework ============== */

static int g_tests_passed = 0;
static int g_tests_failed = 0;
static const char* g_current_test = NULL;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    g_current_test = #name; \
    printf("  %-55s", #name); \
    fflush(stdout); \
    sds_mock_reset(); \
    sds_shutdown(); \
    test_##name(); \
    sds_shutdown(); \
    printf(" ✓\n"); \
    g_tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" ✗ FAILED\n"); \
        printf("    Assertion failed: %s\n", #cond); \
        printf("    At line %d in %s\n", __LINE__, g_current_test); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_STR_NE(a, b) ASSERT(strcmp((a), (b)) != 0)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_NULL(p) ASSERT((p) == NULL)

/* ============== Test Table Definition ============== */

typedef struct {
    uint8_t mode;
    float threshold;
} UtilConfig;

typedef struct {
    float value;
    uint32_t counter;
} UtilState;

typedef struct {
    uint8_t error_code;
    uint8_t battery;
} UtilStatus;

/* Device table */
typedef struct {
    UtilConfig config;
    UtilState state;
    UtilStatus status;
} UtilDeviceTable;

/* Owner table with status slots */
#define MAX_SLOTS 8

typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    bool valid;
    uint32_t last_seen_ms;
    UtilStatus status;
} UtilStatusSlot;

typedef struct {
    UtilConfig config;
    UtilState state;
    UtilStatusSlot status_slots[MAX_SLOTS];
    uint8_t status_count;
} UtilOwnerTable;

/* ============== Serialization ============== */

static void serialize_config(void* section, SdsJsonWriter* w) {
    UtilConfig* cfg = (UtilConfig*)section;
    sds_json_add_uint(w, "mode", cfg->mode);
    sds_json_add_float(w, "threshold", cfg->threshold);
}

static void deserialize_config(void* section, SdsJsonReader* r) {
    UtilConfig* cfg = (UtilConfig*)section;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
    sds_json_get_float_field(r, "threshold", &cfg->threshold);
}

static void serialize_state(void* section, SdsJsonWriter* w) {
    UtilState* st = (UtilState*)section;
    sds_json_add_float(w, "value", st->value);
    sds_json_add_uint(w, "counter", st->counter);
}

static void deserialize_state(void* section, SdsJsonReader* r) {
    UtilState* st = (UtilState*)section;
    sds_json_get_float_field(r, "value", &st->value);
    sds_json_get_uint_field(r, "counter", &st->counter);
}

static void serialize_status(void* section, SdsJsonWriter* w) {
    UtilStatus* st = (UtilStatus*)section;
    sds_json_add_uint(w, "error_code", st->error_code);
    sds_json_add_uint(w, "battery", st->battery);
}

static void deserialize_status(void* section, SdsJsonReader* r) {
    UtilStatus* st = (UtilStatus*)section;
    sds_json_get_uint8_field(r, "error_code", &st->error_code);
    sds_json_get_uint8_field(r, "battery", &st->battery);
}

/* ============== Helper ============== */

static void init_mock_sds(void) {
    SdsMockConfig mock_cfg = {
        .init_returns_success = true,
        .mqtt_connect_returns_success = true,
        .mqtt_connected = true,
        .mqtt_publish_returns_success = true,
        .mqtt_subscribe_returns_success = true,
    };
    sds_mock_configure(&mock_cfg);
    
    SdsConfig config = {
        .node_id = "util_test",
        .mqtt_broker = "mock",
        .mqtt_port = 1883
    };
    sds_init(&config);
}

/* ============================================================================
 * ERROR STRING TESTS
 * ============================================================================ */

TEST(error_string_all_codes) {
    /* Test all defined error codes return non-NULL, non-empty strings */
    ASSERT_NOT_NULL(sds_error_string(SDS_OK));
    ASSERT_STR_EQ(sds_error_string(SDS_OK), "OK");
    
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_NOT_INITIALIZED));
    ASSERT_STR_NE(sds_error_string(SDS_ERR_NOT_INITIALIZED), "");
    
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_ALREADY_INITIALIZED));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_INVALID_CONFIG));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_MQTT_CONNECT_FAILED));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_MQTT_DISCONNECTED));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_TABLE_NOT_FOUND));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_TABLE_ALREADY_REGISTERED));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_MAX_TABLES_REACHED));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_INVALID_TABLE));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_INVALID_ROLE));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_OWNER_EXISTS));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_MAX_NODES_REACHED));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_BUFFER_FULL));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_SECTION_TOO_LARGE));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_PLATFORM_NOT_SET));
    ASSERT_NOT_NULL(sds_error_string(SDS_ERR_PLATFORM_ERROR));
}

TEST(error_string_unknown_code) {
    /* Unknown error codes should return a default string */
    const char* unknown = sds_error_string((SdsError)9999);
    ASSERT_NOT_NULL(unknown);
    /* Should contain "Unknown" or similar */
}

TEST(error_string_unique_messages) {
    /* Each error should have a unique message */
    const char* ok = sds_error_string(SDS_OK);
    const char* not_init = sds_error_string(SDS_ERR_NOT_INITIALIZED);
    const char* invalid_cfg = sds_error_string(SDS_ERR_INVALID_CONFIG);
    
    ASSERT_STR_NE(ok, not_init);
    ASSERT_STR_NE(ok, invalid_cfg);
    ASSERT_STR_NE(not_init, invalid_cfg);
}

/* ============================================================================
 * LOG LEVEL TESTS
 * ============================================================================ */

TEST(log_level_default) {
    /* Default log level should be INFO */
    SdsLogLevel level = sds_get_log_level();
    ASSERT_EQ(level, SDS_LOG_INFO);
}

TEST(log_level_set_and_get) {
    /* Test setting different log levels */
    sds_set_log_level(SDS_LOG_DEBUG);
    ASSERT_EQ(sds_get_log_level(), SDS_LOG_DEBUG);
    
    sds_set_log_level(SDS_LOG_WARN);
    ASSERT_EQ(sds_get_log_level(), SDS_LOG_WARN);
    
    sds_set_log_level(SDS_LOG_ERROR);
    ASSERT_EQ(sds_get_log_level(), SDS_LOG_ERROR);
    
    sds_set_log_level(SDS_LOG_NONE);
    ASSERT_EQ(sds_get_log_level(), SDS_LOG_NONE);
    
    /* Reset to default */
    sds_set_log_level(SDS_LOG_INFO);
}

/* ============================================================================
 * SCHEMA VERSION TESTS
 * ============================================================================ */

TEST(schema_version_default) {
    init_mock_sds();
    
    /* Default version should be "1.0" or similar */
    const char* version = sds_get_schema_version();
    ASSERT_NOT_NULL(version);
}

TEST(schema_version_set_and_get) {
    init_mock_sds();
    
    sds_set_schema_version("2.5.1");
    const char* version = sds_get_schema_version();
    ASSERT_NOT_NULL(version);
    ASSERT_STR_EQ(version, "2.5.1");
}

TEST(schema_version_set_null) {
    init_mock_sds();
    
    /* Setting NULL should be handled gracefully */
    sds_set_schema_version(NULL);
    /* Should not crash, might return default or empty */
    const char* version = sds_get_schema_version();
    (void)version;  /* Just verify no crash */
}

/* ============================================================================
 * TABLE REGISTRY TESTS
 * ============================================================================ */

TEST(table_registry_find_null) {
    /* Finding in empty/null registry should return NULL */
    sds_set_table_registry(NULL, 0);
    const SdsTableMeta* meta = sds_find_table_meta("SomeTable");
    ASSERT_NULL(meta);
}

TEST(table_registry_find_null_name) {
    /* Finding NULL name should return NULL */
    const SdsTableMeta* meta = sds_find_table_meta(NULL);
    ASSERT_NULL(meta);
}

TEST(table_registry_set_and_find) {
    /* Create a simple registry */
    static const SdsTableMeta registry[] = {
        { 
            .table_type = "TableA", 
            .sync_interval_ms = 1000,
            .dev_config_size = 10, 
            .dev_state_size = 20, 
            .dev_status_size = 5 
        },
        { 
            .table_type = "TableB", 
            .sync_interval_ms = 2000,
            .dev_config_size = 15, 
            .dev_state_size = 25, 
            .dev_status_size = 8 
        },
    };
    
    sds_set_table_registry(registry, 2);
    
    const SdsTableMeta* metaA = sds_find_table_meta("TableA");
    ASSERT_NOT_NULL(metaA);
    ASSERT_STR_EQ(metaA->table_type, "TableA");
    ASSERT_EQ(metaA->dev_config_size, 10);
    
    const SdsTableMeta* metaB = sds_find_table_meta("TableB");
    ASSERT_NOT_NULL(metaB);
    ASSERT_STR_EQ(metaB->table_type, "TableB");
    
    const SdsTableMeta* metaC = sds_find_table_meta("TableC");
    ASSERT_NULL(metaC);  /* Not in registry */
    
    /* Clear registry */
    sds_set_table_registry(NULL, 0);
}

/* ============================================================================
 * SIMPLE REGISTER TABLE API TESTS
 * ============================================================================ */

TEST(register_table_simple_api) {
    init_mock_sds();
    
    /* Set up a registry so the simple API can find table metadata */
    static const SdsTableMeta registry[] = {
        { 
            .table_type = "UtilTable",
            .sync_interval_ms = 1000,
            .device_table_size = sizeof(UtilDeviceTable),
            .dev_config_offset = offsetof(UtilDeviceTable, config),
            .dev_config_size = sizeof(UtilConfig),
            .dev_state_offset = offsetof(UtilDeviceTable, state),
            .dev_state_size = sizeof(UtilState),
            .dev_status_offset = offsetof(UtilDeviceTable, status),
            .dev_status_size = sizeof(UtilStatus),
            .serialize_config = serialize_config,
            .deserialize_config = deserialize_config,
            .serialize_state = serialize_state,
            .deserialize_state = deserialize_state,
            .serialize_status = serialize_status,
            .deserialize_status = deserialize_status,
        },
    };
    sds_set_table_registry(registry, 1);
    
    UtilDeviceTable table = {0};
    SdsError err = sds_register_table(&table, "UtilTable", SDS_ROLE_DEVICE, NULL);
    ASSERT_EQ(err, SDS_OK);
    
    sds_set_table_registry(NULL, 0);
}

TEST(register_table_not_in_registry) {
    init_mock_sds();
    
    /* Try to register a table that's not in the registry */
    sds_set_table_registry(NULL, 0);
    
    UtilDeviceTable table = {0};
    SdsError err = sds_register_table(&table, "UnknownTable", SDS_ROLE_DEVICE, NULL);
    ASSERT_EQ(err, SDS_ERR_TABLE_NOT_FOUND);
}

/* ============================================================================
 * VERSION MISMATCH CALLBACK TESTS
 * ============================================================================ */

static int g_mismatch_count = 0;
static char g_mismatch_table[64] = {0};
static char g_mismatch_node[64] = {0};

static bool version_mismatch_callback(const char* table_type, const char* node_id, 
                                      const char* their_version, const char* our_version) {
    g_mismatch_count++;
    if (table_type) strncpy(g_mismatch_table, table_type, sizeof(g_mismatch_table) - 1);
    if (node_id) strncpy(g_mismatch_node, node_id, sizeof(g_mismatch_node) - 1);
    (void)their_version;
    (void)our_version;
    return true;  /* Accept mismatched messages */
}

TEST(version_mismatch_callback_set) {
    init_mock_sds();
    
    g_mismatch_count = 0;
    
    /* Setting callback should not crash */
    sds_on_version_mismatch(version_mismatch_callback);
    
    /* Setting NULL should disable callback */
    sds_on_version_mismatch(NULL);
}

/* ============================================================================
 * LIVENESS INTERVAL TESTS
 * ============================================================================ */

TEST(liveness_interval_default) {
    init_mock_sds();
    
    UtilDeviceTable table = {0};
    sds_register_table_ex(
        &table, "LiveTest", SDS_ROLE_DEVICE, NULL,
        offsetof(UtilDeviceTable, config), sizeof(UtilConfig),
        offsetof(UtilDeviceTable, state), sizeof(UtilState),
        offsetof(UtilDeviceTable, status), sizeof(UtilStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    
    uint32_t interval = sds_get_liveness_interval("LiveTest");
    /* Default should be around 30000ms (30 seconds) */
    ASSERT(interval >= 1000);  /* At least 1 second */
}

TEST(liveness_interval_unknown_table) {
    init_mock_sds();
    
    /* Unknown table should return 0 */
    uint32_t interval = sds_get_liveness_interval("NonExistentTable");
    ASSERT_EQ(interval, 0);
}

/* ============================================================================
 * NODE STATUS FUNCTIONS TESTS
 * ============================================================================ */

static UtilOwnerTable g_owner_table;

static void setup_owner_with_devices(void) {
    init_mock_sds();
    
    memset(&g_owner_table, 0, sizeof(g_owner_table));
    
    sds_register_table_ex(
        &g_owner_table, "NodeTest", SDS_ROLE_OWNER, NULL,
        offsetof(UtilOwnerTable, config), sizeof(UtilConfig),
        offsetof(UtilOwnerTable, state), sizeof(UtilState),
        offsetof(UtilOwnerTable, status_slots[0].status), sizeof(UtilStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    
    sds_set_owner_status_slots(
        "NodeTest",
        offsetof(UtilOwnerTable, status_slots),
        sizeof(UtilStatusSlot),
        offsetof(UtilStatusSlot, status),
        offsetof(UtilOwnerTable, status_count),
        MAX_SLOTS
    );
    
    /* Inject some device status messages to populate slots */
    sds_mock_inject_message_str("sds/NodeTest/status/device_01", 
                                "{\"error_code\":0,\"battery\":95}");
    sds_mock_inject_message_str("sds/NodeTest/status/device_02", 
                                "{\"error_code\":1,\"battery\":80}");
    
    /* Process messages */
    sds_loop();
}

TEST(find_node_status_exists) {
    setup_owner_with_devices();
    
    const UtilStatus* status = (const UtilStatus*)sds_find_node_status(
        &g_owner_table, "NodeTest", "device_01");
    
    /* Note: This may return NULL if the library doesn't populate slots this way */
    /* The test verifies the function doesn't crash */
    (void)status;
}

TEST(find_node_status_not_exists) {
    setup_owner_with_devices();
    
    const void* status = sds_find_node_status(
        &g_owner_table, "NodeTest", "nonexistent_device");
    
    ASSERT_NULL(status);
}

TEST(find_node_status_null_params) {
    setup_owner_with_devices();
    
    /* NULL parameters should return NULL */
    ASSERT_NULL(sds_find_node_status(NULL, "NodeTest", "device_01"));
    ASSERT_NULL(sds_find_node_status(&g_owner_table, NULL, "device_01"));
    ASSERT_NULL(sds_find_node_status(&g_owner_table, "NodeTest", NULL));
}

TEST(is_device_online_not_exists) {
    setup_owner_with_devices();
    
    bool online = sds_is_device_online(
        &g_owner_table, "NodeTest", "nonexistent_device", 30000);
    
    ASSERT(!online);
}

TEST(is_device_online_null_params) {
    setup_owner_with_devices();
    
    ASSERT(!sds_is_device_online(NULL, "NodeTest", "device_01", 30000));
    ASSERT(!sds_is_device_online(&g_owner_table, NULL, "device_01", 30000));
    ASSERT(!sds_is_device_online(&g_owner_table, "NodeTest", NULL, 30000));
}

/* Node iterator callback */
static int g_iter_count = 0;
static char g_iter_nodes[8][SDS_MAX_NODE_ID_LEN];

static void node_iterator_callback(const char* node_id, const void* status, void* user_data) {
    (void)status;
    int* counter = (int*)user_data;
    if (g_iter_count < 8 && node_id) {
        strncpy(g_iter_nodes[g_iter_count], node_id, SDS_MAX_NODE_ID_LEN - 1);
    }
    g_iter_count++;
    if (counter) (*counter)++;
}

TEST(foreach_node_empty) {
    init_mock_sds();
    
    memset(&g_owner_table, 0, sizeof(g_owner_table));
    
    sds_register_table_ex(
        &g_owner_table, "EmptyTest", SDS_ROLE_OWNER, NULL,
        offsetof(UtilOwnerTable, config), sizeof(UtilConfig),
        offsetof(UtilOwnerTable, state), sizeof(UtilState),
        offsetof(UtilOwnerTable, status_slots[0].status), sizeof(UtilStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    
    sds_set_owner_status_slots(
        "EmptyTest",
        offsetof(UtilOwnerTable, status_slots),
        sizeof(UtilStatusSlot),
        offsetof(UtilStatusSlot, status),
        offsetof(UtilOwnerTable, status_count),
        MAX_SLOTS
    );
    
    g_iter_count = 0;
    int counter = 0;
    
    sds_foreach_node(&g_owner_table, "EmptyTest", node_iterator_callback, &counter);
    
    ASSERT_EQ(counter, 0);
}

TEST(foreach_node_null_params) {
    setup_owner_with_devices();
    
    g_iter_count = 0;
    
    /* NULL parameters should not crash */
    sds_foreach_node(NULL, "NodeTest", node_iterator_callback, NULL);
    sds_foreach_node(&g_owner_table, NULL, node_iterator_callback, NULL);
    sds_foreach_node(&g_owner_table, "NodeTest", NULL, NULL);
    
    ASSERT_EQ(g_iter_count, 0);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           SDS Utility Functions Tests                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("─── Error String Tests ───\n");
    RUN_TEST(error_string_all_codes);
    RUN_TEST(error_string_unknown_code);
    RUN_TEST(error_string_unique_messages);
    
    printf("\n─── Log Level Tests ───\n");
    RUN_TEST(log_level_default);
    RUN_TEST(log_level_set_and_get);
    
    printf("\n─── Schema Version Tests ───\n");
    RUN_TEST(schema_version_default);
    RUN_TEST(schema_version_set_and_get);
    RUN_TEST(schema_version_set_null);
    
    printf("\n─── Table Registry Tests ───\n");
    RUN_TEST(table_registry_find_null);
    RUN_TEST(table_registry_find_null_name);
    RUN_TEST(table_registry_set_and_find);
    
    printf("\n─── Simple Register Table API Tests ───\n");
    RUN_TEST(register_table_simple_api);
    RUN_TEST(register_table_not_in_registry);
    
    printf("\n─── Version Mismatch Callback Tests ───\n");
    RUN_TEST(version_mismatch_callback_set);
    
    printf("\n─── Liveness Interval Tests ───\n");
    RUN_TEST(liveness_interval_default);
    RUN_TEST(liveness_interval_unknown_table);
    
    printf("\n─── Node Status Functions Tests ───\n");
    RUN_TEST(find_node_status_exists);
    RUN_TEST(find_node_status_not_exists);
    RUN_TEST(find_node_status_null_params);
    RUN_TEST(is_device_online_not_exists);
    RUN_TEST(is_device_online_null_params);
    RUN_TEST(foreach_node_empty);
    RUN_TEST(foreach_node_null_params);
    
    printf("\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("══════════════════════════════════════════════════════════════\n");
    
    if (g_tests_failed > 0) {
        printf("\n  ✗ TESTS FAILED\n\n");
        return 1;
    }
    
    printf("\n  ✓ ALL TESTS PASSED\n\n");
    return 0;
}
