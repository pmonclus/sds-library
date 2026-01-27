/*
 * test_buffer_overflow.c - Buffer Limit and Overflow Tests
 * 
 * Tests serialization buffer limits and graceful handling:
 *   - JSON writer buffer overflow
 *   - Shadow buffer limits
 *   - Large payload handling
 *   - Status slot limits
 * 
 * Build:
 *   gcc -I../include -I. -o test_buffer_overflow test_buffer_overflow.c \
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
#define ASSERT_LE(a, b) ASSERT((a) <= (b))

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

/* ============================================================================
 * JSON WRITER OVERFLOW TESTS
 * ============================================================================ */

TEST(json_writer_tiny_buffer) {
    char buf[8];  /* Very small buffer */
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "key", "value");
    sds_json_end_object(&w);
    
    /* Should have error flag set due to overflow */
    ASSERT(sds_json_has_error(&w));
    
    /* Buffer should be safely null-terminated */
    ASSERT(buf[sizeof(buf) - 1] == '\0' || strlen(buf) < sizeof(buf));
}

TEST(json_writer_exact_fit) {
    /* Calculate exact size needed for {"a":1} = 7 chars + null = 8 */
    char buf[8];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_int(&w, "a", 1);
    sds_json_end_object(&w);
    
    /* Should fit exactly */
    ASSERT(!sds_json_has_error(&w));
    ASSERT_EQ(strcmp(buf, "{\"a\":1}"), 0);
}

TEST(json_writer_one_byte_short) {
    /* {"a":1} needs 8 bytes with null, give it 7 */
    char buf[7];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_int(&w, "a", 1);
    sds_json_end_object(&w);
    
    /* Should have error - one byte too short */
    ASSERT(sds_json_has_error(&w));
}

TEST(json_writer_long_string_truncated) {
    char buf[32];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "msg", "this is a very long string that will not fit");
    sds_json_end_object(&w);
    
    /* Should have error */
    ASSERT(sds_json_has_error(&w));
    
    /* Buffer should still be null-terminated */
    ASSERT(strlen(buf) < sizeof(buf));
}

TEST(json_writer_many_fields_overflow) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    for (int i = 0; i < 20; i++) {
        char key[8];
        snprintf(key, sizeof(key), "f%d", i);
        sds_json_add_int(&w, key, i);
    }
    sds_json_end_object(&w);
    
    /* Should have error after running out of space */
    ASSERT(sds_json_has_error(&w));
}

TEST(json_writer_escape_overflow) {
    char buf[16];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    /* Each quote in the value needs escaping: \" = 2 chars each */
    sds_json_add_string(&w, "s", "\"\"\"\"\"");
    sds_json_end_object(&w);
    
    /* Should overflow due to escape expansion */
    ASSERT(sds_json_has_error(&w));
}

TEST(json_writer_zero_buffer) {
    char buf[1] = {0};
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, 0);  /* Zero size */
    
    sds_json_start_object(&w);
    sds_json_add_int(&w, "a", 1);
    sds_json_end_object(&w);
    
    /* Should have error */
    ASSERT(sds_json_has_error(&w));
}

TEST(json_writer_continues_after_error) {
    char buf[16];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    
    /* This long string will cause overflow */
    sds_json_add_string(&w, "long", "this_string_is_way_too_long_for_buffer");
    ASSERT(sds_json_has_error(&w));
    
    /* Continue writing - should not crash */
    sds_json_add_int(&w, "x", 1);
    sds_json_add_float(&w, "y", 2.5f);
    sds_json_end_object(&w);
    
    /* Error should still be set */
    ASSERT(sds_json_has_error(&w));
    
    /* Buffer should still be safely terminated */
    ASSERT(strlen(buf) < sizeof(buf));
}

/* ============================================================================
 * JSON READER OVERFLOW TESTS
 * ============================================================================ */

TEST(json_reader_string_too_long_for_buffer) {
    const char* json = "{\"name\":\"this_is_a_very_long_value_that_exceeds_buffer\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[10];  /* Too small for value */
    bool result = sds_json_get_string_field(&r, "name", out, sizeof(out));
    
    /* Returns false because it couldn't read the full string (truncation) */
    /* This is correct behavior - caller knows the read was incomplete */
    ASSERT(!result);
    
    /* Buffer should still be safely null-terminated */
    ASSERT(strlen(out) < sizeof(out));
}

TEST(json_reader_very_long_json) {
    /* Create a JSON string with many fields */
    char json[2048];
    int pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{");
    for (int i = 0; i < 50; i++) {
        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos, "\"field%d\":%d", i, i * 10);
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "}");
    
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    /* Should be able to find fields at end */
    uint32_t val;
    bool result = sds_json_get_uint_field(&r, "field49", &val);
    ASSERT(result);
    ASSERT_EQ(val, 490);
}

/* ============================================================================
 * STATUS SLOT OVERFLOW TESTS
 * ============================================================================ */

typedef struct {
    uint8_t error_code;
} SimpleStatus;

typedef struct {
    char node_id[SDS_MAX_NODE_ID_LEN];
    bool valid;
    SimpleStatus status;
} StatusSlot;

#define MAX_SLOTS 3

typedef struct {
    uint8_t config[8];
    uint8_t state[8];
    StatusSlot status_slots[MAX_SLOTS];
    uint8_t status_count;
} OwnerTable;

static void deserialize_simple_status(void* section, SdsJsonReader* r) {
    SimpleStatus* st = (SimpleStatus*)section;
    sds_json_get_uint8_field(r, "error_code", &st->error_code);
}

TEST(status_slots_fill_up) {
    init_device("owner_node");
    
    OwnerTable table = {0};
    SdsError err = sds_register_table_ex(
        &table, "TestTable", SDS_ROLE_OWNER, NULL,
        offsetof(OwnerTable, config), sizeof(table.config),
        offsetof(OwnerTable, state), sizeof(table.state),
        0, 0,  /* Owner doesn't send status */
        NULL, NULL,
        NULL, NULL,
        NULL, deserialize_simple_status
    );
    ASSERT_EQ(err, SDS_OK);
    
    sds_set_owner_status_slots(
        "TestTable",
        offsetof(OwnerTable, status_slots),
        sizeof(StatusSlot),
        offsetof(StatusSlot, status),
        offsetof(OwnerTable, status_count),
        MAX_SLOTS
    );
    
    /* Fill all slots */
    for (int i = 0; i < MAX_SLOTS; i++) {
        char topic[64];
        snprintf(topic, sizeof(topic), "sds/TestTable/status/device%d", i);
        sds_mock_inject_message_str(topic, "{\"error_code\":0}");
    }
    
    ASSERT_EQ(table.status_count, MAX_SLOTS);
    
    /* Try to add one more - should be dropped */
    sds_mock_inject_message_str("sds/TestTable/status/extra_device", "{\"error_code\":99}");
    
    /* Count should not increase beyond max */
    ASSERT_EQ(table.status_count, MAX_SLOTS);
}

TEST(status_slot_reuse_same_device) {
    init_device("owner_node");
    
    OwnerTable table = {0};
    sds_register_table_ex(
        &table, "TestTable", SDS_ROLE_OWNER, NULL,
        offsetof(OwnerTable, config), sizeof(table.config),
        offsetof(OwnerTable, state), sizeof(table.state),
        0, 0,
        NULL, NULL, NULL, NULL, NULL, deserialize_simple_status
    );
    sds_set_owner_status_slots(
        "TestTable",
        offsetof(OwnerTable, status_slots),
        sizeof(StatusSlot),
        offsetof(StatusSlot, status),
        offsetof(OwnerTable, status_count),
        MAX_SLOTS
    );
    
    /* Same device sends multiple updates */
    for (int i = 0; i < 10; i++) {
        char payload[32];
        snprintf(payload, sizeof(payload), "{\"error_code\":%d}", i);
        sds_mock_inject_message_str("sds/TestTable/status/device1", payload);
    }
    
    /* Should only have one slot used */
    ASSERT_EQ(table.status_count, 1);
    
    /* Last value should be stored */
    ASSERT_EQ(table.status_slots[0].status.error_code, 9);
}

/* ============================================================================
 * LARGE PAYLOAD TESTS
 * ============================================================================ */

typedef struct {
    char field1[64];
    char field2[64];
    char field3[64];
    char field4[64];
} LargeState;

typedef struct {
    uint8_t config[8];
    LargeState state;
    uint8_t status[8];
} LargeTable;

static void serialize_large_state(void* section, SdsJsonWriter* w) {
    LargeState* st = (LargeState*)section;
    sds_json_add_string(w, "field1", st->field1);
    sds_json_add_string(w, "field2", st->field2);
    sds_json_add_string(w, "field3", st->field3);
    sds_json_add_string(w, "field4", st->field4);
}

TEST(large_state_serialization) {
    init_device("large_node");
    
    LargeTable table = {0};
    memset(table.state.field1, 'A', 63);
    memset(table.state.field2, 'B', 63);
    memset(table.state.field3, 'C', 63);
    memset(table.state.field4, 'D', 63);
    
    SdsError err = sds_register_table_ex(
        &table, "LargeTable", SDS_ROLE_DEVICE, NULL,
        offsetof(LargeTable, config), sizeof(table.config),
        offsetof(LargeTable, state), sizeof(LargeState),
        offsetof(LargeTable, status), sizeof(table.status),
        NULL, NULL,
        serialize_large_state, NULL,
        NULL, NULL
    );
    ASSERT_EQ(err, SDS_OK);
    
    /* Try to sync */
    sds_mock_advance_time(1100);
    sds_loop();
    
    /* Check if publish happened - may succeed or fail depending on buffer size */
    const SdsMockPublishedMessage* msg = sds_mock_find_publish_by_topic("sds/LargeTable/state");
    
    if (msg) {
        /* If it succeeded, verify content */
        ASSERT(strstr((char*)msg->payload, "AAAA") != NULL ||
               sds_mock_has_errors());
    }
    /* Either way, no crash - that's the key test */
}

/* ============================================================================
 * INCOMING MESSAGE SIZE TESTS
 * ============================================================================ */

typedef struct {
    uint8_t mode;
} TinyConfig;

typedef struct {
    TinyConfig config;
    uint8_t state[8];
    uint8_t status[8];
} TinyTable;

static void deserialize_tiny_config(void* section, SdsJsonReader* r) {
    TinyConfig* cfg = (TinyConfig*)section;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
}

TEST(receive_oversized_message) {
    init_device("tiny_node");
    
    TinyTable table = { .config = { .mode = 0 } };
    SdsError err = sds_register_table_ex(
        &table, "TinyTable", SDS_ROLE_DEVICE, NULL,
        offsetof(TinyTable, config), sizeof(TinyConfig),
        offsetof(TinyTable, state), sizeof(table.state),
        offsetof(TinyTable, status), sizeof(table.status),
        NULL, deserialize_tiny_config,
        NULL, NULL,
        NULL, NULL
    );
    ASSERT_EQ(err, SDS_OK);
    
    /* Send a very large config message */
    char large_json[2048];
    int pos = snprintf(large_json, sizeof(large_json), "{\"mode\":5,\"extra\":\"");
    for (int i = 0; i < 1500; i++) {
        large_json[pos++] = 'X';
    }
    pos += snprintf(large_json + pos, sizeof(large_json) - pos, "\"}");
    
    /* Inject - should not crash */
    sds_mock_inject_message("sds/TinyTable/config", (uint8_t*)large_json, pos);
    
    /* Config should be updated (extra field ignored) */
    ASSERT_EQ(table.config.mode, 5);
}

TEST(receive_deeply_nested_json) {
    init_device("nest_node");
    
    TinyTable table = { .config = { .mode = 0 } };
    sds_register_table_ex(
        &table, "TinyTable", SDS_ROLE_DEVICE, NULL,
        offsetof(TinyTable, config), sizeof(TinyConfig),
        offsetof(TinyTable, state), sizeof(table.state),
        offsetof(TinyTable, status), sizeof(table.status),
        NULL, deserialize_tiny_config,
        NULL, NULL,
        NULL, NULL
    );
    
    /* Create deeply nested but valid JSON (won't match our keys) */
    char nested[512];
    int pos = 0;
    pos += snprintf(nested + pos, sizeof(nested) - pos, "{\"mode\":7,\"nested\":");
    for (int i = 0; i < 20; i++) {
        pos += snprintf(nested + pos, sizeof(nested) - pos, "{\"level%d\":", i);
    }
    pos += snprintf(nested + pos, sizeof(nested) - pos, "1");
    for (int i = 0; i < 20; i++) {
        pos += snprintf(nested + pos, sizeof(nested) - pos, "}");
    }
    pos += snprintf(nested + pos, sizeof(nested) - pos, "}");
    
    /* Should not crash, and mode should be parsed */
    sds_mock_inject_message("sds/TinyTable/config", (uint8_t*)nested, pos);
    
    ASSERT_EQ(table.config.mode, 7);
}

/* ============================================================================
 * TOPIC LENGTH TESTS
 * ============================================================================ */

TEST(long_table_type_name) {
    init_device("long_name_node");
    
    TinyTable table = {0};
    
    /* Create a very long table type name */
    char long_name[128];
    memset(long_name, 'T', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    
    SdsError err = sds_register_table_ex(
        &table, long_name, SDS_ROLE_DEVICE, NULL,
        offsetof(TinyTable, config), sizeof(TinyConfig),
        offsetof(TinyTable, state), sizeof(table.state),
        offsetof(TinyTable, status), sizeof(table.status),
        NULL, NULL, NULL, NULL, NULL, NULL
    );
    
    /* Should either succeed or fail gracefully with an error */
    /* The key is no crash */
    (void)err;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║          SDS Buffer Overflow Tests                           ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("─── JSON Writer Overflow ───\n");
    RUN_TEST(json_writer_tiny_buffer);
    RUN_TEST(json_writer_exact_fit);
    RUN_TEST(json_writer_one_byte_short);
    RUN_TEST(json_writer_long_string_truncated);
    RUN_TEST(json_writer_many_fields_overflow);
    RUN_TEST(json_writer_escape_overflow);
    RUN_TEST(json_writer_zero_buffer);
    RUN_TEST(json_writer_continues_after_error);
    
    printf("\n─── JSON Reader Overflow ───\n");
    RUN_TEST(json_reader_string_too_long_for_buffer);
    RUN_TEST(json_reader_very_long_json);
    
    printf("\n─── Status Slot Overflow ───\n");
    RUN_TEST(status_slots_fill_up);
    RUN_TEST(status_slot_reuse_same_device);
    
    printf("\n─── Large Payload Handling ───\n");
    RUN_TEST(large_state_serialization);
    RUN_TEST(receive_oversized_message);
    RUN_TEST(receive_deeply_nested_json);
    
    printf("\n─── Topic/Name Length ───\n");
    RUN_TEST(long_table_type_name);
    
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
