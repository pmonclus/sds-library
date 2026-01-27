/*
 * fuzz_json_parser.c - Fuzz target for JSON reader/writer
 * 
 * Tests sds_json_reader and sds_json_writer with adversarial input.
 * 
 * Build with AFL++:
 *   afl-clang-fast -g -O1 -I../../include \
 *       -o fuzz_json fuzz_json_parser.c ../../src/sds_json.c -lm
 * 
 * Build with libFuzzer (clang):
 *   clang -g -O1 -fsanitize=fuzzer,address -I../../include \
 *       -DUSE_LIBFUZZER -o fuzz_json fuzz_json_parser.c ../../src/sds_json.c -lm
 * 
 * Run AFL:
 *   afl-fuzz -i corpus/json -o fuzz_out_json ./fuzz_json
 * 
 * Run libFuzzer:
 *   ./fuzz_json corpus/json
 */

#include "sds_json.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============== Fuzz Entry Point ============== */

static int fuzz_one_input(const uint8_t* data, size_t size) {
    if (size == 0) {
        return 0;
    }
    
    /* Initialize reader with fuzzed data */
    SdsJsonReader r;
    sds_json_reader_init(&r, (const char*)data, size);
    
    /* Try to read various field types with different key names */
    char str_buf[256];
    int32_t i32;
    uint32_t u32;
    uint8_t u8;
    float f;
    bool b;
    
    /* Common key names */
    sds_json_get_string_field(&r, "name", str_buf, sizeof(str_buf));
    sds_json_get_string_field(&r, "value", str_buf, sizeof(str_buf));
    sds_json_get_string_field(&r, "id", str_buf, sizeof(str_buf));
    sds_json_get_string_field(&r, "type", str_buf, sizeof(str_buf));
    sds_json_get_string_field(&r, "msg", str_buf, sizeof(str_buf));
    
    sds_json_get_int_field(&r, "count", &i32);
    sds_json_get_int_field(&r, "index", &i32);
    sds_json_get_int_field(&r, "offset", &i32);
    
    sds_json_get_uint_field(&r, "id", &u32);
    sds_json_get_uint_field(&r, "size", &u32);
    sds_json_get_uint_field(&r, "len", &u32);
    
    sds_json_get_uint8_field(&r, "mode", &u8);
    sds_json_get_uint8_field(&r, "byte", &u8);
    sds_json_get_uint8_field(&r, "flags", &u8);
    
    sds_json_get_float_field(&r, "temp", &f);
    sds_json_get_float_field(&r, "value", &f);
    sds_json_get_float_field(&r, "threshold", &f);
    
    sds_json_get_bool_field(&r, "active", &b);
    sds_json_get_bool_field(&r, "enabled", &b);
    sds_json_get_bool_field(&r, "online", &b);
    
    /* Try with very small buffer */
    char tiny[4];
    sds_json_get_string_field(&r, "s", tiny, sizeof(tiny));
    
    /* Try with buffer size 1 */
    char one[1];
    sds_json_get_string_field(&r, "x", one, sizeof(one));
    
    /* Re-init and try with key derived from input */
    if (size >= 8) {
        sds_json_reader_init(&r, (const char*)data, size);
        
        /* Use first 8 bytes as a key (may contain non-printable chars) */
        char derived_key[9] = {0};
        memcpy(derived_key, data, 8);
        
        sds_json_get_string_field(&r, derived_key, str_buf, sizeof(str_buf));
        sds_json_get_int_field(&r, derived_key, &i32);
        sds_json_get_float_field(&r, derived_key, &f);
    }
    
    /* Test JSON writer with constrained buffers */
    if (size >= 16) {
        /* Use part of input to determine buffer size */
        size_t buf_size = (data[0] % 64) + 1;  /* 1-64 bytes */
        char* write_buf = malloc(buf_size);
        
        if (write_buf) {
            SdsJsonWriter w;
            sds_json_writer_init(&w, write_buf, buf_size);
            
            sds_json_start_object(&w);
            
            /* Write fields using parts of input */
            if (size >= 32) {
                char key[17] = {0};
                memcpy(key, data + 16, 16);
                sds_json_add_string(&w, key, (const char*)(data + 32));
            }
            
            sds_json_add_int(&w, "n", (int32_t)(data[1] | (data[2] << 8)));
            sds_json_add_uint(&w, "u", (uint32_t)(data[3] | (data[4] << 8)));
            sds_json_add_bool(&w, "b", data[5] & 1);
            
            sds_json_end_object(&w);
            
            /* Check error state - shouldn't crash regardless */
            (void)sds_json_has_error(&w);
            
            free(write_buf);
        }
    }
    
    return 0;
}

/* ============== Main / LibFuzzer Entry ============== */

#ifdef USE_LIBFUZZER
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
    
    return 0;
}
#endif
