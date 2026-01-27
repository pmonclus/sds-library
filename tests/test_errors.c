/*
 * test_errors.c - Error path and edge case tests
 * 
 * Tests error handling, invalid inputs, and boundary conditions.
 */

#include "sds.h"
#include "sds_json.h"
#include "sds_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============== Test Infrastructure ============== */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing: %s...", #name); \
    test_##name(); \
    printf(" ✓\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

/* ============== Test Table ============== */

typedef struct {
    uint8_t value;
} TestConfig;

typedef struct {
    float data;
} TestState;

typedef struct {
    uint8_t status;
} TestStatus;

typedef struct {
    TestConfig config;
    TestState state;
    TestStatus status;
} TestDeviceTable;

/* Serialization stubs */
static void serialize_config(void* s, SdsJsonWriter* w) {
    TestConfig* c = (TestConfig*)s;
    sds_json_add_uint(w, "value", c->value);
}

static void deserialize_config(void* s, SdsJsonReader* r) {
    TestConfig* c = (TestConfig*)s;
    sds_json_get_uint8_field(r, "value", &c->value);
}

static void serialize_state(void* s, SdsJsonWriter* w) {
    TestState* st = (TestState*)s;
    sds_json_add_float(w, "data", st->data);
}

static void deserialize_state(void* s, SdsJsonReader* r) {
    TestState* st = (TestState*)s;
    sds_json_get_float_field(r, "data", &st->data);
}

static void serialize_status(void* s, SdsJsonWriter* w) {
    TestStatus* st = (TestStatus*)s;
    sds_json_add_uint(w, "status", st->status);
}

static void deserialize_status(void* s, SdsJsonReader* r) {
    TestStatus* st = (TestStatus*)s;
    sds_json_get_uint8_field(r, "status", &st->status);
}

/* ============== Initialization Error Tests ============== */

TEST(init_null_config) {
    SdsError err = sds_init(NULL);
    ASSERT_EQ(err, SDS_ERR_INVALID_CONFIG);
}

TEST(init_null_broker) {
    SdsConfig config = {
        .node_id = "test_node",
        .mqtt_broker = NULL,
        .mqtt_port = 1883
    };
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_INVALID_CONFIG);
}

TEST(init_empty_broker) {
    SdsConfig config = {
        .node_id = "test_node",
        .mqtt_broker = "",
        .mqtt_port = 1883
    };
    /* Empty broker should fail during connect, not validation */
    /* The behavior depends on platform */
}

TEST(init_broker_too_long) {
    /* Create a broker string > 128 chars */
    char long_broker[256];
    memset(long_broker, 'a', 200);
    long_broker[200] = '\0';
    
    SdsConfig config = {
        .node_id = "test_node",
        .mqtt_broker = long_broker,
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_INVALID_CONFIG);
}

TEST(init_node_id_too_long) {
    /* Create a node_id string > 32 chars */
    char long_node_id[64];
    memset(long_node_id, 'n', 50);
    long_node_id[50] = '\0';
    
    SdsConfig config = {
        .node_id = long_node_id,
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_INVALID_CONFIG);
}

TEST(init_double_init) {
    SdsConfig config = {
        .node_id = "test_double",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_OK);
    
    /* Second init should fail */
    err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_ALREADY_INITIALIZED);
    
    sds_shutdown();
}

/* ============== Table Registration Error Tests ============== */

TEST(register_before_init) {
    TestDeviceTable table = {0};
    SdsError err = sds_register_table(&table, "TestTable", SDS_ROLE_DEVICE, NULL);
    /* Should return error because not initialized */
    ASSERT_EQ(err, SDS_ERR_NOT_INITIALIZED);
}

TEST(register_null_table) {
    SdsConfig config = {
        .node_id = "test_reg",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    SdsError err = sds_register_table(NULL, "TestTable", SDS_ROLE_DEVICE, NULL);
    ASSERT_EQ(err, SDS_ERR_INVALID_TABLE);
    
    sds_shutdown();
}

TEST(register_null_type) {
    SdsConfig config = {
        .node_id = "test_reg2",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    TestDeviceTable table = {0};
    SdsError err = sds_register_table(&table, NULL, SDS_ROLE_DEVICE, NULL);
    ASSERT_EQ(err, SDS_ERR_INVALID_TABLE);
    
    sds_shutdown();
}

TEST(register_unknown_type) {
    SdsConfig config = {
        .node_id = "test_reg3",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    TestDeviceTable table = {0};
    SdsError err = sds_register_table(&table, "NonExistentTable", SDS_ROLE_DEVICE, NULL);
    ASSERT_EQ(err, SDS_ERR_TABLE_NOT_FOUND);
    
    sds_shutdown();
}

TEST(register_invalid_role) {
    SdsConfig config = {
        .node_id = "test_role",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    TestDeviceTable table = {0};
    SdsTableOptions opts = { .sync_interval_ms = 1000 };
    
    /* Use an invalid role value */
    SdsError err = sds_register_table_ex(
        &table, "TestTable", (SdsRole)99, &opts,
        offsetof(TestDeviceTable, config), sizeof(TestConfig),
        offsetof(TestDeviceTable, state), sizeof(TestState),
        offsetof(TestDeviceTable, status), sizeof(TestStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    
    ASSERT_EQ(err, SDS_ERR_INVALID_ROLE);
    
    sds_shutdown();
}

TEST(register_duplicate) {
    SdsConfig config = {
        .node_id = "test_dup",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    TestDeviceTable table1 = {0};
    TestDeviceTable table2 = {0};
    SdsTableOptions opts = { .sync_interval_ms = 1000 };
    
    SdsError err = sds_register_table_ex(
        &table1, "TestTable", SDS_ROLE_DEVICE, &opts,
        offsetof(TestDeviceTable, config), sizeof(TestConfig),
        offsetof(TestDeviceTable, state), sizeof(TestState),
        offsetof(TestDeviceTable, status), sizeof(TestStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    ASSERT_EQ(err, SDS_OK);
    
    /* Register same type again */
    err = sds_register_table_ex(
        &table2, "TestTable", SDS_ROLE_DEVICE, &opts,
        offsetof(TestDeviceTable, config), sizeof(TestConfig),
        offsetof(TestDeviceTable, state), sizeof(TestState),
        offsetof(TestDeviceTable, status), sizeof(TestStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    ASSERT_EQ(err, SDS_ERR_TABLE_ALREADY_REGISTERED);
    
    sds_shutdown();
}

/* ============== Error String Tests ============== */

TEST(error_strings) {
    ASSERT_STR_EQ(sds_error_string(SDS_OK), "OK");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_NOT_INITIALIZED), "Not initialized");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_ALREADY_INITIALIZED), "Already initialized");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_INVALID_CONFIG), "Invalid configuration");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_MQTT_CONNECT_FAILED), "MQTT connect failed");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_MQTT_DISCONNECTED), "MQTT disconnected");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_TABLE_NOT_FOUND), "Table not found");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_TABLE_ALREADY_REGISTERED), "Table already registered");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_MAX_TABLES_REACHED), "Max tables reached");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_INVALID_TABLE), "Invalid table");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_INVALID_ROLE), "Invalid role");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_BUFFER_FULL), "Buffer full");
    ASSERT_STR_EQ(sds_error_string(SDS_ERR_SECTION_TOO_LARGE), "Section too large for shadow buffer");
    
    /* Unknown error code */
    ASSERT_STR_EQ(sds_error_string((SdsError)999), "Unknown error");
}

/* ============== API State Tests ============== */

TEST(is_ready_before_init) {
    ASSERT_EQ(sds_is_ready(), false);
}

TEST(loop_before_init) {
    /* Should not crash, just return */
    sds_loop();
}

TEST(shutdown_before_init) {
    /* Should not crash */
    sds_shutdown();
}

TEST(get_node_id_before_init) {
    const char* id = sds_get_node_id();
    ASSERT(id == NULL || id[0] == '\0');
}

/* ============== Error Callback Tests ============== */

static int g_error_callback_count = 0;
static SdsError g_last_error = SDS_OK;
static char g_last_context[256] = "";

static void error_callback(SdsError error, const char* context) {
    g_error_callback_count++;
    g_last_error = error;
    if (context) {
        strncpy(g_last_context, context, sizeof(g_last_context) - 1);
    }
}

TEST(error_callback_registration) {
    g_error_callback_count = 0;
    
    sds_on_error(error_callback);
    
    /* The callback should be stored */
    /* Note: We can't easily trigger an error from here without more setup */
    
    sds_on_error(NULL);  /* Reset */
}

/* ============== Boundary Tests ============== */

TEST(max_tables) {
    SdsConfig config = {
        .node_id = "test_max",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    /* Register SDS_MAX_TABLES tables */
    TestDeviceTable tables[SDS_MAX_TABLES + 1];
    char type_name[32];
    SdsTableOptions opts = { .sync_interval_ms = 1000 };
    
    for (int i = 0; i < SDS_MAX_TABLES; i++) {
        snprintf(type_name, sizeof(type_name), "Table%d", i);
        SdsError err = sds_register_table_ex(
            &tables[i], type_name, SDS_ROLE_DEVICE, &opts,
            offsetof(TestDeviceTable, config), sizeof(TestConfig),
            offsetof(TestDeviceTable, state), sizeof(TestState),
            offsetof(TestDeviceTable, status), sizeof(TestStatus),
            serialize_config, deserialize_config,
            serialize_state, deserialize_state,
            serialize_status, deserialize_status
        );
        ASSERT_EQ(err, SDS_OK);
    }
    
    /* One more should fail */
    snprintf(type_name, sizeof(type_name), "TableOverflow");
    SdsError err = sds_register_table_ex(
        &tables[SDS_MAX_TABLES], type_name, SDS_ROLE_DEVICE, &opts,
        offsetof(TestDeviceTable, config), sizeof(TestConfig),
        offsetof(TestDeviceTable, state), sizeof(TestState),
        offsetof(TestDeviceTable, status), sizeof(TestStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    ASSERT_EQ(err, SDS_ERR_MAX_TABLES_REACHED);
    
    sds_shutdown();
}

/* ============== Stats Tests ============== */

TEST(stats_after_init) {
    SdsConfig config = {
        .node_id = "test_stats",
        .mqtt_broker = "localhost",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    const SdsStats* stats = sds_get_stats();
    ASSERT(stats != NULL);
    ASSERT_EQ(stats->reconnect_count, 0);
    
    sds_shutdown();
}

/* ============== Main ============== */

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              SDS Error Path Tests                        ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    
    printf("--- Initialization Error Tests ---\n");
    RUN_TEST(init_null_config);
    RUN_TEST(init_null_broker);
    RUN_TEST(init_broker_too_long);
    RUN_TEST(init_node_id_too_long);
    RUN_TEST(init_double_init);
    
    printf("\n--- Table Registration Error Tests ---\n");
    RUN_TEST(register_before_init);
    RUN_TEST(register_null_table);
    RUN_TEST(register_null_type);
    RUN_TEST(register_unknown_type);
    RUN_TEST(register_invalid_role);
    RUN_TEST(register_duplicate);
    
    printf("\n--- Error String Tests ---\n");
    RUN_TEST(error_strings);
    
    printf("\n--- API State Tests ---\n");
    RUN_TEST(is_ready_before_init);
    RUN_TEST(loop_before_init);
    RUN_TEST(shutdown_before_init);
    RUN_TEST(get_node_id_before_init);
    
    printf("\n--- Error Callback Tests ---\n");
    RUN_TEST(error_callback_registration);
    
    printf("\n--- Boundary Tests ---\n");
    RUN_TEST(max_tables);
    
    printf("\n--- Stats Tests ---\n");
    RUN_TEST(stats_after_init);
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return tests_failed > 0 ? 1 : 0;
}

