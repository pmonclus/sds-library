/*
 * sds_json.h - Simple JSON Serialization for SDS
 * 
 * Minimal JSON helpers for serializing/deserializing table data.
 * Uses a simple approach suitable for embedded systems.
 */

#ifndef SDS_JSON_H
#define SDS_JSON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== JSON Writer ============== */

typedef struct {
    char* buffer;
    size_t size;
    size_t pos;
    bool error;
} SdsJsonWriter;

/* Initialize a JSON writer with a buffer */
void sds_json_writer_init(SdsJsonWriter* w, char* buffer, size_t size);

/* Start/end object */
void sds_json_start_object(SdsJsonWriter* w);
void sds_json_end_object(SdsJsonWriter* w);

/* Add fields */
void sds_json_add_string(SdsJsonWriter* w, const char* key, const char* value);
void sds_json_add_int(SdsJsonWriter* w, const char* key, int32_t value);
void sds_json_add_uint(SdsJsonWriter* w, const char* key, uint32_t value);
void sds_json_add_float(SdsJsonWriter* w, const char* key, float value);
void sds_json_add_bool(SdsJsonWriter* w, const char* key, bool value);

/* Get result */
const char* sds_json_get_string(SdsJsonWriter* w);
size_t sds_json_get_length(SdsJsonWriter* w);
bool sds_json_has_error(SdsJsonWriter* w);

/* ============== JSON Reader ============== */

typedef struct {
    const char* json;
    size_t len;
    size_t pos;
} SdsJsonReader;

/* Initialize a JSON reader */
void sds_json_reader_init(SdsJsonReader* r, const char* json, size_t len);

/* Find a field value (returns pointer to value start, or NULL) */
const char* sds_json_find_field(SdsJsonReader* r, const char* key);

/* Parse values (call after find_field) */
bool sds_json_parse_string(const char* value, char* out, size_t out_size);
bool sds_json_parse_int(const char* value, int32_t* out);
bool sds_json_parse_uint(const char* value, uint32_t* out);
bool sds_json_parse_float(const char* value, float* out);
bool sds_json_parse_bool(const char* value, bool* out);

/* Helper: get field value directly */
bool sds_json_get_string_field(SdsJsonReader* r, const char* key, char* out, size_t out_size);
bool sds_json_get_int_field(SdsJsonReader* r, const char* key, int32_t* out);
bool sds_json_get_uint_field(SdsJsonReader* r, const char* key, uint32_t* out);
bool sds_json_get_float_field(SdsJsonReader* r, const char* key, float* out);
bool sds_json_get_bool_field(SdsJsonReader* r, const char* key, bool* out);
bool sds_json_get_uint8_field(SdsJsonReader* r, const char* key, uint8_t* out);

#ifdef __cplusplus
}
#endif

#endif /* SDS_JSON_H */

