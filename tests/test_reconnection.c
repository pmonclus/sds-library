/*
 * test_reconnection.c - MQTT Reconnection Scenario Tests
 * 
 * Tests disconnect/reconnect handling using the mock platform:
 *   - Disconnect during idle
 *   - Disconnect during publish
 *   - Reconnect with pending data
 *   - Multiple rapid reconnects
 *   - Re-subscription after reconnect
 * 
 * Build:
 *   gcc -I../include -I. -o test_reconnection test_reconnection.c \
 *       mock/sds_platform_mock.c ../src/sds_core.c ../src/sds_json.c -lm
 */

#include "sds.h"
#include "sds_json.h"
#include "mock/sds_platform_mock.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_STR_CONTAINS(haystack, needle) ASSERT(strstr((haystack), (needle)) != NULL)

/* ============== Test Table Definitions ============== */

typedef struct {
    uint8_t mode;
    float threshold;
} TestConfig;

typedef struct {
    float value;
    uint32_t counter;
} TestState;

typedef struct {
    uint8_t error_code;
} TestStatus;

typedef struct {
    TestConfig config;
    TestState state;
    TestStatus status;
} TestDeviceTable;

/* ============== Serialization Functions ============== */

static void serialize_config(void* section, SdsJsonWriter* w) {
    TestConfig* cfg = (TestConfig*)section;
    sds_json_add_uint(w, "mode", cfg->mode);
    sds_json_add_float(w, "threshold", cfg->threshold);
}

static void deserialize_config(void* section, SdsJsonReader* r) {
    TestConfig* cfg = (TestConfig*)section;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
    sds_json_get_float_field(r, "threshold", &cfg->threshold);
}

static void serialize_state(void* section, SdsJsonWriter* w) {
    TestState* st = (TestState*)section;
    sds_json_add_float(w, "value", st->value);
    sds_json_add_uint(w, "counter", st->counter);
}

static void deserialize_state(void* section, SdsJsonReader* r) {
    TestState* st = (TestState*)section;
    sds_json_get_float_field(r, "value", &st->value);
    sds_json_get_uint_field(r, "counter", &st->counter);
}

static void serialize_status(void* section, SdsJsonWriter* w) {
    TestStatus* st = (TestStatus*)section;
    sds_json_add_uint(w, "error_code", st->error_code);
}

static void deserialize_status(void* section, SdsJsonReader* r) {
    TestStatus* st = (TestStatus*)section;
    sds_json_get_uint8_field(r, "error_code", &st->error_code);
}

/* ============== Helper Functions ============== */

static SdsError init_device(const char* node_id) {
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
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
}

/* ============================================================================
 * RECONNECTION TESTS
 * ============================================================================ */

TEST(disconnect_detected_on_loop) {
    init_device("test_node");
    ASSERT(sds_is_ready());
    
    /* Simulate disconnect */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_connected = false;
    cfg.mqtt_connect_returns_success = false;  /* Prevent auto-reconnect */
    sds_mock_configure(&cfg);
    
    /* Loop should detect disconnect */
    sds_loop();
    
    ASSERT(!sds_is_ready());
}

TEST(reconnect_after_disconnect) {
    init_device("recon_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    ASSERT(sds_is_ready());
    
    size_t initial_connects = sds_mock_get_connect_count();
    
    /* Simulate disconnect */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_connected = false;
    sds_mock_configure(&cfg);
    
    /* Loop detects disconnect and tries reconnect */
    sds_loop();
    
    /* Reconnect succeeded (connect_returns_success is still true) */
    ASSERT(sds_is_ready());
    ASSERT_GT(sds_mock_get_connect_count(), initial_connects);
    
    /* Check reconnect stat */
    const SdsStats* stats = sds_get_stats();
    ASSERT_EQ(stats->reconnect_count, 1);
}

TEST(resubscribes_after_reconnect) {
    init_device("resub_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    /* Verify initial subscription */
    ASSERT(sds_mock_is_subscribed("sds/TestTable/config"));
    size_t initial_sub_calls = sds_mock_get_subscribe_call_count();
    
    /* Simulate disconnect and reconnect */
    sds_mock_simulate_disconnect();
    sds_loop();  /* Detects disconnect, auto-reconnects */
    
    /* Should have re-subscribed */
    ASSERT_GT(sds_mock_get_subscribe_call_count(), initial_sub_calls);
    ASSERT(sds_mock_is_subscribed("sds/TestTable/config"));
}

TEST(multiple_tables_resubscribe) {
    init_device("multi_node");
    
    TestDeviceTable table1 = {0};
    TestDeviceTable table2 = {0};
    register_device_table(&table1, "Table1");
    register_device_table(&table2, "Table2");
    
    /* Verify initial subscriptions */
    ASSERT(sds_mock_is_subscribed("sds/Table1/config"));
    ASSERT(sds_mock_is_subscribed("sds/Table2/config"));
    size_t initial_sub_calls = sds_mock_get_subscribe_call_count();
    
    /* Simulate disconnect and reconnect */
    sds_mock_simulate_disconnect();
    sds_loop();
    
    /* Both tables should be resubscribed */
    size_t new_sub_calls = sds_mock_get_subscribe_call_count();
    ASSERT(new_sub_calls >= initial_sub_calls + 2);  /* At least 2 more subscribe calls */
    ASSERT(sds_mock_is_subscribed("sds/Table1/config"));
    ASSERT(sds_mock_is_subscribed("sds/Table2/config"));
}

TEST(state_change_during_disconnect_publishes_on_reconnect) {
    init_device("state_node");
    
    TestDeviceTable table = { .state = { .value = 1.0f } };
    register_device_table(&table, "TestTable");
    
    /* Initial sync */
    sds_mock_advance_time(1100);
    sds_loop();
    sds_mock_clear_publishes();
    
    /* Simulate disconnect */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_connected = false;
    cfg.mqtt_publish_returns_success = false;
    sds_mock_configure(&cfg);
    
    /* Change state while disconnected */
    table.state.value = 99.0f;
    
    /* Try to sync - fails because disconnected */
    sds_mock_advance_time(1100);
    sds_loop();
    
    /* No publish should have happened */
    ASSERT_EQ(sds_mock_get_publish_count(), 0);
    
    /* Reconnect */
    cfg.mqtt_connected = true;
    cfg.mqtt_publish_returns_success = true;
    sds_mock_configure(&cfg);
    
    sds_loop();  /* Reconnect */
    sds_mock_advance_time(1100);
    sds_loop();  /* Sync */
    
    /* Should have published the changed state */
    const SdsMockPublishedMessage* msg = sds_mock_find_publish_by_topic("sds/TestTable/state");
    ASSERT(msg != NULL);
    ASSERT_STR_CONTAINS((char*)msg->payload, "99");
}

TEST(publish_succeeds_after_being_reenabled) {
    init_device("retry_node");
    
    TestDeviceTable table = { .state = { .value = 1.0f } };
    register_device_table(&table, "TestTable");
    
    /* Initial sync succeeds */
    sds_mock_advance_time(1100);
    sds_loop();
    sds_mock_clear_publishes();
    
    /* Disable publish, change state, and wait */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_publish_returns_success = false;
    sds_mock_configure(&cfg);
    
    table.state.value = 99.0f;
    sds_mock_advance_time(1100);
    sds_loop();  /* Fails to publish */
    
    /* Re-enable publish */
    cfg.mqtt_publish_returns_success = true;
    sds_mock_configure(&cfg);
    
    /* Change state again to ensure there's something to sync */
    table.state.value = 123.0f;
    sds_mock_advance_time(1100);
    sds_loop();
    
    /* Should have published */
    const SdsMockPublishedMessage* msg = sds_mock_find_publish_by_topic("sds/TestTable/state");
    ASSERT(msg != NULL);
    ASSERT_STR_CONTAINS((char*)msg->payload, "123");
}

TEST(rapid_disconnect_reconnect_cycles) {
    init_device("rapid_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    /* Perform multiple rapid disconnect/reconnect cycles */
    for (int i = 0; i < 5; i++) {
        sds_mock_simulate_disconnect();
        sds_loop();  /* Reconnects automatically */
        ASSERT(sds_is_ready());
    }
    
    /* Should have tracked all reconnects */
    const SdsStats* stats = sds_get_stats();
    ASSERT_EQ(stats->reconnect_count, 5);
    
    /* Should still be functional */
    table.state.value = 42.0f;
    sds_mock_advance_time(1100);
    sds_loop();
    
    const SdsMockPublishedMessage* msg = sds_mock_find_publish_by_topic("sds/TestTable/state");
    ASSERT(msg != NULL);
}

static SdsError g_reconnect_last_error = SDS_OK;
static int g_reconnect_error_count = 0;

static void reconnect_error_callback(SdsError err, const char* ctx) {
    (void)ctx;
    g_reconnect_last_error = err;
    g_reconnect_error_count++;
}

TEST(reconnect_failure_invokes_error_callback) {
    g_reconnect_last_error = SDS_OK;
    g_reconnect_error_count = 0;
    
    init_device("error_node");
    sds_on_error(reconnect_error_callback);
    
    /* Simulate disconnect with reconnect failure */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_connected = false;
    cfg.mqtt_connect_returns_success = false;
    sds_mock_configure(&cfg);
    
    sds_loop();
    
    ASSERT(!sds_is_ready());
    ASSERT_EQ(g_reconnect_last_error, SDS_ERR_MQTT_DISCONNECTED);
    ASSERT_EQ(g_reconnect_error_count, 1);
}

TEST(config_received_after_reconnect) {
    init_device("cfg_node");
    
    TestDeviceTable table = { .config = { .mode = 1 } };
    register_device_table(&table, "TestTable");
    
    /* Disconnect and reconnect */
    sds_mock_simulate_disconnect();
    sds_loop();
    ASSERT(sds_is_ready());
    
    /* Simulate receiving new config after reconnect */
    sds_mock_inject_message_str(
        "sds/TestTable/config",
        "{\"mode\":9,\"threshold\":50.0}"
    );
    
    /* Config should be updated */
    ASSERT_EQ(table.config.mode, 9);
}

TEST(connection_state_tracked_correctly) {
    init_device("track_node");
    ASSERT(sds_is_ready());
    
    /* Disconnect - prevent auto-reconnect */
    SdsMockConfig cfg = *sds_mock_get_config();
    cfg.mqtt_connected = false;
    cfg.mqtt_connect_returns_success = false;
    sds_mock_configure(&cfg);
    
    sds_loop();
    ASSERT(!sds_is_ready());
    
    /* Still disconnected on next loop */
    sds_loop();
    ASSERT(!sds_is_ready());
    
    /* Re-enable connect success before simulating reconnect */
    cfg = *sds_mock_get_config();
    cfg.mqtt_connect_returns_success = true;
    sds_mock_configure(&cfg);
    
    sds_mock_simulate_reconnect();
    
    /* Now we should be ready */
    ASSERT(sds_is_ready());
}

TEST(messages_received_counter_survives_reconnect) {
    init_device("counter_node");
    
    TestDeviceTable table = {0};
    register_device_table(&table, "TestTable");
    
    /* Receive some messages */
    sds_mock_inject_message_str("sds/TestTable/config", "{\"mode\":1}");
    sds_mock_inject_message_str("sds/TestTable/config", "{\"mode\":2}");
    
    const SdsStats* stats = sds_get_stats();
    uint32_t before_reconnect = stats->messages_received;
    ASSERT_EQ(before_reconnect, 2);
    
    /* Disconnect and reconnect */
    sds_mock_simulate_disconnect();
    sds_loop();
    
    /* Receive more messages */
    sds_mock_inject_message_str("sds/TestTable/config", "{\"mode\":3}");
    
    /* Counter should continue from before */
    ASSERT_EQ(stats->messages_received, 3);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║          SDS Reconnection Scenario Tests                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("─── Basic Disconnect/Reconnect ───\n");
    RUN_TEST(disconnect_detected_on_loop);
    RUN_TEST(reconnect_after_disconnect);
    RUN_TEST(connection_state_tracked_correctly);
    
    printf("\n─── Re-subscription ───\n");
    RUN_TEST(resubscribes_after_reconnect);
    RUN_TEST(multiple_tables_resubscribe);
    
    printf("\n─── State During Disconnect ───\n");
    RUN_TEST(state_change_during_disconnect_publishes_on_reconnect);
    RUN_TEST(publish_succeeds_after_being_reenabled);
    
    printf("\n─── Stress and Edge Cases ───\n");
    RUN_TEST(rapid_disconnect_reconnect_cycles);
    RUN_TEST(reconnect_failure_invokes_error_callback);
    RUN_TEST(config_received_after_reconnect);
    RUN_TEST(messages_received_counter_survives_reconnect);
    
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
