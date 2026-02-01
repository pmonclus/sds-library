"""
SdsNode - High-level Pythonic interface for SDS.

This module provides the main SdsNode class that wraps the C library
with a clean, Pythonic API including context managers and decorators.

Thread Safety:
    SdsNode is thread-safe. Multiple threads can safely call poll(),
    register_table(), and access table data. A reentrant lock (RLock)
    protects all critical sections.
"""
from __future__ import annotations

import logging
import threading
import time
import weakref
from typing import Any, Callable, Dict, Optional, Type, Union, TYPE_CHECKING

from sds.types import Role, ErrorCode, SdsError, SdsMqttError, SdsValidationError, check_error

# Maximum node ID length (matches C library SDS_MAX_NODE_ID_LEN - 1 for null terminator)
MAX_NODE_ID_LEN = 31

# Module logger
logger = logging.getLogger(__name__)

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
DeviceEvictedCallback = Callable[[str, str], None]  # (table_type, node_id)


class SdsNode:
    """
    High-level interface for an SDS node.
    
    Provides a Pythonic wrapper around the SDS C library with:
    - Context manager support (with statement)
    - Decorator-based callback registration
    - Automatic resource cleanup
    - Thread-safe operations
    
    Thread Safety:
        This class is thread-safe. Multiple threads can safely call poll(),
        register_table(), and access table data concurrently. A reentrant
        lock (RLock) protects all critical sections. Callbacks are executed
        while holding the lock, so avoid blocking operations in callbacks.
    
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
        connect_timeout_ms: int = 5000,
        retry_count: int = 3,
        retry_delay_ms: int = 1000,
        eviction_grace_ms: int = 0,
    ):
        """
        Create an SDS node.
        
        Args:
            node_id: Unique identifier for this node (max 31 characters)
            broker_host: MQTT broker hostname or IP address
            port: MQTT broker port (default: 1883)
            username: MQTT username for authentication (optional)
            password: MQTT password for authentication (optional)
            auto_init: If True, automatically call init() (default: True)
            connect_timeout_ms: Connection timeout in milliseconds (default: 5000)
            retry_count: Number of connection retries (default: 3)
            retry_delay_ms: Initial retry delay in milliseconds (default: 1000)
                           Uses exponential backoff (delay doubles each retry)
            eviction_grace_ms: Grace period before evicting offline devices (default: 0 = disabled)
                              When > 0, devices that go offline (LWT) will be evicted from
                              status slots after this many milliseconds if they don't reconnect.
        
        Raises:
            SdsValidationError: If node_id is invalid
            SdsError: If auto_init is True and initialization fails
        """
        # Validate node_id
        if not node_id:
            raise SdsValidationError("node_id cannot be empty")
        if len(node_id) > MAX_NODE_ID_LEN:
            raise SdsValidationError(
                f"node_id '{node_id}' exceeds maximum length of {MAX_NODE_ID_LEN} characters"
            )
        # Check for invalid characters (only allow alphanumeric, underscore, hyphen)
        import re
        if not re.match(r'^[a-zA-Z0-9_-]+$', node_id):
            raise SdsValidationError(
                f"node_id '{node_id}' contains invalid characters. "
                "Only alphanumeric, underscore, and hyphen are allowed."
            )
        
        self._node_id = node_id
        self._broker_host = broker_host
        self._port = port
        self._username = username
        self._password = password
        self._connect_timeout_ms = connect_timeout_ms
        self._retry_count = retry_count
        self._retry_delay_ms = retry_delay_ms
        self._eviction_grace_ms = eviction_grace_ms
        
        # Thread safety lock - reentrant to allow nested calls
        self._lock = threading.RLock()
        
        self._initialized = False
        self._tables: Dict[str, Dict[str, Any]] = {}
        
        # Callback storage (keyed by table_type)
        self._config_callbacks: Dict[str, ConfigCallback] = {}
        self._state_callbacks: Dict[str, StateCallback] = {}
        self._status_callbacks: Dict[str, StatusCallback] = {}
        self._error_callback: Optional[ErrorCallback] = None
        self._version_mismatch_callback: Optional[VersionMismatchCallback] = None
        self._eviction_callback: Optional[DeviceEvictedCallback] = None
        
        # Keep C callback handles alive
        self._c_callbacks: Dict[str, Any] = {}
        
        # Register this instance
        SdsNode._instances.add(self)
        
        # Set up a finalizer for reliable cleanup even if __del__ is not called
        self._finalizer = weakref.finalize(self, self._cleanup_class, self._node_id)
        
        if auto_init:
            self.init()
    
    @staticmethod
    def _cleanup_class(node_id: str) -> None:
        """
        Static cleanup method for finalizer.
        
        Note: This is called by the finalizer when the SdsNode is garbage collected.
        It cannot access instance state, only static/class state.
        """
        # The instance has already been cleaned up if shutdown() was called,
        # but log for debugging if something unexpected happens
        logger.debug(f"Finalizer called for node {node_id}")
    
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
        Connection is retried with exponential backoff based on retry_count
        and retry_delay_ms parameters.
        
        Raises:
            SdsError: If initialization fails after all retries
            SdsAlreadyInitializedError: If already initialized
        """
        with self._lock:
            if self._initialized:
                raise SdsError.from_code(ErrorCode.ALREADY_INITIALIZED)
            
            # Create config struct
            config = ffi.new("SdsConfig*")
            
            # Allocate string buffers and keep them alive as instance attributes.
            # CFFI buffers must be stored separately because assigning to struct
            # fields only copies the pointer, not the Python reference.
            self._config_node_id = ffi.new("char[]", self._node_id.encode("utf-8"))
            self._config_broker = ffi.new("char[]", self._broker_host.encode("utf-8"))
            config.node_id = self._config_node_id
            config.mqtt_broker = self._config_broker
            config.mqtt_port = self._port
            
            if self._username is not None:
                self._config_username = ffi.new("char[]", self._username.encode("utf-8"))
                config.mqtt_username = self._config_username
            else:
                self._config_username = None
                config.mqtt_username = ffi.NULL
            
            if self._password is not None:
                self._config_password = ffi.new("char[]", self._password.encode("utf-8"))
                config.mqtt_password = self._config_password
            else:
                self._config_password = None
                config.mqtt_password = ffi.NULL
            
            # Set eviction grace period
            config.eviction_grace_ms = self._eviction_grace_ms
            
            # Keep config struct alive
            self._config = config
            
            # Set as current instance for callbacks
            SdsNode._current_instance = self
            
            # Initialize with retry logic
            last_error: Optional[SdsError] = None
            delay_ms = self._retry_delay_ms
            
            for attempt in range(self._retry_count + 1):
                try:
                    result = lib.sds_init(config)
                    check_error(result)
                    self._initialized = True
                    if attempt > 0:
                        logger.info(f"Connected to MQTT broker after {attempt + 1} attempts")
                    return
                except SdsMqttError as e:
                    last_error = e
                    if attempt < self._retry_count:
                        logger.warning(
                            f"MQTT connection failed (attempt {attempt + 1}/{self._retry_count + 1}), "
                            f"retrying in {delay_ms}ms: {e}"
                        )
                        time.sleep(delay_ms / 1000.0)
                        delay_ms *= 2  # Exponential backoff
                    else:
                        logger.error(f"MQTT connection failed after {attempt + 1} attempts: {e}")
                except SdsError:
                    # Non-MQTT errors are not retried
                    raise
            
            # All retries exhausted
            if last_error:
                raise last_error
    
    def shutdown(self) -> None:
        """
        Shutdown SDS and disconnect from the MQTT broker.
        
        This is called automatically when exiting a context manager.
        Safe to call multiple times. Thread-safe.
        """
        with self._lock:
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
            # Log errors during destruction instead of silently ignoring
            logger.warning("Error during SdsNode cleanup", exc_info=True)
    
    def is_ready(self) -> bool:
        """
        Check if SDS is initialized and connected.
        
        Thread-safe.
        
        Returns:
            True if connected and ready for operations
        """
        with self._lock:
            if not self._initialized:
                return False
            return lib.sds_is_ready()
    
    def poll(self, timeout_ms: int = 0) -> None:
        """
        Process MQTT messages and sync table changes.
        
        This should be called regularly in your main loop. Thread-safe.
        Callbacks are executed while holding the lock.
        
        Args:
            timeout_ms: Not currently used (reserved for future async support)
        """
        with self._lock:
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
        with self._lock:
            return self._register_table_impl(
                table_type=table_type,
                role=role,
                sync_interval_ms=sync_interval_ms,
                schema=schema,
                config_schema=config_schema,
                state_schema=state_schema,
                status_schema=status_schema,
            )
    
    def _register_table_impl(
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
        """Internal implementation of register_table (called with lock held)."""
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
        
        # Look up table metadata from C registry
        table_meta = lib.sds_find_table_meta(table_type.encode("utf-8"))
        
        # If not in C registry, try to use Python schemas directly
        if table_meta == ffi.NULL:
            if config_schema is None and state_schema is None and status_schema is None:
                raise SdsError(
                    ErrorCode.TABLE_NOT_FOUND,
                    f"Table type '{table_type}' not found in registry. "
                    "Provide schema parameter or ensure sds_types.h is compiled into the library."
                )
            
            # Use Python schemas with sds_register_table_ex
            return self._register_table_with_python_schema(
                table_type=table_type,
                role=role,
                config_schema=config_schema,
                state_schema=state_schema,
                status_schema=status_schema,
                sync_interval_ms=sync_interval_ms,
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
            lock=self._lock,
        )
        
        # Store table info
        self._tables[table_type] = {
            "role": role,
            "buffer": table_buffer,
            "meta": table_meta,
            "table": sds_table,
        }
        
        return sds_table
    
    def _register_table_with_python_schema(
        self,
        table_type: str,
        role: Role,
        config_schema: Optional[Type] = None,
        state_schema: Optional[Type] = None,
        status_schema: Optional[Type] = None,
        sync_interval_ms: Optional[int] = None,
    ) -> "SdsTable":
        """
        Register a table using Python-only schemas (no C registry).
        
        Uses sds_register_table_ex with CFFI callbacks for serialization.
        """
        from sds.tables import analyze_dataclass, TableSectionInfo
        
        # Analyze schemas to get sizes and offsets
        config_info = analyze_dataclass(config_schema) if config_schema else None
        state_info = analyze_dataclass(state_schema) if state_schema else None
        status_info = analyze_dataclass(status_schema) if status_schema else None
        
        config_size = config_info.total_size if config_info else 0
        state_size = state_info.total_size if state_info else 0
        status_size = status_info.total_size if status_info else 0
        
        # For device: config + state + status
        # For owner: config + state + status + status_count + status_slots
        # Calculate offsets (sections are placed sequentially)
        config_offset = 0
        state_offset = config_size
        status_offset = config_size + state_size
        table_size = config_size + state_size + status_size
        
        # Status slot layout (matches C struct layout):
        # - node_id: 32 bytes (SDS_MAX_NODE_ID_LEN)
        # - valid: 1 byte
        # - online: 1 byte
        # - eviction_pending: 1 byte
        # - padding: 1 byte (for uint32 alignment)
        # - last_seen_ms: 4 bytes
        # - eviction_deadline: 4 bytes
        # - status: status_size bytes
        SDS_MAX_NODE_ID_LEN = 32
        slot_node_id_offset = 0
        slot_valid_offset = SDS_MAX_NODE_ID_LEN  # 32
        slot_online_offset = SDS_MAX_NODE_ID_LEN + 1  # 33
        slot_eviction_pending_offset = SDS_MAX_NODE_ID_LEN + 2  # 34
        # padding at 35 for uint32 alignment
        slot_last_seen_offset = SDS_MAX_NODE_ID_LEN + 4  # 36 (aligned)
        slot_eviction_deadline_offset = SDS_MAX_NODE_ID_LEN + 8  # 40
        slot_status_offset = SDS_MAX_NODE_ID_LEN + 12  # 44
        slot_size = SDS_MAX_NODE_ID_LEN + 12 + status_size  # 44 + status_size
        max_slots = 8
        
        # For owner, add space for status_slots and count
        status_slots_offset = 0
        status_count_offset = 0
        if role == Role.OWNER:
            status_count_offset = table_size
            table_size += 1  # status_count (uint8)
            # Align to 4 bytes for slots array
            if table_size % 4 != 0:
                table_size += 4 - (table_size % 4)
            status_slots_offset = table_size
            table_size += slot_size * max_slots
        
        # Allocate table buffer
        table_buffer = ffi.new(f"char[{table_size}]")
        
        # Create serialization/deserialization callbacks
        # We use closures to capture the schema info
        serializers = self._create_serializers(config_info, state_info, status_info, table_buffer)
        
        # Prepare options
        options = ffi.NULL
        if sync_interval_ms is not None:
            options = ffi.new("SdsTableOptions*")
            options.sync_interval_ms = sync_interval_ms
        
        # Register using extended API
        result = lib.sds_register_table_ex(
            table_buffer,
            table_type.encode("utf-8"),
            role.value,
            options,
            config_offset, config_size,
            state_offset, state_size,
            status_offset, status_size,
            serializers["serialize_config"],
            serializers["deserialize_config"],
            serializers["serialize_state"],
            serializers["deserialize_state"],
            serializers["serialize_status"],
            serializers["deserialize_status"],
        )
        check_error(result)
        
        # For owner, configure status slot tracking
        if role == Role.OWNER:
            lib.sds_set_owner_status_slots(
                table_type.encode("utf-8"),
                status_slots_offset,
                slot_size,
                slot_status_offset,
                status_count_offset,
                max_slots,
            )
            lib.sds_set_owner_slot_offsets(
                table_type.encode("utf-8"),
                slot_valid_offset,
                slot_online_offset,
                slot_last_seen_offset,
            )
            # Configure eviction offsets for device eviction support
            lib.sds_set_owner_eviction_offsets(
                table_type.encode("utf-8"),
                slot_eviction_pending_offset,
                slot_eviction_deadline_offset,
            )
        
        # Create a fake table_meta for the SdsTable wrapper
        # We'll store the info we need directly
        fake_meta = {
            "config_offset": config_offset,
            "config_size": config_size,
            "state_offset": state_offset,
            "state_size": state_size,
            "status_offset": status_offset,
            "status_size": status_size,
            "status_slots_offset": status_slots_offset,
            "status_count_offset": status_count_offset,
            "slot_size": slot_size,
            "slot_valid_offset": slot_valid_offset,
            "slot_online_offset": slot_online_offset,
            "slot_eviction_pending_offset": slot_eviction_pending_offset,
            "slot_last_seen_offset": slot_last_seen_offset,
            "slot_eviction_deadline_offset": slot_eviction_deadline_offset,
            "slot_status_offset": slot_status_offset,
            "max_slots": max_slots,
        }
        
        # Create table wrapper
        sds_table = SdsTable(
            table_type=table_type,
            role=role,
            buffer=table_buffer,
            meta=None,  # No C meta
            config_schema=config_schema,
            state_schema=state_schema,
            status_schema=status_schema,
            python_meta=fake_meta,  # Use Python-calculated offsets
            lock=self._lock,
        )
        
        # Store table info
        self._tables[table_type] = {
            "role": role,
            "buffer": table_buffer,
            "meta": None,
            "table": sds_table,
            "serializers": serializers,  # Keep alive
        }
        
        return sds_table
    
    def _create_serializers(
        self,
        config_info: Optional["TableSectionInfo"],
        state_info: Optional["TableSectionInfo"],
        status_info: Optional["TableSectionInfo"],
        table_buffer: Any,
    ) -> Dict[str, Any]:
        """Create CFFI callbacks for serialization."""
        
        def make_serialize(section_info: Optional["TableSectionInfo"]):
            if section_info is None:
                return ffi.NULL
            
            @ffi.callback("SdsSerializeFunc")
            def serialize(section_ptr, writer_ptr):
                try:
                    # Read values from C buffer and write to JSON
                    for field in section_info.fields:
                        offset = field.offset
                        ptr = ffi.cast("char*", section_ptr) + offset
                        
                        if field.field_type.value == "float32":
                            val = ffi.cast("float*", ptr)[0]
                            lib.sds_json_add_float(writer_ptr, field.name.encode(), val)
                        elif field.field_type.value == "uint8":
                            val = ffi.cast("uint8_t*", ptr)[0]
                            lib.sds_json_add_uint(writer_ptr, field.name.encode(), int(val))
                        elif field.field_type.value == "int32":
                            val = ffi.cast("int32_t*", ptr)[0]
                            lib.sds_json_add_int(writer_ptr, field.name.encode(), val)
                        elif field.field_type.value == "uint32":
                            val = ffi.cast("uint32_t*", ptr)[0]
                            lib.sds_json_add_uint(writer_ptr, field.name.encode(), val)
                        elif field.field_type.value == "bool":
                            val = ffi.cast("bool*", ptr)[0]
                            lib.sds_json_add_bool(writer_ptr, field.name.encode(), val)
                        elif field.field_type.value == "string":
                            val = ffi.string(ffi.cast("char*", ptr), field.string_len)
                            lib.sds_json_add_string(writer_ptr, field.name.encode(), val)
                except Exception as e:
                    logger.exception(f"Serialize error for {section_info.name if section_info else 'unknown'}")
            
            return serialize
        
        def make_deserialize(section_info: Optional["TableSectionInfo"]):
            if section_info is None:
                return ffi.NULL
            
            @ffi.callback("SdsDeserializeFunc")
            def deserialize(section_ptr, reader_ptr):
                try:
                    for field in section_info.fields:
                        offset = field.offset
                        ptr = ffi.cast("char*", section_ptr) + offset
                        
                        if field.field_type.value == "float32":
                            val = ffi.new("float*")
                            if lib.sds_json_get_float_field(reader_ptr, field.name.encode(), val):
                                ffi.cast("float*", ptr)[0] = val[0]
                        elif field.field_type.value == "uint8":
                            val = ffi.new("uint8_t*")
                            if lib.sds_json_get_uint8_field(reader_ptr, field.name.encode(), val):
                                ffi.cast("uint8_t*", ptr)[0] = val[0]
                        elif field.field_type.value == "int32":
                            val = ffi.new("int32_t*")
                            if lib.sds_json_get_int_field(reader_ptr, field.name.encode(), val):
                                ffi.cast("int32_t*", ptr)[0] = val[0]
                        elif field.field_type.value == "uint32":
                            val = ffi.new("uint32_t*")
                            if lib.sds_json_get_uint_field(reader_ptr, field.name.encode(), val):
                                ffi.cast("uint32_t*", ptr)[0] = val[0]
                        elif field.field_type.value == "bool":
                            val = ffi.new("bool*")
                            if lib.sds_json_get_bool_field(reader_ptr, field.name.encode(), val):
                                ffi.cast("bool*", ptr)[0] = val[0]
                        elif field.field_type.value == "string":
                            buf = ffi.new(f"char[{field.string_len}]")
                            if lib.sds_json_get_string_field(reader_ptr, field.name.encode(), buf, field.string_len):
                                ffi.memmove(ptr, buf, field.string_len)
                except Exception as e:
                    logger.exception(f"Deserialize error for {section_info.name if section_info else 'unknown'}")
            
            return deserialize
        
        # Create and store callbacks (must keep references alive!)
        callbacks = {
            "serialize_config": make_serialize(config_info),
            "deserialize_config": make_deserialize(config_info),
            "serialize_state": make_serialize(state_info),
            "deserialize_state": make_deserialize(state_info),
            "serialize_status": make_serialize(status_info),
            "deserialize_status": make_deserialize(status_info),
        }
        
        return callbacks
    
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
    
    def on_device_evicted(self) -> Callable[[DeviceEvictedCallback], DeviceEvictedCallback]:
        """
        Decorator to register a device eviction callback (owner role only).
        
        Called when a device is evicted from status slots after the eviction
        grace period expires following an LWT message.
        
        Example:
            @node.on_device_evicted()
            def handle_eviction(table_type: str, node_id: str):
                print(f"Device {node_id} was evicted from {table_type}")
        
        Note:
            Eviction is only triggered if eviction_grace_ms > 0 was set
            when creating the SdsNode.
            
        Returns:
            Decorator function
        """
        def decorator(func: DeviceEvictedCallback) -> DeviceEvictedCallback:
            self._eviction_callback = func
            self._setup_eviction_callback()
            return func
        return decorator
    
    def _setup_eviction_callback(self) -> None:
        """Set up the C-level eviction callback."""
        @ffi.callback("SdsDeviceEvictedCallback")
        def c_callback(c_table_type, c_node_id, user_data):
            try:
                ttype = decode_string(c_table_type)
                node_id = decode_string(c_node_id)
                if ttype and node_id and self._eviction_callback:
                    self._eviction_callback(ttype, node_id)
            except Exception:
                # Log but don't propagate - C code can't handle Python exceptions
                logger.exception("Error in eviction callback")
        
        # Keep callback alive
        self._c_callbacks["eviction"] = c_callback
        # Pass NULL for table_type since eviction callback is global
        lib.sds_on_device_evicted(ffi.NULL, c_callback, ffi.NULL)
    
    def _setup_config_callback(self, table_type: str) -> None:
        """Set up the C-level config callback."""
        @ffi.callback("SdsConfigCallback")
        def c_callback(c_table_type, user_data):
            try:
                ttype = decode_string(c_table_type)
                if ttype and ttype in self._config_callbacks:
                    self._config_callbacks[ttype](ttype)
            except Exception:
                # Log but don't propagate - C code can't handle Python exceptions
                logger.exception(f"Error in config callback for {table_type}")
        
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
            except Exception:
                # Log but don't propagate - C code can't handle Python exceptions
                logger.exception(f"Error in state callback for {table_type}")
        
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
            except Exception:
                # Log but don't propagate - C code can't handle Python exceptions
                logger.exception(f"Error in status callback for {table_type}")
        
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
            except Exception:
                logger.exception("Error in error callback")
        
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
            except Exception:
                logger.exception("Error in version mismatch callback")
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
    
    def get_eviction_grace(self) -> int:
        """
        Get the configured eviction grace period.
        
        Returns:
            Eviction grace period in milliseconds (0 = disabled)
        """
        # Pass NULL since eviction grace is now global
        return lib.sds_get_eviction_grace(ffi.NULL)
    
    @property
    def eviction_grace_ms(self) -> int:
        """The configured eviction grace period in milliseconds."""
        return self._eviction_grace_ms
    
    def get_schema_version(self) -> str:
        """
        Get the current schema version.
        
        Returns:
            Schema version string
        """
        c_str = lib.sds_get_schema_version()
        return decode_string(c_str) or "unknown"
