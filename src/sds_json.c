/*
 * sds_json.c - Simple JSON Serialization Implementation
 */

#include "sds_json.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

/* ============== JSON Writer ============== */

void sds_json_writer_init(SdsJsonWriter* w, char* buffer, size_t size) {
    w->buffer = buffer;
    w->size = size;
    w->pos = 0;
    w->error = false;
    if (size > 0) {
        buffer[0] = '\0';
    }
}

static void json_append(SdsJsonWriter* w, const char* str) {
    if (w->error) return;
    
    size_t len = strlen(str);
    if (w->pos + len >= w->size) {
        w->error = true;
        return;
    }
    
    memcpy(w->buffer + w->pos, str, len);
    w->pos += len;
    w->buffer[w->pos] = '\0';
}

static void json_append_char(SdsJsonWriter* w, char c) {
    if (w->error) return;
    
    if (w->pos + 1 >= w->size) {
        w->error = true;
        return;
    }
    
    w->buffer[w->pos++] = c;
    w->buffer[w->pos] = '\0';
}

/**
 * Append a hex digit character.
 */
static char hex_digit(int n) {
    return (n < 10) ? ('0' + n) : ('a' + n - 10);
}

/**
 * Append a string with JSON escaping for special characters.
 * Escapes: " \ / backspace formfeed newline carriage-return tab
 * Control characters (0x00-0x1F) are escaped as \uXXXX.
 */
static void json_append_escaped(SdsJsonWriter* w, const char* str) {
    if (w->error || !str) return;
    
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        
        /* Check if we need to escape this character */
        const char* escape_seq = NULL;
        switch (c) {
            case '"':  escape_seq = "\\\""; break;
            case '\\': escape_seq = "\\\\"; break;
            case '\b': escape_seq = "\\b";  break;
            case '\f': escape_seq = "\\f";  break;
            case '\n': escape_seq = "\\n";  break;
            case '\r': escape_seq = "\\r";  break;
            case '\t': escape_seq = "\\t";  break;
            default:
                if (c < 0x20) {
                    /* Control characters: escape as \u00XX */
                    if (w->pos + 6 >= w->size) {
                        w->error = true;
                        return;
                    }
                    w->buffer[w->pos++] = '\\';
                    w->buffer[w->pos++] = 'u';
                    w->buffer[w->pos++] = '0';
                    w->buffer[w->pos++] = '0';
                    w->buffer[w->pos++] = hex_digit((c >> 4) & 0xF);
                    w->buffer[w->pos++] = hex_digit(c & 0xF);
                    w->buffer[w->pos] = '\0';
                } else {
                    json_append_char(w, c);
                }
                continue;
        }
        
        /* Append the 2-char escape sequence */
        if (escape_seq) {
            /* Check we have room for 2 chars */
            if (w->pos + 2 >= w->size) {
                w->error = true;
                return;
            }
            w->buffer[w->pos++] = escape_seq[0];
            w->buffer[w->pos++] = escape_seq[1];
            w->buffer[w->pos] = '\0';
        }
    }
}

static bool needs_comma(SdsJsonWriter* w) {
    /* Need comma if last char is not { or [ */
    if (w->pos == 0) return false;
    char last = w->buffer[w->pos - 1];
    return last != '{' && last != '[';
}

void sds_json_start_object(SdsJsonWriter* w) {
    json_append_char(w, '{');
}

void sds_json_end_object(SdsJsonWriter* w) {
    json_append_char(w, '}');
}

void sds_json_add_string(SdsJsonWriter* w, const char* key, const char* value) {
    if (needs_comma(w)) json_append_char(w, ',');
    json_append_char(w, '"');
    json_append(w, key);  /* Keys don't need escaping - they're from code */
    json_append(w, "\":\"");
    json_append_escaped(w, value);  /* Values need escaping - they're user data */
    json_append_char(w, '"');
}

void sds_json_add_int(SdsJsonWriter* w, const char* key, int32_t value) {
    if (needs_comma(w)) json_append_char(w, ',');
    
    char num[16];
    snprintf(num, sizeof(num), "%d", value);
    
    json_append_char(w, '"');
    json_append(w, key);
    json_append(w, "\":");
    json_append(w, num);
}

void sds_json_add_uint(SdsJsonWriter* w, const char* key, uint32_t value) {
    if (needs_comma(w)) json_append_char(w, ',');
    
    char num[16];
    snprintf(num, sizeof(num), "%u", value);
    
    json_append_char(w, '"');
    json_append(w, key);
    json_append(w, "\":");
    json_append(w, num);
}

void sds_json_add_float(SdsJsonWriter* w, const char* key, float value) {
    if (needs_comma(w)) json_append_char(w, ',');
    
    char num[32];
    snprintf(num, sizeof(num), "%.4f", value);
    
    json_append_char(w, '"');
    json_append(w, key);
    json_append(w, "\":");
    json_append(w, num);
}

void sds_json_add_bool(SdsJsonWriter* w, const char* key, bool value) {
    if (needs_comma(w)) json_append_char(w, ',');
    
    json_append_char(w, '"');
    json_append(w, key);
    json_append(w, "\":");
    json_append(w, value ? "true" : "false");
}

const char* sds_json_get_string(SdsJsonWriter* w) {
    return w->buffer;
}

size_t sds_json_get_length(SdsJsonWriter* w) {
    return w->pos;
}

bool sds_json_has_error(SdsJsonWriter* w) {
    return w->error;
}

/* ============== JSON Reader ============== */

void sds_json_reader_init(SdsJsonReader* r, const char* json, size_t len) {
    r->json = json;
    r->len = len;
    r->pos = 0;
}

static void skip_whitespace(const char** p, const char* end) {
    while (*p < end && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

const char* sds_json_find_field(SdsJsonReader* r, const char* key) {
    if (!r->json || r->len == 0 || !key) {
        return NULL;
    }
    
    const char* p = r->json;
    const char* end = r->json + r->len;
    size_t key_len = strlen(key);
    
    while (p < end) {
        /* Find opening quote for key */
        while (p < end && *p != '"') p++;
        if (p >= end) return NULL;
        p++;  /* Skip quote */
        
        /* Check if this is our key */
        if (p + key_len < end && 
            strncmp(p, key, key_len) == 0 && 
            p[key_len] == '"') {
            
            p += key_len + 1;  /* Skip key and closing quote */
            
            /* Skip whitespace and colon */
            skip_whitespace(&p, end);
            if (p >= end || *p != ':') return NULL;
            p++;
            skip_whitespace(&p, end);
            
            return p;  /* Return pointer to value */
        }
        
        /* Skip to end of this key */
        while (p < end && *p != '"') p++;
        if (p < end) p++;  /* Skip closing quote */
    }
    
    return NULL;
}

/**
 * Decode a JSON escape sequence.
 * 
 * @param src Pointer to character after backslash
 * @param out Output character
 * @return Number of source characters consumed (1 for simple escapes), or 0 on error
 */
static size_t decode_escape(const char* src, char* out) {
    switch (*src) {
        case '"':  *out = '"';  return 1;
        case '\\': *out = '\\'; return 1;
        case '/':  *out = '/';  return 1;
        case 'b':  *out = '\b'; return 1;
        case 'f':  *out = '\f'; return 1;
        case 'n':  *out = '\n'; return 1;
        case 'r':  *out = '\r'; return 1;
        case 't':  *out = '\t'; return 1;
        case 'u':
            /* \uXXXX - for embedded systems, we skip full unicode support
             * and just pass through as '?' for non-ASCII values */
            if (src[1] && src[2] && src[3] && src[4]) {
                /* Parse 4 hex digits */
                unsigned int codepoint = 0;
                for (int j = 1; j <= 4; j++) {
                    char c = src[j];
                    codepoint <<= 4;
                    if (c >= '0' && c <= '9') codepoint |= (c - '0');
                    else if (c >= 'a' && c <= 'f') codepoint |= (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') codepoint |= (c - 'A' + 10);
                    else return 0;  /* Invalid hex digit */
                }
                /* For ASCII range, output directly; otherwise use '?' */
                *out = (codepoint < 128) ? (char)codepoint : '?';
                return 5;  /* u + 4 hex digits */
            }
            return 0;  /* Incomplete \uXXXX */
        default:
            return 0;  /* Unknown escape */
    }
}

/**
 * Parse a JSON string value with bounds checking and escape sequence handling.
 * 
 * @param value Pointer to opening quote of the string value
 * @param max_len Maximum bytes to read from value (remaining in buffer)
 * @param out Output buffer for the string
 * @param out_size Size of output buffer
 * @return true if successfully parsed, false on error
 */
static bool parse_string_bounded(const char* value, size_t max_len, char* out, size_t out_size) {
    if (!value || max_len < 2 || *value != '"') return false;
    
    /* Handle zero-size buffer gracefully */
    if (out_size == 0) {
        return false;
    }
    
    value++;  /* Skip opening quote */
    max_len--;
    size_t i = 0;
    
    /* Read until closing quote, respecting both max_len and out_size */
    while (max_len > 0 && *value != '"' && *value != '\0' && i < out_size - 1) {
        if (*value == '\\' && max_len > 1) {
            /* Handle escape sequence */
            char decoded;
            size_t consumed = decode_escape(value + 1, &decoded);
            if (consumed > 0 && consumed < max_len) {
                out[i++] = decoded;
                value += 1 + consumed;  /* Skip backslash + escape chars */
                max_len -= 1 + consumed;
            } else {
                /* Invalid escape - copy literally */
                out[i++] = *value++;
                max_len--;
            }
        } else {
            out[i++] = *value++;
            max_len--;
        }
    }
    out[i] = '\0';
    
    /* Return true only if we found the closing quote */
    return (max_len > 0 && *value == '"');
}

bool sds_json_parse_string(const char* value, char* out, size_t out_size) {
    /* Legacy API - assume null-terminated string (less safe) */
    if (!value || *value != '"') return false;
    
    /* Handle zero-size buffer gracefully */
    if (out_size == 0) {
        return false;
    }
    
    value++;  /* Skip opening quote */
    size_t i = 0;
    
    while (*value && *value != '"' && i < out_size - 1) {
        if (*value == '\\' && value[1]) {
            /* Handle escape sequence */
            char decoded;
            size_t consumed = decode_escape(value + 1, &decoded);
            if (consumed > 0) {
                out[i++] = decoded;
                value += 1 + consumed;  /* Skip backslash + escape chars */
            } else {
                /* Invalid escape - copy literally */
                out[i++] = *value++;
            }
        } else {
            out[i++] = *value++;
        }
    }
    out[i] = '\0';
    
    return true;
}

bool sds_json_parse_int(const char* value, int32_t* out) {
    if (!value) return false;
    
    errno = 0;
    char* endptr;
    long val = strtol(value, &endptr, 10);
    
    /* Check for parse error */
    if (endptr == value) return false;
    
    /* Check for overflow/underflow */
    if (errno == ERANGE) return false;
    
    /* Check if value fits in int32_t (important on 64-bit systems where long > 32 bits) */
    if (val < INT32_MIN || val > INT32_MAX) return false;
    
    *out = (int32_t)val;
    return true;
}

bool sds_json_parse_uint(const char* value, uint32_t* out) {
    if (!value) return false;
    
    /* Check for negative sign - strtoul accepts negative numbers */
    while (isspace((unsigned char)*value)) value++;
    if (*value == '-') return false;
    
    errno = 0;
    char* endptr;
    unsigned long val = strtoul(value, &endptr, 10);
    
    /* Check for parse error */
    if (endptr == value) return false;
    
    /* Check for overflow */
    if (errno == ERANGE) return false;
    
    /* Check if value fits in uint32_t (important on 64-bit systems where long > 32 bits) */
    if (val > UINT32_MAX) return false;
    
    *out = (uint32_t)val;
    return true;
}

bool sds_json_parse_float(const char* value, float* out) {
    if (!value) return false;
    
    char* endptr;
    float val = strtof(value, &endptr);
    if (endptr == value) return false;
    
    *out = val;
    return true;
}

bool sds_json_parse_bool(const char* value, bool* out) {
    if (!value) return false;
    
    if (strncmp(value, "true", 4) == 0) {
        *out = true;
        return true;
    } else if (strncmp(value, "false", 5) == 0) {
        *out = false;
        return true;
    }
    
    return false;
}

bool sds_json_get_string_field(SdsJsonReader* r, const char* key, char* out, size_t out_size) {
    const char* value = sds_json_find_field(r, key);
    if (!value) return false;
    
    /* Calculate remaining bytes in the JSON buffer from value position */
    size_t remaining = r->len - (size_t)(value - r->json);
    return parse_string_bounded(value, remaining, out, out_size);
}

bool sds_json_get_int_field(SdsJsonReader* r, const char* key, int32_t* out) {
    const char* value = sds_json_find_field(r, key);
    if (!value) return false;
    return sds_json_parse_int(value, out);
}

bool sds_json_get_uint_field(SdsJsonReader* r, const char* key, uint32_t* out) {
    const char* value = sds_json_find_field(r, key);
    if (!value) return false;
    return sds_json_parse_uint(value, out);
}

bool sds_json_get_float_field(SdsJsonReader* r, const char* key, float* out) {
    const char* value = sds_json_find_field(r, key);
    if (!value) return false;
    return sds_json_parse_float(value, out);
}

bool sds_json_get_bool_field(SdsJsonReader* r, const char* key, bool* out) {
    const char* value = sds_json_find_field(r, key);
    if (!value) return false;
    return sds_json_parse_bool(value, out);
}

bool sds_json_get_uint8_field(SdsJsonReader* r, const char* key, uint8_t* out) {
    uint32_t val;
    if (!sds_json_get_uint_field(r, key, &val)) return false;
    
    /* Check if value fits in uint8_t */
    if (val > 255) return false;
    
    *out = (uint8_t)val;
    return true;
}

