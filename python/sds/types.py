"""
Type definitions, enums, and exceptions for the SDS Python wrapper.
"""
from __future__ import annotations

from enum import IntEnum
from typing import Optional


class Role(IntEnum):
    """
    Role of a node for a particular table.
    
    Each table registration specifies a role that determines:
    - Which sections the node can write to
    - Which MQTT topics it subscribes to and publishes on
    
    Attributes:
        OWNER: Publishes config, receives state/status from devices
        DEVICE: Receives config, publishes state/status
    """
    OWNER = 0
    DEVICE = 1


class LogLevel(IntEnum):
    """
    Log level for controlling SDS output.
    
    Higher values include all lower levels. For example, INFO
    will log INFO, WARN, and ERROR messages.
    
    Attributes:
        NONE: Disable all logging
        ERROR: Error conditions only
        WARN: Warnings and errors
        INFO: Informational messages, warnings, and errors
        DEBUG: All messages including debug
    """
    NONE = 0
    ERROR = 1
    WARN = 2
    INFO = 3
    DEBUG = 4


class ErrorCode(IntEnum):
    """
    SDS error codes.
    
    These correspond to the SdsError enum in the C library.
    """
    OK = 0
    
    # Initialization errors
    NOT_INITIALIZED = 1
    ALREADY_INITIALIZED = 2
    INVALID_CONFIG = 3
    
    # Connection errors
    MQTT_CONNECT_FAILED = 4
    MQTT_DISCONNECTED = 5
    
    # Table errors
    TABLE_NOT_FOUND = 6
    TABLE_ALREADY_REGISTERED = 7
    MAX_TABLES_REACHED = 8
    INVALID_TABLE = 9
    
    # Role errors
    INVALID_ROLE = 10
    OWNER_EXISTS = 11
    
    # Capacity errors
    MAX_NODES_REACHED = 12
    BUFFER_FULL = 13
    SECTION_TOO_LARGE = 14
    
    # Platform errors
    PLATFORM_NOT_SET = 15
    PLATFORM_ERROR = 16


# ============== Exceptions ==============


class SdsError(Exception):
    """
    Base exception for all SDS errors.
    
    Attributes:
        code: The error code from the C library
        message: Human-readable error message
    """
    
    def __init__(self, code: int, message: Optional[str] = None):
        self.code = ErrorCode(code) if code in ErrorCode._value2member_map_ else code
        if message is None:
            # Try to get error string from library
            try:
                from sds._bindings import get_error_string
                message = get_error_string(code)
            except ImportError:
                message = f"SDS error code {code}"
        self.message = message
        super().__init__(f"{self.message} (code={self.code})")
    
    @classmethod
    def from_code(cls, code: int) -> "SdsError":
        """
        Create the appropriate exception subclass for an error code.
        
        Args:
            code: Error code from C library
            
        Returns:
            Appropriate SdsError subclass instance
            
        Raises:
            Nothing if code is OK (returns None in that case)
        """
        if code == ErrorCode.OK:
            return None  # type: ignore
        
        # Map error codes to specific exception types
        error_map = {
            ErrorCode.NOT_INITIALIZED: SdsNotInitializedError,
            ErrorCode.ALREADY_INITIALIZED: SdsAlreadyInitializedError,
            ErrorCode.INVALID_CONFIG: SdsConfigError,
            ErrorCode.MQTT_CONNECT_FAILED: SdsMqttError,
            ErrorCode.MQTT_DISCONNECTED: SdsMqttError,
            ErrorCode.TABLE_NOT_FOUND: SdsTableError,
            ErrorCode.TABLE_ALREADY_REGISTERED: SdsTableError,
            ErrorCode.MAX_TABLES_REACHED: SdsTableError,
            ErrorCode.INVALID_TABLE: SdsTableError,
            ErrorCode.INVALID_ROLE: SdsTableError,
            ErrorCode.OWNER_EXISTS: SdsTableError,
            ErrorCode.MAX_NODES_REACHED: SdsCapacityError,
            ErrorCode.BUFFER_FULL: SdsCapacityError,
            ErrorCode.SECTION_TOO_LARGE: SdsCapacityError,
            ErrorCode.PLATFORM_NOT_SET: SdsPlatformError,
            ErrorCode.PLATFORM_ERROR: SdsPlatformError,
        }
        
        exception_class = error_map.get(code, cls)
        return exception_class(code)


class SdsNotInitializedError(SdsError):
    """Raised when SDS operations are attempted before initialization."""
    pass


class SdsAlreadyInitializedError(SdsError):
    """Raised when sds_init() is called twice."""
    pass


class SdsConfigError(SdsError):
    """Raised when invalid configuration is provided."""
    pass


class SdsMqttError(SdsError):
    """Raised on MQTT connection or communication errors."""
    pass


class SdsTableError(SdsError):
    """Raised on table registration or lookup errors."""
    pass


class SdsCapacityError(SdsError):
    """Raised when capacity limits are exceeded."""
    pass


class SdsPlatformError(SdsError):
    """Raised on platform-specific errors."""
    pass


def check_error(code: int) -> None:
    """
    Check an error code and raise an exception if it's not OK.
    
    Args:
        code: Error code from C library
        
    Raises:
        SdsError: If code is not SDS_OK
    """
    if code != ErrorCode.OK:
        exc = SdsError.from_code(code)
        if exc is not None:
            raise exc
