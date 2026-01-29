"""
SDS (Synchronized Data Structures) Python Library

A Pythonic interface for IoT state synchronization over MQTT.
This is a wrapper around the SDS C library using CFFI.

Example usage:
    >>> from sds import SdsNode, Role, Field, FieldType
    >>> from dataclasses import dataclass
    >>> 
    >>> @dataclass
    ... class SensorState:
    ...     temperature: float = Field(float32=True)
    ...     humidity: float = Field(float32=True)
    >>> 
    >>> with SdsNode("sensor_01", "localhost") as node:
    ...     table = node.register_table(
    ...         "SensorData", Role.DEVICE,
    ...         state_schema=SensorState,
    ...     )
    ...     
    ...     # C-like attribute access
    ...     table.state.temperature = 23.5
    ...     table.state.humidity = 65.0
    ...     
    ...     while True:
    ...         node.poll(timeout_ms=1000)
"""

__version__ = "1.0.0"
__author__ = "SDS Team"

# Core classes
from sds.node import SdsNode
from sds.table import SdsTable, SectionProxy, DeviceView

# Enums
from sds.types import Role, ErrorCode, LogLevel

# Exceptions
from sds.types import (
    SdsError,
    SdsNotInitializedError,
    SdsAlreadyInitializedError,
    SdsConfigError,
    SdsMqttError,
    SdsTableError,
    SdsCapacityError,
    SdsPlatformError,
    SdsValidationError,
    check_error,
)

# Table helpers
from sds.tables import Field, FieldType

# Logging configuration
from sds._logging import configure_logging

# Public API
__all__ = [
    # Version
    "__version__",
    
    # Core classes
    "SdsNode",
    "SdsTable",
    "SectionProxy",
    "DeviceView",
    
    # Enums
    "Role",
    "ErrorCode",
    "LogLevel",
    
    # Exceptions
    "SdsError",
    "SdsNotInitializedError",
    "SdsAlreadyInitializedError",
    "SdsConfigError",
    "SdsMqttError",
    "SdsTableError",
    "SdsCapacityError",
    "SdsPlatformError",
    "SdsValidationError",
    "check_error",
    
    # Table helpers
    "Field",
    "FieldType",
    
    # Utility functions
    "get_version",
    "get_c_library_version",
    "set_log_level",
    "get_log_level",
    "configure_logging",
]


def get_version() -> str:
    """Get the SDS Python library version."""
    return __version__


def get_c_library_version() -> str:
    """
    Get the SDS C library version.
    
    Note: This requires the CFFI extension to be built.
    """
    try:
        from sds.node import lib
        return "1.0.0"  # TODO: Add version function to C library
    except ImportError:
        return "unknown (extension not built)"


def set_log_level(level: LogLevel) -> None:
    """
    Set the runtime log level.
    
    Controls which log messages are output. Can be called at any time,
    even before creating an SdsNode.
    
    Args:
        level: The minimum log level to output (inclusive)
    
    Example:
        >>> from sds import set_log_level, LogLevel
        >>> set_log_level(LogLevel.DEBUG)  # Enable all logs
        >>> set_log_level(LogLevel.ERROR)  # Only errors
        >>> set_log_level(LogLevel.NONE)   # Disable all logs
    """
    from sds._bindings import lib
    lib.sds_set_log_level(level.value)


def get_log_level() -> LogLevel:
    """
    Get the current runtime log level.
    
    Returns:
        The current log level
    
    Example:
        >>> from sds import get_log_level, LogLevel
        >>> level = get_log_level()
        >>> print(f"Current log level: {level.name}")
    """
    from sds._bindings import lib
    return LogLevel(lib.sds_get_log_level())
