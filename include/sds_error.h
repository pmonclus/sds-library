/*
 * sds_error.h - SDS Error Codes
 */

#ifndef SDS_ERROR_H
#define SDS_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDS_OK = 0,
    
    /* Initialization errors */
    SDS_ERR_NOT_INITIALIZED,        /* sds_init() not called */
    SDS_ERR_ALREADY_INITIALIZED,    /* sds_init() called twice */
    SDS_ERR_INVALID_CONFIG,         /* Invalid configuration */
    
    /* Connection errors */
    SDS_ERR_MQTT_CONNECT_FAILED,    /* Failed to connect to MQTT broker */
    SDS_ERR_MQTT_DISCONNECTED,      /* Lost connection to broker */
    
    /* Table errors */
    SDS_ERR_TABLE_NOT_FOUND,        /* Table type not registered */
    SDS_ERR_TABLE_ALREADY_REGISTERED, /* Table type already registered */
    SDS_ERR_MAX_TABLES_REACHED,     /* Too many tables registered */
    SDS_ERR_INVALID_TABLE,          /* Invalid table pointer or type */
    
    /* Role errors */
    SDS_ERR_INVALID_ROLE,           /* Invalid role specified */
    SDS_ERR_OWNER_EXISTS,           /* Another node is already owner */
    
    /* Capacity errors */
    SDS_ERR_MAX_NODES_REACHED,      /* Owner's node slots full */
    SDS_ERR_BUFFER_FULL,            /* Message buffer too small */
    
    /* Platform errors */
    SDS_ERR_PLATFORM_NOT_SET,       /* Platform not configured */
    SDS_ERR_PLATFORM_ERROR,         /* Platform-specific error */
    
} SdsError;

/**
 * Get human-readable error message.
 * 
 * @param error Error code
 * @return Static string describing the error
 */
const char* sds_error_string(SdsError error);

#ifdef __cplusplus
}
#endif

#endif /* SDS_ERROR_H */

