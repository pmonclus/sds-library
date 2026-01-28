"""
SdsNode - High-level Pythonic interface for SDS.

This module provides the main SdsNode class that wraps the C library
with a clean, Pythonic API including context managers and decorators.
"""
from __future__ import annotations

import weakref
from typing import Any, Callable, Dict, Optional, Type, Union, TYPE_CHECKING

from sds.types import Role, ErrorCode, SdsError, check_error

# Import CFFI bindings (will fail if extension not built)
from sds._bindings import ffi, lib, encode_string, decode_string

# Import table wrapper (import here to avoid circular imports)
from sds.table import SdsTable


# Type aliases for callbacks
ConfigCallback = Callable[[str], None]
StateCallback = Callable[[str, str], None]
StatusCallback = Callable[[str, str], None]
ErrorCallback = Callable[[int, str], None]
VersionMismatchCallback = Callable[[str, str, str, str], bool]


class SdsNode:
    """
    High-level interface for an SDS node.
    
    Provides a Pythonic wrapper around the SDS C library with:
    - Context manager support (with statement)
    - Decorator-based callback registration
    - Automatic resource cleanup
    
    Example:
        >>> with SdsNode("sensor_01", "localhost") as node:
        ...     node.register_table("SensorData", Role.DEVICE)
        ...     while True:
        ...         node.poll(timeout_ms=1000)
    
    Attributes:
        node_id: The unique identifier for this node
        broker_host: MQTT broker hostname
        port: MQTT broker port
    """
    
    # Class-level storage for callback instances to prevent GC
    _instances: weakref.WeakSet["SdsNode"] = weakref.WeakSet()
    _current_instance: Optional["SdsNode"] = None
    
    def __init__(
        self,
        node_id: str,
        broker_host: str,
        port: int = 1883,
        username: Optional[str] = None,
        password: Optional[str] = None,
        *,
        auto_init: bool = True,
    ):
        """
        Create an SDS node.
        
        Args:
            node_id: Unique identifier for this node
            broker_host: MQTT broker hostname or IP address
            port: MQTT broker port (default: 1883)
            username: MQTT username for authentication (optional)
            password: MQTT password for authentication (optional)
            auto_init: If True, automatically call init() (default: True)
        
        Raises:
            SdsError: If auto_init is True and initialization fails
        """
        self._node_id = node_id
        self._broker_host = broker_host
        self._port = port
        self._username = username
        self._password = password
        
        self._initialized = False
        self._tables: Dict[str, Dict[str, Any]] = {}
        
        # Callback storage (keyed by table_type)
        self._config_callbacks: Dict[str, ConfigCallback] = {}
        self._state_callbacks: Dict[str, StateCallback] = {}
        self._status_callbacks: Dict[str, StatusCallback] = {}
        self._error_callback: Optional[ErrorCallback] = None
        self._version_mismatch_callback: Optional[VersionMismatchCallback] = None
        
        # Keep C callback handles alive
        self._c_callbacks: Dict[str, Any] = {}
        
        # Register this instance
        SdsNode._instances.add(self)
        
        if auto_init:
            self.init()
    
    @property
    def node_id(self) -> str:
        """Get the node ID."""
        return self._node_id
    
    @property
    def broker_host(self) -> str:
        """Get the MQTT broker hostname."""
        return self._broker_host
    
    @property
    def port(self) -> int:
        """Get the MQTT broker port."""
        return self._port
    
    def init(self) -> None:
        """
        Initialize SDS and connect to the MQTT broker.
        
        This is called automatically if auto_init=True (the default).
        
        Raises:
            SdsError: If initialization fails
            SdsAlreadyInitializedError: If already initialized
        """
        if self._initialized:
            raise SdsError.from_code(ErrorCode.ALREADY_INITIALIZED)
        
        # Create config struct
        config = ffi.new("SdsConfig*")
        config.node_id = ffi.new("char[]", self._node_id.encode("utf-8"))
        config.mqtt_broker = ffi.new("char[]", self._broker_host.encode("utf-8"))
        config.mqtt_port = self._port
        
        if self._username is not None:
            config.mqtt_username = ffi.new("char[]", self._username.encode("utf-8"))
        else:
            config.mqtt_username = ffi.NULL
        
        if self._password is not None:
            config.mqtt_password = ffi.new("char[]", self._password.encode("utf-8"))
        else:
            config.mqtt_password = ffi.NULL
        
        # Keep config alive
        self._config = config
        
        # Set as current instance for callbacks
        SdsNode._current_instance = self
        
        # Initialize
        result = lib.sds_init(config)
        check_error(result)
        
        self._initialized = True
    
    def shutdown(self) -> None:
        """
        Shutdown SDS and disconnect from the MQTT broker.
        
        This is called automatically when exiting a context manager.
        Safe to call multiple times.
        """
        if self._initialized:
            lib.sds_shutdown()
            self._initialized = False
            self._tables.clear()
            
            if SdsNode._current_instance is self:
                SdsNode._current_instance = None
    
    def __enter__(self) -> "SdsNode":
        """Enter context manager."""
        return self
    
    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        """Exit context manager - cleanup resources."""
        self.shutdown()
    
    def __del__(self) -> None:
        """Destructor - ensure cleanup."""
        try:
            self.shutdown()
        except Exception:
            pass  # Ignore errors during destruction
    
    def is_ready(self) -> bool:
        """
        Check if SDS is initialized and connected.
        
        Returns:
            True if connected and ready for operations
        """
        if not self._initialized:
            return False
        return lib.sds_is_ready()
    
    def poll(self, timeout_ms: int = 0) -> None:
        """
        Process MQTT messages and sync table changes.
        
        This should be called regularly in your main loop.
        
        Args:
            timeout_ms: Not currently used (reserved for future async support)
        """
        if not self._initialized:
            raise SdsError.from_code(ErrorCode.NOT_INITIALIZED)
        
        lib.sds_loop()
    
    def register_table(
        self,
        table_type: str,
        role: Role,
        *,
        sync_interval_ms: Optional[int] = None,
        schema: Optional[Type] = None,
        config_schema: Optional[Type] = None,
        state_schema: Optional[Type] = None,
        status_schema: Optional[Type] = None,
    ) -> SdsTable:
        """
        Register a table with the specified role.
        
        Returns an SdsTable object that provides C-like attribute access
        to the table's sections when schema classes are provided.
        
        Args:
            table_type: Name of the table type (must match schema)
            role: SDS_ROLE_OWNER or SDS_ROLE_DEVICE
            sync_interval_ms: Optional sync interval override
            schema: Schema bundle class with Config/State/Status attributes
                   (generated by sds_codegen.py)
            config_schema: Optional dataclass defining config fields
            state_schema: Optional dataclass defining state fields
            status_schema: Optional dataclass defining status fields
        
        Returns:
            SdsTable object for C-like attribute access
        
        Raises:
            SdsError: If registration fails
        
        Example using generated schema bundle:
            from sds_types import SensorData
            
            table = node.register_table("SensorData", Role.DEVICE, schema=SensorData)
            table.state.temperature = 23.5
        
        Example using individual schemas:
            @dataclass
            class SensorState:
                temperature: float = Field(float32=True)
            
            table = node.register_table(
                "SensorData", Role.DEVICE,
                state_schema=SensorState,
            )
            table.state.temperature = 23.5
        """
        # Extract schemas from bundle if provided
        if schema is not None:
            if hasattr(schema, 'Config') and config_schema is None:
                config_schema = schema.Config
            if hasattr(schema, 'State') and state_schema is None:
                state_schema = schema.State
            if hasattr(schema, 'Status') and status_schema is None:
                status_schema = schema.Status
        if not self._initialized:
            raise SdsError.from_code(ErrorCode.NOT_INITIALIZED)
        
        # Look up table metadata
        table_meta = lib.sds_find_table_meta(table_type.encode("utf-8"))
        if table_meta == ffi.NULL:
            raise SdsError(
                ErrorCode.TABLE_NOT_FOUND,
                f"Table type '{table_type}' not found in registry. "
                "Make sure the generated sds_types.h is compiled into the library."
            )
        
        # Determine table size based on role
        if role == Role.DEVICE:
            table_size = table_meta.device_table_size
        else:
            table_size = table_meta.owner_table_size
        
        # Allocate table structure
        table_buffer = ffi.new(f"char[{table_size}]")
        
        # Prepare options
        options = ffi.NULL
        if sync_interval_ms is not None:
            options = ffi.new("SdsTableOptions*")
            options.sync_interval_ms = sync_interval_ms
        
        # Register
        result = lib.sds_register_table(
            table_buffer,
            table_type.encode("utf-8"),
            role.value,
            options
        )
        check_error(result)
        
        # Create table wrapper
        sds_table = SdsTable(
            table_type=table_type,
            role=role,
            buffer=table_buffer,
            meta=table_meta,
            config_schema=config_schema,
            state_schema=state_schema,
            status_schema=status_schema,
        )
        
        # Store table info
        self._tables[table_type] = {
            "role": role,
            "buffer": table_buffer,
            "meta": table_meta,
            "table": sds_table,
        }
        
        return sds_table
    
    def unregister_table(self, table_type: str) -> None:
        """
        Unregister a table.
        
        Args:
            table_type: Name of the table type
        
        Raises:
            SdsError: If the table is not registered
        """
        if not self._initialized:
            raise SdsError.from_code(ErrorCode.NOT_INITIALIZED)
        
        result = lib.sds_unregister_table(table_type.encode("utf-8"))
        check_error(result)
        
        # Remove from our tracking
        self._tables.pop(table_type, None)
    
    def get_table(self, table_type: str) -> SdsTable:
        """
        Get a previously registered table.
        
        Args:
            table_type: Name of the table type
        
        Returns:
            SdsTable object
        
        Raises:
            SdsError: If the table is not registered
        """
        if table_type not in self._tables:
            raise SdsError(
                ErrorCode.TABLE_NOT_FOUND,
                f"Table '{table_type}' not registered"
            )
        
        return self._tables[table_type]["table"]
    
    def get_table_count(self) -> int:
        """
        Get the number of registered tables.
        
        Returns:
            Number of currently registered tables
        """
        return lib.sds_get_table_count()
    
    def get_stats(self) -> Dict[str, int]:
        """
        Get runtime statistics.
        
        Returns:
            Dictionary with keys: messages_sent, messages_received,
            reconnect_count, errors
        """
        stats = lib.sds_get_stats()
        return {
            "messages_sent": stats.messages_sent,
            "messages_received": stats.messages_received,
            "reconnect_count": stats.reconnect_count,
            "errors": stats.errors,
        }
    
    # ============== Callback Registration ==============
    
    def on_config(self, table_type: str) -> Callable[[ConfigCallback], ConfigCallback]:
        """
        Decorator to register a config update callback.
        
        Example:
            @node.on_config("SensorData")
            def handle_config(table_type: str):
                print(f"Config received for {table_type}")
        
        Args:
            table_type: Table type to receive callbacks for
            
        Returns:
            Decorator function
        """
        def decorator(func: ConfigCallback) -> ConfigCallback:
            self._config_callbacks[table_type] = func
            self._setup_config_callback(table_type)
            return func
        return decorator
    
    def on_state(self, table_type: str) -> Callable[[StateCallback], StateCallback]:
        """
        Decorator to register a state update callback (owner role only).
        
        Example:
            @node.on_state("SensorData")
            def handle_state(table_type: str, from_node: str):
                print(f"State from {from_node}")
        
        Args:
            table_type: Table type to receive callbacks for
            
        Returns:
            Decorator function
        """
        def decorator(func: StateCallback) -> StateCallback:
            self._state_callbacks[table_type] = func
            self._setup_state_callback(table_type)
            return func
        return decorator
    
    def on_status(self, table_type: str) -> Callable[[StatusCallback], StatusCallback]:
        """
        Decorator to register a status update callback (owner role only).
        
        Example:
            @node.on_status("SensorData")
            def handle_status(table_type: str, from_node: str):
                print(f"Status from {from_node}")
        
        Args:
            table_type: Table type to receive callbacks for
            
        Returns:
            Decorator function
        """
        def decorator(func: StatusCallback) -> StatusCallback:
            self._status_callbacks[table_type] = func
            self._setup_status_callback(table_type)
            return func
        return decorator
    
    def _setup_config_callback(self, table_type: str) -> None:
        """Set up the C-level config callback."""
        @ffi.callback("SdsConfigCallback")
        def c_callback(c_table_type, user_data):
            try:
                ttype = decode_string(c_table_type)
                if ttype and ttype in self._config_callbacks:
                    self._config_callbacks[ttype](ttype)
            except Exception as e:
                # Log but don't propagate - C code can't handle Python exceptions
                import sys
                print(f"Error in config callback for {table_type}: {e}", file=sys.stderr)
        
        # Keep callback alive
        self._c_callbacks[f"config_{table_type}"] = c_callback
        lib.sds_on_config_update(table_type.encode("utf-8"), c_callback, ffi.NULL)
    
    def _setup_state_callback(self, table_type: str) -> None:
        """Set up the C-level state callback."""
        @ffi.callback("SdsStateCallback")
        def c_callback(c_table_type, c_from_node, user_data):
            try:
                ttype = decode_string(c_table_type)
                from_node = decode_string(c_from_node)
                if ttype and from_node and ttype in self._state_callbacks:
                    self._state_callbacks[ttype](ttype, from_node)
            except Exception as e:
                # Log but don't propagate - C code can't handle Python exceptions
                import sys
                print(f"Error in state callback for {table_type}: {e}", file=sys.stderr)
        
        # Keep callback alive
        self._c_callbacks[f"state_{table_type}"] = c_callback
        lib.sds_on_state_update(table_type.encode("utf-8"), c_callback, ffi.NULL)
    
    def _setup_status_callback(self, table_type: str) -> None:
        """Set up the C-level status callback."""
        @ffi.callback("SdsStatusCallback")
        def c_callback(c_table_type, c_from_node, user_data):
            try:
                ttype = decode_string(c_table_type)
                from_node = decode_string(c_from_node)
                if ttype and from_node and ttype in self._status_callbacks:
                    self._status_callbacks[ttype](ttype, from_node)
            except Exception as e:
                # Log but don't propagate - C code can't handle Python exceptions
                import sys
                print(f"Error in status callback for {table_type}: {e}", file=sys.stderr)
        
        # Keep callback alive
        self._c_callbacks[f"status_{table_type}"] = c_callback
        lib.sds_on_status_update(table_type.encode("utf-8"), c_callback, ffi.NULL)
    
    def on_error(self, callback: ErrorCallback) -> ErrorCallback:
        """
        Register an error callback.
        
        The callback is called when SDS encounters an error.
        Can be used as a decorator or called directly.
        
        Example:
            @node.on_error
            def handle_error(error_code: int, context: str):
                print(f"Error {error_code}: {context}")
        
        Args:
            callback: Function that takes (error_code, context)
            
        Returns:
            The callback function (for decorator use)
        """
        self._error_callback = callback
        
        @ffi.callback("SdsErrorCallback")
        def c_callback(c_error_code, c_context):
            try:
                context = decode_string(c_context) or ""
                self._error_callback(int(c_error_code), context)
            except Exception as e:
                import sys
                print(f"Error in error callback: {e}", file=sys.stderr)
        
        # Keep callback alive
        self._c_callbacks["error"] = c_callback
        lib.sds_on_error(c_callback)
        
        return callback
    
    def on_version_mismatch(
        self,
        callback: VersionMismatchCallback,
    ) -> VersionMismatchCallback:
        """
        Register a version mismatch callback.
        
        The callback is called when a device reports a different schema version.
        Returning True from the callback accepts the message anyway.
        
        Example:
            @node.on_version_mismatch
            def handle_mismatch(table_type: str, device_id: str, 
                              local_ver: str, remote_ver: str) -> bool:
                print(f"Version mismatch: {local_ver} vs {remote_ver}")
                return False  # Reject the message
        
        Args:
            callback: Function that takes (table_type, device_id, 
                     local_version, remote_version) and returns bool
            
        Returns:
            The callback function (for decorator use)
        """
        self._version_mismatch_callback = callback
        
        @ffi.callback("SdsVersionMismatchCallback")
        def c_callback(c_table_type, c_device_id, c_local_ver, c_remote_ver):
            try:
                table_type = decode_string(c_table_type) or ""
                device_id = decode_string(c_device_id) or ""
                local_ver = decode_string(c_local_ver) or ""
                remote_ver = decode_string(c_remote_ver) or ""
                return self._version_mismatch_callback(
                    table_type, device_id, local_ver, remote_ver
                )
            except Exception as e:
                import sys
                print(f"Error in version mismatch callback: {e}", file=sys.stderr)
                return False
        
        # Keep callback alive
        self._c_callbacks["version_mismatch"] = c_callback
        lib.sds_on_version_mismatch(c_callback)
        
        return callback
    
    # ============== Owner Helper Methods ==============
    
    def is_device_online(
        self,
        table_type: str,
        device_node_id: str,
        timeout_ms: Optional[int] = None,
    ) -> bool:
        """
        Check if a device is currently online (owner role only).
        
        A device is considered online if:
        - We have a valid status slot for it
        - The "online" flag is true
        - Last message was within the timeout
        
        Args:
            table_type: Table type name
            device_node_id: Device node ID to check
            timeout_ms: Liveness timeout (default: 1.5x liveness interval)
        
        Returns:
            True if device is online
        
        Raises:
            SdsError: If table is not registered or not owner role
        """
        if table_type not in self._tables:
            raise SdsError(
                ErrorCode.TABLE_NOT_FOUND,
                f"Table '{table_type}' not registered"
            )
        
        table_info = self._tables[table_type]
        if table_info["role"] != Role.OWNER:
            raise SdsError(
                ErrorCode.INVALID_ROLE,
                "is_device_online() requires OWNER role"
            )
        
        if timeout_ms is None:
            # Default to 1.5x the liveness interval
            liveness = lib.sds_get_liveness_interval(table_type.encode("utf-8"))
            timeout_ms = int(liveness * 1.5)
        
        return lib.sds_is_device_online(
            table_info["buffer"],
            table_type.encode("utf-8"),
            device_node_id.encode("utf-8"),
            timeout_ms
        )
    
    def get_liveness_interval(self, table_type: str) -> int:
        """
        Get the liveness interval for a table type.
        
        Args:
            table_type: Table type name
        
        Returns:
            Liveness interval in milliseconds
        """
        return lib.sds_get_liveness_interval(table_type.encode("utf-8"))
    
    def get_schema_version(self) -> str:
        """
        Get the current schema version.
        
        Returns:
            Schema version string
        """
        c_str = lib.sds_get_schema_version()
        return decode_string(c_str) or "unknown"
