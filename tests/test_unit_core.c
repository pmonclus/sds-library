/*
 * test_unit_core.c - Unit Tests for SDS Core using Mock Platform
 * 
 * These tests run without a real MQTT broker by using the mock platform.
 * They provide fast, deterministic testing of core SDS functionality.
 * 
 * Build:
 *   gcc -I../include -o test_unit_core test_unit_core.c \
 *       mock/sds_platform_mock.c ../src/sds_core.c ../src/sds_json.c -lm
 * 
 * Run:
 *   ./test_unit_core
 */

#include "sds.h"
#include "sds_json.h"
#include "mock/sds_platform_mock.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>

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
    sds_shutdown(); /* Ensure clean state */ \
    test_##name(); \
    sds_shutdown(); /* Clean up */ \
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
#define ASSERT_STR_CONTAINS(haystack, needle) ASSERT(strstr((haystack), (needle)) != NULL)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf(" ✗ FAILED\n"); \
        printf("    Float mismatch: %f != %f (eps=%f)\n", (double)(a), (double)(b), (double)(eps)); \
        printf("    At line %d in %s\n", __LINE__, g_current_test); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

/* ============== Test Table Definition ============== */

typedef struct {
    uint8_t mode;
    float threshold;
} TestConfig;

typedef struct {
    float temperature;
    float humidity;
    uint32_t reading_count;
} TestState;

typedef struct {
    uint8_t error_code;
    uint8_t battery_level;
} TestStatus;

typedef struct {
    TestConfig config;
    TestState state;
    TestStatus status;
} TestDeviceTable;

/* Status slot for owner
 * The sds_core.c code expects these fields in this order:
 *   - node_id[SDS_MAX_NODE_ID_LEN] at offset 0
 *   - valid (bool) at offset SDS_MAX_NODE_ID_LEN
 *   - Additional fields are user-defined
 * The slot_status_offset tells the core where the status struct begins
 */
typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];  /* Required: at offset 0 */
    bool valid;                          /* Required: at offset SDS_MAX_NODE_ID_LEN */
    uint32_t last_seen_ms;               /* User field */
    TestStatus status;                   /* User field: status data */
} TestStatusSlot;

#define TEST_MAX_NODES 4

typedef struct {
    TestConfig config;
    TestState state;
    TestStatusSlot status_slots[TEST_MAX_NODES];
    uint8_t status_count;
} TestOwnerTable;

/* ============== Serialization Functions ============== */

static void serialize_test_config(void* section, SdsJsonWriter* w) {
    TestConfig* cfg = (TestConfig*)section;
    sds_json_add_uint(w, "mode", cfg->mode);
    sds_json_add_float(w, "threshold", cfg->threshold);
}

static void deserialize_test_config(void* section, SdsJsonReader* r) {
    TestConfig* cfg = (TestConfig*)section;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
    sds_json_get_float_field(r, "threshold", &cfg->threshold);
}

static void serialize_test_state(void* section, SdsJsonWriter* w) {
    TestState* st = (TestState*)section;
    sds_json_add_float(w, "temperature", st->temperature);
    sds_json_add_float(w, "humidity", st->humidity);
    sds_json_add_uint(w, "reading_count", st->reading_count);
}

static void deserialize_test_state(void* section, SdsJsonReader* r) {
    TestState* st = (TestState*)section;
    sds_json_get_float_field(r, "temperature", &st->temperature);
    sds_json_get_float_field(r, "humidity", &st->humidity);
    sds_json_get_uint_field(r, "reading_count", &st->reading_count);
}

static void serialize_test_status(void* section, SdsJsonWriter* w) {
    TestStatus* st = (TestStatus*)section;
    sds_json_add_uint(w, "error_code", st->error_code);
    sds_json_add_uint(w, "battery_level", st->battery_level);
}

static void deserialize_test_status(void* section, SdsJsonReader* r) {
    TestStatus* st = (TestStatus*)section;
    sds_json_get_uint8_field(r, "error_code", &st->error_code);
    sds_json_get_uint8_field(r, "battery_level", &st->battery_level);
}

/* ============== Helper Functions ============== */

static SdsError init_sds_with_mock(const char* node_id) {
    SdsMockConfig mock_cfg = {
        .init_returns_success = true,
        .mqtt_connect_returns_success = true,
        .mqtt_connected = true,
        .mqtt_publish_returns_success = true,
        .mqtt_subscribe_returns_success = true,
    };
    sds_mock_configure(&mock_cfg);
    
    SdsConfig config = {
        .node_id = node_id,
        .mqtt_broker = "mock_broker",
        .mqtt_port = 1883
    };
    
    return sds_init(&config);
}

static SdsError register_device_table(TestDeviceTable* table, const char* type) {
    return sds_register_table_ex(
        table, type, SDS_ROLE_DEVICE, NULL,
        offsetof(TestDeviceTable, config), sizeof(TestConfig),
        offsetof(TestDeviceTable, state), sizeof(TestState),
        offsetof(TestDeviceTable, status), sizeof(TestStatus),
        NULL, deserialize_test_config,
        serialize_test_state, NULL,
        serialize_test_status, NULL
    );
}

static SdsError register_owner_table(TestOwnerTable* table, const char* type) {
    SdsError err = sds_register_table_ex(
        table, type, SDS_ROLE_OWNER, NULL,
        offsetof(TestOwnerTable, config), sizeof(TestConfig),
        offsetof(TestOwnerTable, state), sizeof(TestState),
        0, 0,  /* Owner doesn't send status */
        serialize_test_config, NULL,
        NULL, deserialize_test_state,
        NULL, deserialize_test_status
    );
    
    if (err == SDS_OK) {
        sds_set_owner_status_slots(
            type,
            offsetof(TestOwnerTable, status_slots),
            sizeof(TestStatusSlot),
            offsetof(TestStatusSlot, status),
            offsetof(TestOwnerTable, status_count),
            TEST_MAX_NODES
        );
    }
    
    return err;
}

/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================ */

TEST(init_success) {
    SdsError err = init_sds_with_mock("test_node");
    ASSERT_EQ(err, SDS_OK);
    ASSERT(sds_is_ready());
    ASSERT_STR_EQ(sds_get_node_id(), "test_node");
}

TEST(init_double_init_fails) {
    SdsError err = init_sds_with_mock("test_node");
    ASSERT_EQ(err, SDS_OK);
    
    SdsConfig config = { .node_id = "second", .mqtt_broker = "broker" };
    err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_ALREADY_INITIALIZED);
}

TEST(init_null_config_fails) {
    SdsError err = sds_init(NULL);
    ASSERT_EQ(err, SDS_ERR_INVALID_CONFIG);
}

TEST(init_null_broker_fails) {
    SdsConfig config = { .node_id = "test", .mqtt_broker = NULL };
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_INVALID_CONFIG);
}

TEST(init_platform_failure) {
    SdsMockConfig mock_cfg = {
        .init_returns_success = false,
    };
    sds_mock_configure(&mock_cfg);
    
    SdsConfig config = { .node_id = "test", .mqtt_broker = "broker" };
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_PLATFORM_ERROR);
}

TEST(init_mqtt_connect_failure) {
    SdsMockConfig mock_cfg = {
        .init_returns_success = true,
        .mqtt_connect_returns_success = false,
    };
    sds_mock_configure(&mock_cfg);
    
    SdsConfig config = { .node_id = "test", .mqtt_broker = "broker" };
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_ERR_MQTT_CONNECT_FAILED);
}

TEST(init_auto_generates_node_id) {
    SdsMockConfig mock_cfg = {
        .init_returns_success = true,
        .mqtt_connect_returns_success = true,
        .mqtt_connected = true,
    };
    sds_mock_configure(&mock_cfg);
    
    SdsConfig config = { .node_id = NULL, .mqtt_broker = "broker" };
    SdsError err = sds_init(&config);
    ASSERT_EQ(err, SDS_OK);
    
    const char* node_id = sds_get_node_id();
    ASSERT(node_id != NULL);
    ASSERT(strlen(node_id) > 0);
    ASSERT(strncmp(node_id, "node_", 5) == 0);
}

TEST(shutdown_cleans_up) {
    init_sds_with_mock("test_node");
    ASSERT(sds_is_ready());
    
    sds_shutdown();
    ASSERT(!sds_is_ready());
}

/* ============================================================================
 * TABLE REGISTRATION TESTS
 * ============================================================================ */

TEST(register_device_table_success) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    SdsError err = register_device_table(&table, "TestTable");
    ASSERT_EQ(err, SDS_OK);
    ASSERT_EQ(sds_get_table_count(), 1);
}

TEST(register_owner_table_success) {
    init_sds_with_mock("test_node");
    
    TestOwnerTable table = {0};
    SdsError err = register_owner_table(&table, "TestTable");
    ASSERT_EQ(err, SDS_OK);
    ASSERT_EQ(sds_get_table_count(), 1);
}

TEST(register_multiple_tables) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table1 = {0};
    TestDeviceTable table2 = {0};
    
    ASSERT_EQ(register_device_table(&table1, "Table1"), SDS_OK);
    ASSERT_EQ(register_device_table(&table2, "Table2"), SDS_OK);
    ASSERT_EQ(sds_get_table_count(), 2);
}

TEST(register_duplicate_table_fails) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table1 = {0};
    TestDeviceTable table2 = {0};
    
    ASSERT_EQ(register_device_table(&table1, "TestTable"), SDS_OK);
    ASSERT_EQ(register_device_table(&table2, "TestTable"), SDS_ERR_TABLE_ALREADY_REGISTERED);
}

TEST(register_before_init_fails) {
    TestDeviceTable table = {0};
    SdsError err = register_device_table(&table, "TestTable");
    ASSERT_EQ(err, SDS_ERR_NOT_INITIALIZED);
}

TEST(register_null_table_fails) {
    init_sds_with_mock("test_node");
    
    SdsError err = sds_register_table_ex(
        NULL, "TestTable", SDS_ROLE_DEVICE, NULL,
        0, 0, 0, 0, 0, 0,
        NULL, NULL, NULL, NULL, NULL, NULL
    );
    ASSERT_EQ(err, SDS_ERR_INVALID_TABLE);
}

TEST(register_invalid_role_fails) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    SdsError err = sds_register_table_ex(
        &table, "TestTable", (SdsRole)99, NULL,
        0, 0, 0, 0, 0, 0,
        NULL, NULL, NULL, NULL, NULL, NULL
    );
    ASSERT_EQ(err, SDS_ERR_INVALID_ROLE);
}

TEST(unregister_table_success) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    ASSERT_EQ(sds_get_table_count(), 1);
    
    SdsError err = sds_unregister_table("TestTable");
    ASSERT_EQ(err, SDS_OK);
    ASSERT_EQ(sds_get_table_count(), 0);
}

TEST(unregister_nonexistent_fails) {
    init_sds_with_mock("test_node");
    
    SdsError err = sds_unregister_table("NonExistent");
    ASSERT_EQ(err, SDS_ERR_TABLE_NOT_FOUND);
}

/* ============================================================================
 * SUBSCRIPTION TESTS
 * ============================================================================ */

TEST(device_subscribes_to_config) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    ASSERT(sds_mock_is_subscribed("sds/TestTable/config"));
}

TEST(owner_subscribes_to_state_and_status) {
    init_sds_with_mock("test_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    
    ASSERT(sds_mock_is_subscribed("sds/TestTable/state"));
    ASSERT(sds_mock_is_subscribed("sds/TestTable/status/+"));
}

TEST(unregister_removes_subscriptions) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    ASSERT(sds_mock_is_subscribed("sds/TestTable/config"));
    
    sds_unregister_table("TestTable");
    ASSERT(!sds_mock_is_subscribed("sds/TestTable/config"));
}

/* ============================================================================
 * SYNC / PUBLISH TESTS
 * ============================================================================ */

TEST(device_publishes_state_on_change) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    table.state.temperature = 25.0f;
    table.state.humidity = 50.0f;
    register_device_table(&table, "TestTable");
    
    /* Advance time past sync interval */
    sds_mock_advance_time(1100);
    sds_loop();
    
    /* Should have published state */
    const SdsMockPublishedMessage* msg = sds_mock_find_publish_by_topic("sds/TestTable/state");
    ASSERT(msg != NULL);
    ASSERT_STR_CONTAINS((char*)msg->payload, "temperature");
    ASSERT_STR_CONTAINS((char*)msg->payload, "25.0");
}

TEST(device_publishes_status_on_change) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    table.status.battery_level = 85;
    register_device_table(&table, "TestTable");
    
    sds_mock_advance_time(1100);
    sds_loop();
    
    /* Should have published status */
    const SdsMockPublishedMessage* msg = sds_mock_find_publish_by_topic("sds/TestTable/status/device_node");
    ASSERT(msg != NULL);
    ASSERT_STR_CONTAINS((char*)msg->payload, "battery_level");
    ASSERT_STR_CONTAINS((char*)msg->payload, "85");
}

TEST(owner_publishes_config_on_change) {
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    table.config.mode = 3;
    table.config.threshold = 30.5f;
    register_owner_table(&table, "TestTable");
    
    /* Initial publish happens at registration */
    const SdsMockPublishedMessage* msg = sds_mock_find_publish_by_topic("sds/TestTable/config");
    ASSERT(msg != NULL);
    ASSERT_STR_CONTAINS((char*)msg->payload, "mode");
    ASSERT_STR_CONTAINS((char*)msg->payload, "3");
    ASSERT(msg->retained);  /* Config should be retained */
}

TEST(no_publish_if_unchanged) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    table.state.temperature = 25.0f;
    register_device_table(&table, "TestTable");
    
    /* First sync */
    sds_mock_advance_time(1100);
    sds_loop();
    size_t count1 = sds_mock_get_publish_count();
    
    /* Second sync without changes */
    sds_mock_advance_time(1100);
    sds_loop();
    size_t count2 = sds_mock_get_publish_count();
    
    /* Should not have published again */
    ASSERT_EQ(count1, count2);
}

TEST(publishes_on_value_change) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    table.state.temperature = 25.0f;
    register_device_table(&table, "TestTable");
    
    /* First sync */
    sds_mock_advance_time(1100);
    sds_loop();
    size_t count1 = sds_mock_get_publish_count();
    
    /* Change value */
    table.state.temperature = 26.0f;
    
    /* Second sync with change */
    sds_mock_advance_time(1100);
    sds_loop();
    size_t count2 = sds_mock_get_publish_count();
    
    /* Should have published again */
    ASSERT(count2 > count1);
}

/* ============================================================================
 * MESSAGE RECEIVE TESTS
 * ============================================================================ */

TEST(device_receives_config) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    /* Inject a config message */
    sds_mock_inject_message_str(
        "sds/TestTable/config",
        "{\"ts\":1000,\"from\":\"owner\",\"mode\":5,\"threshold\":35.5}"
    );
    
    /* Verify config was applied */
    ASSERT_EQ(table.config.mode, 5);
    ASSERT_FLOAT_EQ(table.config.threshold, 35.5f, 0.01f);
}

TEST(owner_receives_state) {
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    
    /* Inject a state message from a device */
    sds_mock_inject_message_str(
        "sds/TestTable/state",
        "{\"ts\":1000,\"node\":\"device1\",\"temperature\":28.5,\"humidity\":60.0,\"reading_count\":42}"
    );
    
    /* Verify state was received */
    ASSERT_FLOAT_EQ(table.state.temperature, 28.5f, 0.01f);
    ASSERT_FLOAT_EQ(table.state.humidity, 60.0f, 0.01f);
    ASSERT_EQ(table.state.reading_count, 42);
}

TEST(owner_receives_status_creates_slot) {
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    
    /* Inject status from a device */
    sds_mock_inject_message_str(
        "sds/TestTable/status/device1",
        "{\"ts\":1000,\"error_code\":0,\"battery_level\":90}"
    );
    
    /* Verify slot was created */
    ASSERT_EQ(table.status_count, 1);
    ASSERT(table.status_slots[0].valid);
    ASSERT_STR_EQ(table.status_slots[0].node_id, "device1");
    ASSERT_EQ(table.status_slots[0].status.battery_level, 90);
}

TEST(owner_tracks_multiple_devices) {
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    
    /* Status from multiple devices */
    sds_mock_inject_message_str(
        "sds/TestTable/status/device1",
        "{\"ts\":1000,\"error_code\":0,\"battery_level\":90}"
    );
    sds_mock_inject_message_str(
        "sds/TestTable/status/device2",
        "{\"ts\":1001,\"error_code\":1,\"battery_level\":75}"
    );
    sds_mock_inject_message_str(
        "sds/TestTable/status/device3",
        "{\"ts\":1002,\"error_code\":0,\"battery_level\":50}"
    );
    
    /* Verify all devices tracked */
    ASSERT_EQ(table.status_count, 3);
}

TEST(owner_updates_existing_slot) {
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    
    /* First status */
    sds_mock_inject_message_str(
        "sds/TestTable/status/device1",
        "{\"ts\":1000,\"error_code\":0,\"battery_level\":90}"
    );
    
    /* Second status from same device */
    sds_mock_inject_message_str(
        "sds/TestTable/status/device1",
        "{\"ts\":2000,\"error_code\":0,\"battery_level\":85}"
    );
    
    /* Should still have only 1 slot */
    ASSERT_EQ(table.status_count, 1);
    ASSERT_EQ(table.status_slots[0].status.battery_level, 85);
}

TEST(owner_ignores_own_state) {
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    table.state.temperature = 20.0f;
    register_owner_table(&table, "TestTable");
    
    /* Inject a state message from ourselves */
    sds_mock_inject_message_str(
        "sds/TestTable/state",
        "{\"ts\":1000,\"node\":\"owner_node\",\"temperature\":99.9,\"humidity\":99.9,\"reading_count\":999}"
    );
    
    /* State should NOT have changed (we ignore our own messages) */
    ASSERT_FLOAT_EQ(table.state.temperature, 20.0f, 0.01f);
}

/* ============================================================================
 * CALLBACK TESTS
 * ============================================================================ */

static int g_config_callback_count = 0;
static int g_state_callback_count = 0;
static int g_status_callback_count = 0;
static char g_last_callback_from_node[64] = "";

static void test_config_callback(const char* table_type) {
    (void)table_type;
    g_config_callback_count++;
}

static void test_state_callback(const char* table_type, const char* from_node) {
    (void)table_type;
    g_state_callback_count++;
    if (from_node) {
        strncpy(g_last_callback_from_node, from_node, sizeof(g_last_callback_from_node) - 1);
    }
}

static void test_status_callback(const char* table_type, const char* from_node) {
    (void)table_type;
    g_status_callback_count++;
    if (from_node) {
        strncpy(g_last_callback_from_node, from_node, sizeof(g_last_callback_from_node) - 1);
    }
}

TEST(config_callback_invoked) {
    g_config_callback_count = 0;
    
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    sds_on_config_update("TestTable", test_config_callback);
    
    sds_mock_inject_message_str(
        "sds/TestTable/config",
        "{\"mode\":5,\"threshold\":35.5}"
    );
    
    ASSERT_EQ(g_config_callback_count, 1);
}

TEST(state_callback_invoked_with_node) {
    g_state_callback_count = 0;
    g_last_callback_from_node[0] = '\0';
    
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    sds_on_state_update("TestTable", test_state_callback);
    
    sds_mock_inject_message_str(
        "sds/TestTable/state",
        "{\"node\":\"remote_device\",\"temperature\":25.0}"
    );
    
    ASSERT_EQ(g_state_callback_count, 1);
    ASSERT_STR_EQ(g_last_callback_from_node, "remote_device");
}

TEST(status_callback_invoked_with_node) {
    g_status_callback_count = 0;
    g_last_callback_from_node[0] = '\0';
    
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    sds_on_status_update("TestTable", test_status_callback);
    
    sds_mock_inject_message_str(
        "sds/TestTable/status/sensor_42",
        "{\"error_code\":0,\"battery_level\":80}"
    );
    
    ASSERT_EQ(g_status_callback_count, 1);
    ASSERT_STR_EQ(g_last_callback_from_node, "sensor_42");
}

/* ============================================================================
 * RECONNECTION TESTS
 * ============================================================================ */

TEST(detects_disconnect) {
    init_sds_with_mock("test_node");
    ASSERT(sds_is_ready());
    
    /* Simulate disconnect AND prevent auto-reconnect */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_connected = false;
    cfg.mqtt_connect_returns_success = false;  /* Prevent auto-reconnect */
    sds_mock_configure(&cfg);
    
    /* sds_loop should detect disconnect and fail to reconnect */
    sds_loop();
    
    ASSERT(!sds_is_ready());
}

TEST(reconnects_after_disconnect) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    size_t initial_connect_count = sds_mock_get_connect_count();
    
    /* Simulate disconnect and reconnect */
    sds_mock_simulate_disconnect();
    sds_loop();  /* Detect disconnect */
    
    sds_mock_simulate_reconnect();
    sds_loop();  /* Should reconnect */
    
    ASSERT(sds_is_ready());
    ASSERT(sds_mock_get_connect_count() > initial_connect_count);
    
    /* Verify reconnect count stat */
    const SdsStats* stats = sds_get_stats();
    ASSERT(stats->reconnect_count >= 1);
}

TEST(resubscribes_after_reconnect) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    size_t initial_sub_count = sds_mock_get_subscribe_call_count();
    
    /* Simulate disconnect and reconnect */
    sds_mock_simulate_disconnect();
    sds_loop();
    sds_mock_simulate_reconnect();
    sds_loop();
    
    /* Should have re-subscribed */
    ASSERT(sds_mock_get_subscribe_call_count() > initial_sub_count);
    ASSERT(sds_mock_is_subscribed("sds/TestTable/config"));
}

/* ============================================================================
 * STATISTICS TESTS
 * ============================================================================ */

TEST(stats_count_messages_sent) {
    init_sds_with_mock("test_node");
    
    TestDeviceTable table = {0};
    table.state.temperature = 25.0f;
    register_device_table(&table, "TestTable");
    
    const SdsStats* stats = sds_get_stats();
    uint32_t initial = stats->messages_sent;
    
    sds_mock_advance_time(1100);
    sds_loop();
    
    ASSERT(stats->messages_sent > initial);
}

TEST(stats_count_messages_received) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    const SdsStats* stats = sds_get_stats();
    uint32_t initial = stats->messages_received;
    
    sds_mock_inject_message_str(
        "sds/TestTable/config",
        "{\"mode\":5}"
    );
    
    ASSERT(stats->messages_received > initial);
}

/* ============================================================================
 * ERROR CALLBACK TESTS
 * ============================================================================ */

static SdsError g_last_error = SDS_OK;
static char g_last_error_context[256] = "";

static void test_error_callback(SdsError error, const char* context) {
    g_last_error = error;
    if (context) {
        strncpy(g_last_error_context, context, sizeof(g_last_error_context) - 1);
    }
}

TEST(error_callback_on_reconnect_failure) {
    g_last_error = SDS_OK;
    g_last_error_context[0] = '\0';
    
    init_sds_with_mock("test_node");
    sds_on_error(test_error_callback);
    
    /* Simulate disconnect */
    sds_mock_simulate_disconnect();
    
    /* Make reconnect fail */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_connect_returns_success = false;
    sds_mock_configure(&cfg);
    
    sds_loop();  /* Should try to reconnect and fail */
    
    ASSERT_EQ(g_last_error, SDS_ERR_MQTT_DISCONNECTED);
}

/* ============================================================================
 * EDGE CASE TESTS
 * ============================================================================ */

TEST(handles_empty_payload) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    table.config.mode = 1;
    register_device_table(&table, "TestTable");
    
    /* Inject empty payload - should not crash */
    sds_mock_inject_message("sds/TestTable/config", (const uint8_t*)"", 0);
    
    /* Config should remain unchanged */
    ASSERT_EQ(table.config.mode, 1);
}

TEST(handles_malformed_json) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    table.config.mode = 1;
    register_device_table(&table, "TestTable");
    
    /* Inject malformed JSON - should not crash */
    sds_mock_inject_message_str(
        "sds/TestTable/config",
        "{not valid json at all"
    );
    
    /* Config should remain unchanged */
    ASSERT_EQ(table.config.mode, 1);
}

TEST(handles_wrong_topic_prefix) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    const SdsStats* stats = sds_get_stats();
    uint32_t initial = stats->messages_received;
    
    /* Inject message with wrong prefix - should be ignored */
    sds_mock_inject_message_str(
        "wrong/TestTable/config",
        "{\"mode\":5}"
    );
    
    /* Message should still be counted but ignored */
    /* (or not, depending on implementation) */
}

TEST(handles_unknown_table) {
    init_sds_with_mock("device_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    /* Inject message for unknown table - should not crash */
    sds_mock_inject_message_str(
        "sds/UnknownTable/config",
        "{\"mode\":5}"
    );
}

TEST(status_slots_full) {
    init_sds_with_mock("owner_node");
    
    TestOwnerTable table = {0};
    register_owner_table(&table, "TestTable");
    
    /* Fill all slots (TEST_MAX_NODES = 4) */
    for (int i = 0; i < TEST_MAX_NODES + 2; i++) {
        char topic[64];
        snprintf(topic, sizeof(topic), "sds/TestTable/status/device%d", i);
        sds_mock_inject_message_str(topic, "{\"error_code\":0,\"battery_level\":80}");
    }
    
    /* Should have filled only TEST_MAX_NODES slots */
    ASSERT_EQ(table.status_count, TEST_MAX_NODES);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║          SDS Core Unit Tests (Mock Platform)                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("─── Initialization Tests ───\n");
    RUN_TEST(init_success);
    RUN_TEST(init_double_init_fails);
    RUN_TEST(init_null_config_fails);
    RUN_TEST(init_null_broker_fails);
    RUN_TEST(init_platform_failure);
    RUN_TEST(init_mqtt_connect_failure);
    RUN_TEST(init_auto_generates_node_id);
    RUN_TEST(shutdown_cleans_up);
    
    printf("\n─── Table Registration Tests ───\n");
    RUN_TEST(register_device_table_success);
    RUN_TEST(register_owner_table_success);
    RUN_TEST(register_multiple_tables);
    RUN_TEST(register_duplicate_table_fails);
    RUN_TEST(register_before_init_fails);
    RUN_TEST(register_null_table_fails);
    RUN_TEST(register_invalid_role_fails);
    RUN_TEST(unregister_table_success);
    RUN_TEST(unregister_nonexistent_fails);
    
    printf("\n─── Subscription Tests ───\n");
    RUN_TEST(device_subscribes_to_config);
    RUN_TEST(owner_subscribes_to_state_and_status);
    RUN_TEST(unregister_removes_subscriptions);
    
    printf("\n─── Sync / Publish Tests ───\n");
    RUN_TEST(device_publishes_state_on_change);
    RUN_TEST(device_publishes_status_on_change);
    RUN_TEST(owner_publishes_config_on_change);
    RUN_TEST(no_publish_if_unchanged);
    RUN_TEST(publishes_on_value_change);
    
    printf("\n─── Message Receive Tests ───\n");
    RUN_TEST(device_receives_config);
    RUN_TEST(owner_receives_state);
    RUN_TEST(owner_receives_status_creates_slot);
    RUN_TEST(owner_tracks_multiple_devices);
    RUN_TEST(owner_updates_existing_slot);
    RUN_TEST(owner_ignores_own_state);
    
    printf("\n─── Callback Tests ───\n");
    RUN_TEST(config_callback_invoked);
    RUN_TEST(state_callback_invoked_with_node);
    RUN_TEST(status_callback_invoked_with_node);
    
    printf("\n─── Reconnection Tests ───\n");
    RUN_TEST(detects_disconnect);
    RUN_TEST(reconnects_after_disconnect);
    RUN_TEST(resubscribes_after_reconnect);
    
    printf("\n─── Statistics Tests ───\n");
    RUN_TEST(stats_count_messages_sent);
    RUN_TEST(stats_count_messages_received);
    
    printf("\n─── Error Callback Tests ───\n");
    RUN_TEST(error_callback_on_reconnect_failure);
    
    printf("\n─── Edge Case Tests ───\n");
    RUN_TEST(handles_empty_payload);
    RUN_TEST(handles_malformed_json);
    RUN_TEST(handles_wrong_topic_prefix);
    RUN_TEST(handles_unknown_table);
    RUN_TEST(status_slots_full);
    
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
