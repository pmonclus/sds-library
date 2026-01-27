/*
 * test_concurrent.c - Thread Safety and Concurrent Access Tests
 * 
 * Tests for race conditions and thread-safety issues.
 * NOTE: The SDS library is currently single-threaded by design.
 * These tests help identify where thread-safety would be needed
 * for RTOS environments.
 * 
 * Build:
 *   gcc -I../include -I. -pthread -o test_concurrent test_concurrent.c \
 *       mock/sds_platform_mock.c ../src/sds_core.c ../src/sds_json.c -lm
 * 
 * Run with ThreadSanitizer:
 *   clang -fsanitize=thread -g -O1 -I../include -I. -pthread \
 *       -o test_concurrent_tsan test_concurrent.c \
 *       mock/sds_platform_mock.c ../src/sds_core.c ../src/sds_json.c -lm
 *   ./test_concurrent_tsan
 */

#include "sds.h"
#include "sds_json.h"
#include "mock/sds_platform_mock.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

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
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_LT(a, b) ASSERT((a) < (b))

/* ============== Test Table Definition ============== */

typedef struct {
    float value;
    uint32_t counter;
} SharedState;

typedef struct {
    uint8_t config_data[32];
    SharedState state;
    uint8_t status_data[32];
} SharedTable;

/* Global shared state for tests */
static SharedTable g_shared_table;
static atomic_int g_loop_count;
static atomic_int g_modify_count;
static atomic_int g_message_count;
static volatile int g_stop_flag;

/* ============== Serialization ============== */

static void serialize_state(void* section, SdsJsonWriter* w) {
    SharedState* st = (SharedState*)section;
    sds_json_add_float(w, "value", st->value);
    sds_json_add_uint(w, "counter", st->counter);
}

static void deserialize_state(void* section, SdsJsonReader* r) {
    SharedState* st = (SharedState*)section;
    sds_json_get_float_field(r, "value", &st->value);
    sds_json_get_uint_field(r, "counter", &st->counter);
}

/* ============== Thread Functions ============== */

/* Thread that repeatedly calls sds_loop() */
static void* loop_thread_func(void* arg) {
    (void)arg;
    
    while (!g_stop_flag) {
        sds_loop();
        atomic_fetch_add(&g_loop_count, 1);
        
        /* Small sleep to avoid pure spinning */
        usleep(100);  /* 0.1ms */
    }
    
    return NULL;
}

/* Thread that modifies table state */
static void* modifier_thread_func(void* arg) {
    (void)arg;
    
    while (!g_stop_flag) {
        /* Modify state rapidly */
        g_shared_table.state.value = (float)(rand() % 10000) / 100.0f;
        g_shared_table.state.counter++;
        atomic_fetch_add(&g_modify_count, 1);
        
        usleep(50);  /* 0.05ms */
    }
    
    return NULL;
}

/* Thread that injects messages */
static void* message_thread_func(void* arg) {
    (void)arg;
    
    while (!g_stop_flag) {
        char payload[64];
        snprintf(payload, sizeof(payload), 
                 "{\"value\":%.1f,\"counter\":%d}",
                 (float)(rand() % 1000) / 10.0f,
                 rand() % 10000);
        
        sds_mock_inject_message_str("sds/SharedTable/state", payload);
        atomic_fetch_add(&g_message_count, 1);
        
        usleep(200);  /* 0.2ms */
    }
    
    return NULL;
}

/* ============== Helper ============== */

static void init_shared_sds(void) {
    SdsMockConfig mock_cfg = {
        .init_returns_success = true,
        .mqtt_connect_returns_success = true,
        .mqtt_connected = true,
        .mqtt_publish_returns_success = true,
        .mqtt_subscribe_returns_success = true,
    };
    sds_mock_configure(&mock_cfg);
    
    SdsConfig config = {
        .node_id = "concurrent_test",
        .mqtt_broker = "mock",
        .mqtt_port = 1883
    };
    sds_init(&config);
    
    memset(&g_shared_table, 0, sizeof(g_shared_table));
    
    sds_register_table_ex(
        &g_shared_table, "SharedTable", SDS_ROLE_DEVICE, NULL,
        offsetof(SharedTable, config_data), sizeof(g_shared_table.config_data),
        offsetof(SharedTable, state), sizeof(SharedState),
        offsetof(SharedTable, status_data), sizeof(g_shared_table.status_data),
        NULL, NULL,
        serialize_state, deserialize_state,
        NULL, NULL
    );
}

/* ============================================================================
 * CONCURRENT ACCESS TESTS
 * ============================================================================ */

TEST(single_thread_baseline) {
    /* Establish baseline: everything works in single-threaded mode */
    init_shared_sds();
    
    g_shared_table.state.value = 42.0f;
    g_shared_table.state.counter = 100;
    
    /* Run loop a few times */
    for (int i = 0; i < 10; i++) {
        sds_mock_advance_time(1100);
        sds_loop();
    }
    
    ASSERT(sds_is_ready());
    
    const SdsStats* stats = sds_get_stats();
    ASSERT_GT(stats->messages_sent, 0);
}

TEST(concurrent_loop_and_modify) {
    /*
     * WARNING: This test exercises a known race condition.
     * Without mutex protection, this may:
     * - Pass (lucky timing)
     * - Fail with assertion
     * - Crash
     * - Produce corrupt data
     * 
     * Run with ThreadSanitizer to detect races.
     */
    init_shared_sds();
    
    g_stop_flag = 0;
    atomic_store(&g_loop_count, 0);
    atomic_store(&g_modify_count, 0);
    
    pthread_t loop_tid, modifier_tid;
    
    /* Start threads */
    pthread_create(&loop_tid, NULL, loop_thread_func, NULL);
    pthread_create(&modifier_tid, NULL, modifier_thread_func, NULL);
    
    /* Let them run for a short time */
    usleep(100000);  /* 100ms */
    
    /* Stop threads */
    g_stop_flag = 1;
    pthread_join(loop_tid, NULL);
    pthread_join(modifier_tid, NULL);
    
    int loops = atomic_load(&g_loop_count);
    int mods = atomic_load(&g_modify_count);
    
    printf("(loops=%d, mods=%d) ", loops, mods);
    
    /* Verify we actually did some work */
    ASSERT_GT(loops, 0);
    ASSERT_GT(mods, 0);
    
    /* Library should still be in a valid state (may have corrupted data though) */
    /* This is a "soft" check - the real test is whether we crashed */
}

TEST(concurrent_loop_and_message_inject) {
    /*
     * Tests race between sds_loop() and incoming message callback.
     * This simulates the MQTT callback scenario on ESP32.
     */
    init_shared_sds();
    
    g_stop_flag = 0;
    atomic_store(&g_loop_count, 0);
    atomic_store(&g_message_count, 0);
    
    pthread_t loop_tid, message_tid;
    
    pthread_create(&loop_tid, NULL, loop_thread_func, NULL);
    pthread_create(&message_tid, NULL, message_thread_func, NULL);
    
    usleep(100000);  /* 100ms */
    
    g_stop_flag = 1;
    pthread_join(loop_tid, NULL);
    pthread_join(message_tid, NULL);
    
    int loops = atomic_load(&g_loop_count);
    int msgs = atomic_load(&g_message_count);
    
    printf("(loops=%d, msgs=%d) ", loops, msgs);
    
    ASSERT_GT(loops, 0);
    ASSERT_GT(msgs, 0);
}

TEST(concurrent_multiple_modifiers) {
    /*
     * Multiple threads all modifying the same table.
     */
    init_shared_sds();
    
    g_stop_flag = 0;
    atomic_store(&g_modify_count, 0);
    
    pthread_t mod_tids[4];
    
    for (int i = 0; i < 4; i++) {
        pthread_create(&mod_tids[i], NULL, modifier_thread_func, NULL);
    }
    
    /* One loop thread */
    pthread_t loop_tid;
    pthread_create(&loop_tid, NULL, loop_thread_func, NULL);
    
    usleep(100000);  /* 100ms */
    
    g_stop_flag = 1;
    
    for (int i = 0; i < 4; i++) {
        pthread_join(mod_tids[i], NULL);
    }
    pthread_join(loop_tid, NULL);
    
    int mods = atomic_load(&g_modify_count);
    printf("(mods=%d) ", mods);
    
    ASSERT_GT(mods, 0);
}

TEST(stats_counter_races) {
    /*
     * Test that statistics counters don't get corrupted.
     * Without atomic operations, counters may show incorrect values.
     */
    init_shared_sds();
    
    g_stop_flag = 0;
    atomic_store(&g_loop_count, 0);
    
    pthread_t loop_tids[2];
    pthread_t message_tid;
    
    /* Two loop threads (contending for stats) */
    pthread_create(&loop_tids[0], NULL, loop_thread_func, NULL);
    pthread_create(&loop_tids[1], NULL, loop_thread_func, NULL);
    pthread_create(&message_tid, NULL, message_thread_func, NULL);
    
    usleep(100000);  /* 100ms */
    
    g_stop_flag = 1;
    pthread_join(loop_tids[0], NULL);
    pthread_join(loop_tids[1], NULL);
    pthread_join(message_tid, NULL);
    
    const SdsStats* stats = sds_get_stats();
    
    /* Stats should be reasonable (not corrupted to extreme values) */
    ASSERT_LT(stats->messages_sent, 1000000);
    ASSERT_LT(stats->messages_received, 1000000);
    ASSERT_LT(stats->errors, 10000);
}

TEST(rapid_init_shutdown) {
    /*
     * Rapid init/shutdown cycles to stress initialization paths.
     */
    for (int i = 0; i < 20; i++) {
        sds_mock_reset();
        
        SdsMockConfig mock_cfg = {
            .init_returns_success = true,
            .mqtt_connect_returns_success = true,
            .mqtt_connected = true,
        };
        sds_mock_configure(&mock_cfg);
        
        SdsConfig config = {
            .node_id = "rapid_test",
            .mqtt_broker = "mock",
        };
        
        SdsError err = sds_init(&config);
        ASSERT_EQ(err, SDS_OK);
        ASSERT(sds_is_ready());
        
        sds_shutdown();
        ASSERT(!sds_is_ready());
    }
}

TEST(message_inject_during_shutdown) {
    /*
     * What happens if a message arrives during shutdown?
     */
    init_shared_sds();
    
    /* Inject some messages */
    sds_mock_inject_message_str("sds/SharedTable/state", "{\"value\":1.0}");
    sds_mock_inject_message_str("sds/SharedTable/state", "{\"value\":2.0}");
    
    /* Shutdown while messages might be processing */
    sds_shutdown();
    
    /* More messages after shutdown - should be safely ignored */
    sds_mock_inject_message_str("sds/SharedTable/state", "{\"value\":3.0}");
    
    /* Should not crash */
    ASSERT(!sds_is_ready());
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║          SDS Concurrent Access Tests                         ║\n");
    printf("║   (Run with ThreadSanitizer to detect races)                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("─── Baseline ───\n");
    RUN_TEST(single_thread_baseline);
    RUN_TEST(rapid_init_shutdown);
    
    printf("\n─── Concurrent Access (may expose races) ───\n");
    RUN_TEST(concurrent_loop_and_modify);
    RUN_TEST(concurrent_loop_and_message_inject);
    RUN_TEST(concurrent_multiple_modifiers);
    RUN_TEST(stats_counter_races);
    
    printf("\n─── Edge Cases ───\n");
    RUN_TEST(message_inject_during_shutdown);
    
    printf("\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("══════════════════════════════════════════════════════════════\n");
    
    if (g_tests_failed > 0) {
        printf("\n  ✗ TESTS FAILED\n\n");
        return 1;
    }
    
    printf("\n  ✓ ALL TESTS PASSED\n");
    printf("  Note: Run with ThreadSanitizer to detect data races.\n\n");
    return 0;
}
