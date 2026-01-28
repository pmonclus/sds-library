"""
Low-level CFFI bindings for the SDS library.

This module provides direct access to the C library functions.
Most users should use the higher-level sds.SdsNode class instead.
"""
from __future__ import annotations

import sys
from typing import TYPE_CHECKING

# Try to import the compiled CFFI extension
try:
    from sds._sds_cffi import ffi, lib
except ImportError as e:
    # Provide helpful error message
    raise ImportError(
        "SDS CFFI extension not found. Please build the extension first:\n"
        "  cd python && pip install -e .\n"
        "or:\n"
        "  cd python && python sds/_build_ffi.py\n"
        f"Original error: {e}"
    ) from e


# Re-export ffi and lib for direct access
__all__ = [
    "ffi",
    "lib",
    "SdsErrorCode",
    "SdsRoleCode",
    "encode_string",
    "decode_string",
]


class SdsErrorCode:
    """Error code constants from the C library."""
    
    OK = lib.SDS_OK
    NOT_INITIALIZED = lib.SDS_ERR_NOT_INITIALIZED
    ALREADY_INITIALIZED = lib.SDS_ERR_ALREADY_INITIALIZED
    INVALID_CONFIG = lib.SDS_ERR_INVALID_CONFIG
    MQTT_CONNECT_FAILED = lib.SDS_ERR_MQTT_CONNECT_FAILED
    MQTT_DISCONNECTED = lib.SDS_ERR_MQTT_DISCONNECTED
    TABLE_NOT_FOUND = lib.SDS_ERR_TABLE_NOT_FOUND
    TABLE_ALREADY_REGISTERED = lib.SDS_ERR_TABLE_ALREADY_REGISTERED
    MAX_TABLES_REACHED = lib.SDS_ERR_MAX_TABLES_REACHED
    INVALID_TABLE = lib.SDS_ERR_INVALID_TABLE
    INVALID_ROLE = lib.SDS_ERR_INVALID_ROLE
    OWNER_EXISTS = lib.SDS_ERR_OWNER_EXISTS
    MAX_NODES_REACHED = lib.SDS_ERR_MAX_NODES_REACHED
    BUFFER_FULL = lib.SDS_ERR_BUFFER_FULL
    SECTION_TOO_LARGE = lib.SDS_ERR_SECTION_TOO_LARGE
    PLATFORM_NOT_SET = lib.SDS_ERR_PLATFORM_NOT_SET
    PLATFORM_ERROR = lib.SDS_ERR_PLATFORM_ERROR


class SdsRoleCode:
    """Role code constants from the C library."""
    
    OWNER = lib.SDS_ROLE_OWNER
    DEVICE = lib.SDS_ROLE_DEVICE


def encode_string(s: str | None) -> bytes | ffi.CData:
    """
    Encode a Python string to a C string (char*).
    
    Args:
        s: Python string or None
        
    Returns:
        C string (ffi.new("char[]")) or ffi.NULL if input is None
    """
    if s is None:
        return ffi.NULL
    return s.encode("utf-8")


def decode_string(c_str: ffi.CData) -> str | None:
    """
    Decode a C string (char*) to a Python string.
    
    Args:
        c_str: C string pointer
        
    Returns:
        Python string or None if pointer is NULL
    """
    if c_str == ffi.NULL:
        return None
    return ffi.string(c_str).decode("utf-8")


def get_error_string(error_code: int) -> str:
    """Get human-readable error message for an error code."""
    c_str = lib.sds_error_string(error_code)
    return decode_string(c_str) or f"Unknown error ({error_code})"
