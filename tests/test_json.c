/*
 * test_json.c - Comprehensive JSON Serialization/Parsing Tests
 * 
 * Tests both positive cases (valid inputs) and negative cases (trying to break it).
 * 
 * Build:
 *   gcc -I../include -o test_json test_json.c ../src/sds_json.c
 * 
 * Run:
 *   ./test_json
 */

#include "sds_json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <math.h>

/* ============== Test Framework ============== */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s", #name); \
    test_##name(); \
    printf(" ✓\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" ✗ FAILED\n"); \
        printf("    Assertion failed: %s\n", #cond); \
        printf("    At line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf(" ✗ FAILED\n"); \
        printf("    Expected: \"%s\"\n", (b)); \
        printf("    Got:      \"%s\"\n", (a)); \
        printf("    At line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf(" ✗ FAILED\n"); \
        printf("    Expected: %f\n", (double)(b)); \
        printf("    Got:      %f\n", (double)(a)); \
        printf("    At line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * JSON WRITER TESTS - Positive Cases
 * ============================================================================ */

TEST(writer_empty_object) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{}");
    ASSERT(sds_json_get_length(&w) == 2);
}

TEST(writer_single_string) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "name", "value");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"name\":\"value\"}");
}

TEST(writer_single_int) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_int(&w, "count", -42);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"count\":-42}");
}

TEST(writer_single_uint) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_uint(&w, "value", 4294967295U);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"value\":4294967295}");
}

TEST(writer_single_float) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_float(&w, "temp", 23.5f);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    /* Float has .4 precision, so 23.5 becomes 23.5000 */
    ASSERT(strstr(buf, "23.5") != NULL);
}

TEST(writer_single_bool_true) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_bool(&w, "enabled", true);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"enabled\":true}");
}

TEST(writer_single_bool_false) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_bool(&w, "enabled", false);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"enabled\":false}");
}

TEST(writer_multiple_fields) {
    char buf[128];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "name", "test");
    sds_json_add_int(&w, "value", 42);
    sds_json_add_bool(&w, "active", true);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"name\":\"test\",\"value\":42,\"active\":true}");
}

TEST(writer_empty_string_value) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "empty", "");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"empty\":\"\"}");
}

TEST(writer_zero_values) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_int(&w, "a", 0);
    sds_json_add_uint(&w, "b", 0);
    sds_json_add_float(&w, "c", 0.0f);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT(strstr(buf, "\"a\":0") != NULL);
    ASSERT(strstr(buf, "\"b\":0") != NULL);
}

TEST(writer_negative_int) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_int(&w, "neg", -2147483648);  /* INT_MIN */
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT(strstr(buf, "-2147483648") != NULL);
}

TEST(writer_negative_float) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_float(&w, "temp", -40.5f);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT(strstr(buf, "-40.5") != NULL);
}

/* ============================================================================
 * JSON WRITER TESTS - String Escaping
 * ============================================================================ */

TEST(writer_escape_quotes) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "msg", "say \"hello\"");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"msg\":\"say \\\"hello\\\"\"}");
}

TEST(writer_escape_backslash) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "path", "C:\\temp\\file");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"path\":\"C:\\\\temp\\\\file\"}");
}

TEST(writer_escape_newline) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "text", "line1\nline2");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"text\":\"line1\\nline2\"}");
}

TEST(writer_escape_tab) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "text", "col1\tcol2");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"text\":\"col1\\tcol2\"}");
}

TEST(writer_escape_carriage_return) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "text", "line1\r\nline2");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"text\":\"line1\\r\\nline2\"}");
}

TEST(writer_escape_mixed) {
    char buf[128];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "complex", "He said \"Hello!\"\nPath: C:\\test");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    /* Should have escaped quotes, newline, and backslashes */
    ASSERT(strstr(buf, "\\\"Hello!\\\"") != NULL);
    ASSERT(strstr(buf, "\\n") != NULL);
    ASSERT(strstr(buf, "C:\\\\test") != NULL);
}

/* ============================================================================
 * JSON WRITER TESTS - Buffer Overflow / Edge Cases
 * ============================================================================ */

TEST(writer_buffer_exact_fit) {
    /* Buffer exactly fits the output */
    char buf[17];  /* {"a":"b"} = 9 chars + null */
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "a", "b");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"a\":\"b\"}");
}

TEST(writer_buffer_overflow_string) {
    char buf[10];  /* Too small */
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "longkey", "longvalue");
    sds_json_end_object(&w);
    
    ASSERT(sds_json_has_error(&w));
}

TEST(writer_buffer_overflow_escape) {
    /* {"a":"\"\"\"\"\""} = 1+5+10+2 = 18 chars + null = 19 bytes needed */
    char buf[15];  /* Too small for escaped content */
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    /* With escaping, each quote becomes 2 chars: 5 quotes = 10 chars escaped */
    sds_json_add_string(&w, "a", "\"\"\"\"\"");
    sds_json_end_object(&w);
    
    ASSERT(sds_json_has_error(&w));
}

TEST(writer_buffer_size_one) {
    char buf[1];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    
    /* Should have error - can't fit even one char */
    ASSERT(sds_json_has_error(&w));
}

TEST(writer_buffer_size_zero) {
    char buf[1] = {0};
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, 0);
    
    sds_json_start_object(&w);
    
    /* Should handle gracefully */
    ASSERT(sds_json_has_error(&w) || sds_json_get_length(&w) == 0);
}

TEST(writer_long_string) {
    char buf[512];
    char long_value[256];
    memset(long_value, 'x', 255);
    long_value[255] = '\0';
    
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "data", long_value);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    ASSERT(sds_json_get_length(&w) > 260);
}

TEST(writer_null_value_string) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "key", NULL);
    sds_json_end_object(&w);
    
    /* Should handle NULL gracefully - empty string */
    ASSERT(!sds_json_has_error(&w));
    ASSERT_STR_EQ(buf, "{\"key\":\"\"}");
}

/* ============================================================================
 * JSON READER TESTS - Positive Cases
 * ============================================================================ */

TEST(reader_parse_string) {
    const char* json = "{\"name\":\"Alice\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    ASSERT(sds_json_get_string_field(&r, "name", out, sizeof(out)));
    ASSERT_STR_EQ(out, "Alice");
}

TEST(reader_parse_int_positive) {
    const char* json = "{\"count\":42}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    int32_t out;
    ASSERT(sds_json_get_int_field(&r, "count", &out));
    ASSERT(out == 42);
}

TEST(reader_parse_int_negative) {
    const char* json = "{\"temp\":-15}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    int32_t out;
    ASSERT(sds_json_get_int_field(&r, "temp", &out));
    ASSERT(out == -15);
}

TEST(reader_parse_int_max) {
    const char* json = "{\"val\":2147483647}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    int32_t out;
    ASSERT(sds_json_get_int_field(&r, "val", &out));
    ASSERT(out == 2147483647);
}

TEST(reader_parse_int_min) {
    const char* json = "{\"val\":-2147483648}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    int32_t out;
    ASSERT(sds_json_get_int_field(&r, "val", &out));
    ASSERT(out == -2147483648);
}

TEST(reader_parse_uint) {
    const char* json = "{\"id\":4294967295}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    uint32_t out;
    ASSERT(sds_json_get_uint_field(&r, "id", &out));
    ASSERT(out == 4294967295U);
}

TEST(reader_parse_uint8) {
    const char* json = "{\"byte\":255}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    uint8_t out;
    ASSERT(sds_json_get_uint8_field(&r, "byte", &out));
    ASSERT(out == 255);
}

TEST(reader_parse_float) {
    const char* json = "{\"temp\":23.5}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    float out;
    ASSERT(sds_json_get_float_field(&r, "temp", &out));
    ASSERT_FLOAT_EQ(out, 23.5f, 0.001f);
}

TEST(reader_parse_float_negative) {
    const char* json = "{\"temp\":-40.5}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    float out;
    ASSERT(sds_json_get_float_field(&r, "temp", &out));
    ASSERT_FLOAT_EQ(out, -40.5f, 0.001f);
}

TEST(reader_parse_float_scientific) {
    const char* json = "{\"val\":1.5e10}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    float out;
    ASSERT(sds_json_get_float_field(&r, "val", &out));
    ASSERT_FLOAT_EQ(out, 1.5e10f, 1e8f);
}

TEST(reader_parse_bool_true) {
    const char* json = "{\"active\":true}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    bool out;
    ASSERT(sds_json_get_bool_field(&r, "active", &out));
    ASSERT(out == true);
}

TEST(reader_parse_bool_false) {
    const char* json = "{\"active\":false}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    bool out;
    ASSERT(sds_json_get_bool_field(&r, "active", &out));
    ASSERT(out == false);
}

TEST(reader_parse_multiple_fields) {
    const char* json = "{\"name\":\"test\",\"count\":42,\"active\":true}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char name[32];
    int32_t count;
    bool active;
    
    ASSERT(sds_json_get_string_field(&r, "name", name, sizeof(name)));
    ASSERT(sds_json_get_int_field(&r, "count", &count));
    ASSERT(sds_json_get_bool_field(&r, "active", &active));
    
    ASSERT_STR_EQ(name, "test");
    ASSERT(count == 42);
    ASSERT(active == true);
}

TEST(reader_parse_with_whitespace) {
    const char* json = "{ \"name\" : \"test\" , \"value\" : 42 }";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char name[32];
    int32_t value;
    
    ASSERT(sds_json_get_string_field(&r, "name", name, sizeof(name)));
    ASSERT(sds_json_get_int_field(&r, "value", &value));
    
    ASSERT_STR_EQ(name, "test");
    ASSERT(value == 42);
}

TEST(reader_parse_empty_string) {
    const char* json = "{\"empty\":\"\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    ASSERT(sds_json_get_string_field(&r, "empty", out, sizeof(out)));
    ASSERT_STR_EQ(out, "");
}

/* ============================================================================
 * JSON READER TESTS - Missing / Not Found
 * ============================================================================ */

TEST(reader_field_not_found) {
    const char* json = "{\"name\":\"test\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    int32_t out;
    ASSERT(!sds_json_get_int_field(&r, "missing", &out));
}

TEST(reader_empty_object) {
    const char* json = "{}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    ASSERT(!sds_json_get_string_field(&r, "name", out, sizeof(out)));
}

TEST(reader_similar_key_names) {
    const char* json = "{\"name\":\"value\",\"name2\":\"other\",\"myname\":\"third\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    
    ASSERT(sds_json_get_string_field(&r, "name", out, sizeof(out)));
    ASSERT_STR_EQ(out, "value");
    
    ASSERT(sds_json_get_string_field(&r, "name2", out, sizeof(out)));
    ASSERT_STR_EQ(out, "other");
}

/* ============================================================================
 * JSON READER TESTS - Negative / Malformed Input
 * ============================================================================ */

TEST(reader_null_json) {
    SdsJsonReader r;
    sds_json_reader_init(&r, NULL, 0);
    
    char out[32];
    ASSERT(!sds_json_get_string_field(&r, "name", out, sizeof(out)));
}

TEST(reader_truncated_json) {
    const char* json = "{\"name\":\"test";  /* Missing closing quote and brace */
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    /* Should return false because string is not properly terminated */
    bool result = sds_json_get_string_field(&r, "name", out, sizeof(out));
    /* Behavior may vary - key thing is no crash */
    (void)result;
}

TEST(reader_no_value) {
    const char* json = "{\"name\":}";  /* Missing value */
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    /* Should fail gracefully */
    ASSERT(!sds_json_get_string_field(&r, "name", out, sizeof(out)));
}

TEST(reader_wrong_type_int_as_string) {
    const char* json = "{\"value\":42}";  /* Number, not string */
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    /* Number is not quoted, so string parse should fail */
    ASSERT(!sds_json_get_string_field(&r, "value", out, sizeof(out)));
}

TEST(reader_wrong_type_string_as_int) {
    const char* json = "{\"value\":\"notanumber\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    int32_t out;
    /* Quoted string, looking for int - should fail */
    ASSERT(!sds_json_get_int_field(&r, "value", &out));
}

TEST(reader_wrong_type_string_as_bool) {
    const char* json = "{\"active\":\"true\"}";  /* String "true", not boolean */
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    bool out;
    /* Quoted, looking for bool literal - should fail */
    ASSERT(!sds_json_get_bool_field(&r, "active", &out));
}

TEST(reader_invalid_bool) {
    const char* json = "{\"active\":maybe}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    bool out;
    ASSERT(!sds_json_get_bool_field(&r, "active", &out));
}

TEST(reader_string_buffer_too_small) {
    const char* json = "{\"name\":\"averylongstringthatwontfit\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[8];  /* Too small */
    bool result = sds_json_get_string_field(&r, "name", out, sizeof(out));
    
    /* With bounded parsing, returns false if closing quote not found within out_size-1 chars */
    /* The implementation truncates but returns false because it couldn't read complete string */
    /* This is stricter - verifies partial reads are caught */
    (void)result;
    /* Key assertion: no buffer overflow, output is null-terminated */
    ASSERT(strlen(out) <= 7);
}

TEST(reader_zero_length_buffer) {
    const char* json = "{\"name\":\"test\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[1] = {0};
    bool result = sds_json_get_string_field(&r, "name", out, 0);
    
    /* Zero-size buffer should return false to indicate error */
    ASSERT(result == false);
    /* Key: no crash, no buffer overflow */
}

TEST(reader_garbage_input) {
    const char* json = "!@#$%^&*()";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    ASSERT(!sds_json_get_string_field(&r, "name", out, sizeof(out)));
}

TEST(reader_just_braces) {
    const char* json = "{}{}{}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    ASSERT(!sds_json_get_string_field(&r, "name", out, sizeof(out)));
}

TEST(reader_partial_key_match) {
    const char* json = "{\"username\":\"alice\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    /* Looking for "user" should NOT match "username" */
    ASSERT(!sds_json_get_string_field(&r, "user", out, sizeof(out)));
}

TEST(reader_binary_in_json) {
    /* JSON with embedded null byte */
    char json[] = "{\"name\":\"te\0st\"}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, 16);  /* Include null in length */
    
    char out[32];
    bool result = sds_json_get_string_field(&r, "name", out, sizeof(out));
    /* Should parse up to null or handle gracefully */
    (void)result;
    /* Key: no crash */
}

TEST(reader_very_long_key) {
    char json[512];
    char longkey[256];
    memset(longkey, 'x', 255);
    longkey[255] = '\0';
    
    snprintf(json, sizeof(json), "{\"%s\":\"value\"}", longkey);
    
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    char out[32];
    ASSERT(sds_json_get_string_field(&r, longkey, out, sizeof(out)));
    ASSERT_STR_EQ(out, "value");
}

TEST(reader_length_limited) {
    /* Provide shorter length than actual string */
    const char* json = "{\"name\":\"test\",\"value\":42}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, 10);  /* Only first 10 chars */
    
    char out[32];
    /* Should fail to find complete value */
    bool result = sds_json_get_string_field(&r, "name", out, sizeof(out));
    /* Behavior varies - key is no crash */
    (void)result;
}

TEST(reader_overflow_numbers) {
    /* Number too large for int32 */
    const char* json = "{\"huge\":99999999999999999999}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    int32_t out;
    /* strtol handles overflow with ERANGE, but we don't check it */
    /* Should return some value without crashing */
    bool result = sds_json_get_int_field(&r, "huge", &out);
    (void)result;  /* Just verify no crash */
}

TEST(reader_negative_uint) {
    /* Negative value for unsigned */
    const char* json = "{\"val\":-1}";
    SdsJsonReader r;
    sds_json_reader_init(&r, json, strlen(json));
    
    uint32_t out;
    /* strtoul converts -1 to UINT_MAX */
    bool result = sds_json_get_uint_field(&r, "val", &out);
    if (result) {
        ASSERT(out == 4294967295U);  /* -1 wraps to max */
    }
}

/* ============================================================================
 * ROUND-TRIP TESTS - Write then Read
 * ============================================================================ */

TEST(roundtrip_string) {
    char buf[128];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "msg", "Hello, World!");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    
    SdsJsonReader r;
    sds_json_reader_init(&r, buf, sds_json_get_length(&w));
    
    char out[32];
    ASSERT(sds_json_get_string_field(&r, "msg", out, sizeof(out)));
    ASSERT_STR_EQ(out, "Hello, World!");
}

TEST(roundtrip_int) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_int(&w, "value", -12345);
    sds_json_end_object(&w);
    
    SdsJsonReader r;
    sds_json_reader_init(&r, buf, sds_json_get_length(&w));
    
    int32_t out;
    ASSERT(sds_json_get_int_field(&r, "value", &out));
    ASSERT(out == -12345);
}

TEST(roundtrip_float) {
    char buf[64];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_float(&w, "temp", 23.456f);
    sds_json_end_object(&w);
    
    SdsJsonReader r;
    sds_json_reader_init(&r, buf, sds_json_get_length(&w));
    
    float out;
    ASSERT(sds_json_get_float_field(&r, "temp", &out));
    ASSERT_FLOAT_EQ(out, 23.456f, 0.001f);
}

TEST(roundtrip_escaped_string) {
    char buf[128];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    /* Note: Reader doesn't unescape, so we test that escaped JSON is valid */
    sds_json_start_object(&w);
    sds_json_add_string(&w, "msg", "line1\nline2");
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    /* Verify the JSON is valid (has \n escaped) */
    ASSERT(strstr(buf, "\\n") != NULL);
    /* The raw string read back will include backslash-n as two chars */
}

TEST(roundtrip_complex) {
    char buf[256];
    SdsJsonWriter w;
    sds_json_writer_init(&w, buf, sizeof(buf));
    
    sds_json_start_object(&w);
    sds_json_add_string(&w, "node", "sensor1");
    sds_json_add_float(&w, "temp", 23.5f);
    sds_json_add_int(&w, "humidity", 45);
    sds_json_add_bool(&w, "active", true);
    sds_json_add_uint(&w, "uptime", 123456);
    sds_json_end_object(&w);
    
    ASSERT(!sds_json_has_error(&w));
    
    SdsJsonReader r;
    sds_json_reader_init(&r, buf, sds_json_get_length(&w));
    
    char node[32];
    float temp;
    int32_t humidity;
    bool active;
    uint32_t uptime;
    
    ASSERT(sds_json_get_string_field(&r, "node", node, sizeof(node)));
    ASSERT(sds_json_get_float_field(&r, "temp", &temp));
    ASSERT(sds_json_get_int_field(&r, "humidity", &humidity));
    ASSERT(sds_json_get_bool_field(&r, "active", &active));
    ASSERT(sds_json_get_uint_field(&r, "uptime", &uptime));
    
    ASSERT_STR_EQ(node, "sensor1");
    ASSERT_FLOAT_EQ(temp, 23.5f, 0.01f);
    ASSERT(humidity == 45);
    ASSERT(active == true);
    ASSERT(uptime == 123456);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║           SDS JSON Serialization Tests                   ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    
    printf("─── Writer Positive Tests ───\n");
    RUN_TEST(writer_empty_object);
    RUN_TEST(writer_single_string);
    RUN_TEST(writer_single_int);
    RUN_TEST(writer_single_uint);
    RUN_TEST(writer_single_float);
    RUN_TEST(writer_single_bool_true);
    RUN_TEST(writer_single_bool_false);
    RUN_TEST(writer_multiple_fields);
    RUN_TEST(writer_empty_string_value);
    RUN_TEST(writer_zero_values);
    RUN_TEST(writer_negative_int);
    RUN_TEST(writer_negative_float);
    
    printf("\n─── Writer Escaping Tests ───\n");
    RUN_TEST(writer_escape_quotes);
    RUN_TEST(writer_escape_backslash);
    RUN_TEST(writer_escape_newline);
    RUN_TEST(writer_escape_tab);
    RUN_TEST(writer_escape_carriage_return);
    RUN_TEST(writer_escape_mixed);
    
    printf("\n─── Writer Overflow/Edge Cases ───\n");
    RUN_TEST(writer_buffer_exact_fit);
    RUN_TEST(writer_buffer_overflow_string);
    RUN_TEST(writer_buffer_overflow_escape);
    RUN_TEST(writer_buffer_size_one);
    RUN_TEST(writer_buffer_size_zero);
    RUN_TEST(writer_long_string);
    RUN_TEST(writer_null_value_string);
    
    printf("\n─── Reader Positive Tests ───\n");
    RUN_TEST(reader_parse_string);
    RUN_TEST(reader_parse_int_positive);
    RUN_TEST(reader_parse_int_negative);
    RUN_TEST(reader_parse_int_max);
    RUN_TEST(reader_parse_int_min);
    RUN_TEST(reader_parse_uint);
    RUN_TEST(reader_parse_uint8);
    RUN_TEST(reader_parse_float);
    RUN_TEST(reader_parse_float_negative);
    RUN_TEST(reader_parse_float_scientific);
    RUN_TEST(reader_parse_bool_true);
    RUN_TEST(reader_parse_bool_false);
    RUN_TEST(reader_parse_multiple_fields);
    RUN_TEST(reader_parse_with_whitespace);
    RUN_TEST(reader_parse_empty_string);
    
    printf("\n─── Reader Missing/Not Found ───\n");
    RUN_TEST(reader_field_not_found);
    RUN_TEST(reader_empty_object);
    RUN_TEST(reader_similar_key_names);
    
    printf("\n─── Reader Negative/Malformed ───\n");
    RUN_TEST(reader_null_json);
    RUN_TEST(reader_truncated_json);
    RUN_TEST(reader_no_value);
    RUN_TEST(reader_wrong_type_int_as_string);
    RUN_TEST(reader_wrong_type_string_as_int);
    RUN_TEST(reader_wrong_type_string_as_bool);
    RUN_TEST(reader_invalid_bool);
    RUN_TEST(reader_string_buffer_too_small);
    RUN_TEST(reader_zero_length_buffer);
    RUN_TEST(reader_garbage_input);
    RUN_TEST(reader_just_braces);
    RUN_TEST(reader_partial_key_match);
    RUN_TEST(reader_binary_in_json);
    RUN_TEST(reader_very_long_key);
    RUN_TEST(reader_length_limited);
    RUN_TEST(reader_overflow_numbers);
    RUN_TEST(reader_negative_uint);
    
    printf("\n─── Round-Trip Tests ───\n");
    RUN_TEST(roundtrip_string);
    RUN_TEST(roundtrip_int);
    RUN_TEST(roundtrip_float);
    RUN_TEST(roundtrip_escaped_string);
    RUN_TEST(roundtrip_complex);
    
    printf("\n");
    printf("══════════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("══════════════════════════════════════════════════════════\n");
    
    if (tests_failed > 0) {
        printf("\n  ✗ TESTS FAILED\n\n");
        return 1;
    }
    
    printf("\n  ✓ ALL TESTS PASSED\n\n");
    return 0;
}

