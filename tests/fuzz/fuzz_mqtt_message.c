/*
 * fuzz_mqtt_message.c - Fuzz target for MQTT message handling
 * 
 * Tests on_mqtt_message() and message parsing with adversarial input.
 * 
 * Build with AFL++:
 *   afl-clang-fast -g -O1 -I../../include -I.. \
 *       -o fuzz_mqtt fuzz_mqtt_message.c \
 *       ../mock/sds_platform_mock.c ../../src/sds_core.c ../../src/sds_json.c -lm
 * 
 * Build with libFuzzer (clang):
 *   clang -g -O1 -fsanitize=fuzzer,address -I../../include -I.. \
 *       -DUSE_LIBFUZZER -o fuzz_mqtt fuzz_mqtt_message.c \
 *       ../mock/sds_platform_mock.c ../../src/sds_core.c ../../src/sds_json.c -lm
 * 
 * Run AFL:
 *   afl-fuzz -i corpus/mqtt -o fuzz_out_mqtt ./fuzz_mqtt
 * 
 * Run libFuzzer:
 *   ./fuzz_mqtt corpus/mqtt
 */

#include "sds.h"
#include "sds_json.h"
#include "mock/sds_platform_mock.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============== Fuzz Table Definition ============== */

typedef struct {
    uint8_t mode;
    float threshold;
    char name[32];
} FuzzConfig;

typedef struct {
    float value;
    uint32_t counter;
    char label[64];
} FuzzState;

typedef struct {
    uint8_t error_code;
    uint8_t battery;
} FuzzStatus;

typedef struct {
    FuzzConfig config;
    FuzzState state;
    FuzzStatus status;
} FuzzTable;

static FuzzTable g_table;
static int g_initialized = 0;

/* ============== Serialization Functions ============== */

static void serialize_config(void* section, SdsJsonWriter* w) {
    FuzzConfig* cfg = (FuzzConfig*)section;
    sds_json_add_uint(w, "mode", cfg->mode);
    sds_json_add_float(w, "threshold", cfg->threshold);
    sds_json_add_string(w, "name", cfg->name);
}

static void deserialize_config(void* section, SdsJsonReader* r) {
    FuzzConfig* cfg = (FuzzConfig*)section;
    sds_json_get_uint8_field(r, "mode", &cfg->mode);
    sds_json_get_float_field(r, "threshold", &cfg->threshold);
    sds_json_get_string_field(r, "name", cfg->name, sizeof(cfg->name));
}

static void serialize_state(void* section, SdsJsonWriter* w) {
    FuzzState* st = (FuzzState*)section;
    sds_json_add_float(w, "value", st->value);
    sds_json_add_uint(w, "counter", st->counter);
    sds_json_add_string(w, "label", st->label);
}

static void deserialize_state(void* section, SdsJsonReader* r) {
    FuzzState* st = (FuzzState*)section;
    sds_json_get_float_field(r, "value", &st->value);
    sds_json_get_uint_field(r, "counter", &st->counter);
    sds_json_get_string_field(r, "label", st->label, sizeof(st->label));
}

static void serialize_status(void* section, SdsJsonWriter* w) {
    FuzzStatus* st = (FuzzStatus*)section;
    sds_json_add_uint(w, "error_code", st->error_code);
    sds_json_add_uint(w, "battery", st->battery);
}

static void deserialize_status(void* section, SdsJsonReader* r) {
    FuzzStatus* st = (FuzzStatus*)section;
    sds_json_get_uint8_field(r, "error_code", &st->error_code);
    sds_json_get_uint8_field(r, "battery", &st->battery);
}

/* ============== Initialization ============== */

static void init_sds_for_fuzzing(void) {
    if (g_initialized) {
        sds_shutdown();
    }
    
    sds_mock_reset();
    
    SdsMockConfig mock_cfg = {
        .init_returns_success = true,
        .mqtt_connect_returns_success = true,
        .mqtt_connected = true,
        .mqtt_publish_returns_success = true,
        .mqtt_subscribe_returns_success = true,
    };
    sds_mock_configure(&mock_cfg);
    
    SdsConfig config = {
        .node_id = "fuzz_node",
        .mqtt_broker = "mock_broker",
        .mqtt_port = 1883
    };
    
    if (sds_init(&config) != SDS_OK) {
        return;
    }
    
    memset(&g_table, 0, sizeof(g_table));
    
    sds_register_table_ex(
        &g_table, "FuzzTable", SDS_ROLE_DEVICE, NULL,
        offsetof(FuzzTable, config), sizeof(FuzzConfig),
        offsetof(FuzzTable, state), sizeof(FuzzState),
        offsetof(FuzzTable, status), sizeof(FuzzStatus),
        serialize_config, deserialize_config,
        serialize_state, deserialize_state,
        serialize_status, deserialize_status
    );
    
    g_initialized = 1;
}

/* ============== Fuzz Entry Point ============== */

static int fuzz_one_input(const uint8_t* data, size_t size) {
    if (size < 2) {
        return 0;  /* Need at least a topic separator and some data */
    }
    
    init_sds_for_fuzzing();
    
    if (!g_initialized) {
        return 0;
    }
    
    /* 
     * Input format: topic_len (1 byte) | topic | payload
     * This allows fuzzer to control topic/payload boundary
     */
    uint8_t topic_len = data[0] % 128;  /* Cap at 128 bytes */
    
    if (size < (size_t)(1 + topic_len)) {
        return 0;
    }
    
    /* Extract topic (null-terminate it) */
    char topic[129] = {0};
    size_t actual_topic_len = topic_len < (size - 1) ? topic_len : (size - 1);
    memcpy(topic, data + 1, actual_topic_len);
    topic[actual_topic_len] = '\0';
    
    /* Extract payload */
    const uint8_t* payload = data + 1 + actual_topic_len;
    size_t payload_len = size - 1 - actual_topic_len;
    
    /* Inject the fuzzed message */
    sds_mock_inject_message(topic, payload, payload_len);
    
    /* Run loop to process any side effects */
    sds_mock_advance_time(1100);
    sds_loop();
    
    return 0;
}

/* ============== Main / LibFuzzer Entry ============== */

#ifdef USE_LIBFUZZER
/* LibFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return fuzz_one_input(data, size);
}
#else
/* AFL / stdin mode */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
    while (__AFL_LOOP(10000)) {
#endif
        uint8_t input[4096];
        size_t len = fread(input, 1, sizeof(input), stdin);
        
        if (len > 0) {
            fuzz_one_input(input, len);
        }
        
#ifdef __AFL_HAVE_MANUAL_CONTROL
    }
#endif
    
    sds_shutdown();
    return 0;
}
#endif
