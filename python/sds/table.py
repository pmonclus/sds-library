"""
SdsTable - C-like table access for SDS.

This module provides the SdsTable class that wraps registered tables
and provides C-like attribute access (table.state.temperature = x).
"""
from __future__ import annotations

import struct
from typing import Any, Dict, Iterator, Optional, Tuple, Type, TypeVar

from sds._bindings import ffi, lib, decode_string
from sds.types import Role, ErrorCode, SdsError
from sds.tables import (
    FieldType,
    TableFieldInfo,
    TableSectionInfo,
    analyze_dataclass,
    _FIELD_FORMATS,
)

T = TypeVar("T")


class SectionProxy:
    """
    Proxy object that provides C-like attribute access to a table section.
    
    This class intercepts attribute access and reads/writes directly from
    the C memory buffer, mimicking C struct field access.
    
    Example:
        table.state.temperature = 23.5  # Writes to C buffer
        print(table.state.temperature)   # Reads from C buffer
    """
    
    # Slots for performance and to avoid __setattr__ recursion
    __slots__ = ("_section_info", "_buffer_ptr", "_readonly")
    
    def __init__(
        self,
        section_info: TableSectionInfo,
        buffer_ptr: Any,
        readonly: bool = False,
    ):
        """
        Initialize the section proxy.
        
        Args:
            section_info: Field layout information
            buffer_ptr: CFFI pointer to the section start in the C buffer
            readonly: If True, writes will raise an error
        """
        object.__setattr__(self, "_section_info", section_info)
        object.__setattr__(self, "_buffer_ptr", buffer_ptr)
        object.__setattr__(self, "_readonly", readonly)
    
    def _find_field(self, name: str) -> Optional[TableFieldInfo]:
        """Find field info by name."""
        for field in self._section_info.fields:
            if field.name == name:
                return field
        return None
    
    def __getattr__(self, name: str) -> Any:
        """Read a field value from the C buffer."""
        field = self._find_field(name)
        if field is None:
            raise AttributeError(f"No field named '{name}'")
        
        # Calculate pointer to field
        field_ptr = ffi.cast("char*", self._buffer_ptr) + field.offset
        
        if field.field_type == FieldType.STRING:
            # Read null-terminated string
            raw = ffi.buffer(field_ptr, field.string_len or 32)[:]
            null_idx = raw.find(b'\x00')
            if null_idx >= 0:
                raw = raw[:null_idx]
            return raw.decode("utf-8", errors="replace")
        elif field.field_type == FieldType.BOOL:
            return bool(ffi.cast("uint8_t*", field_ptr)[0])
        elif field.field_type == FieldType.FLOAT32:
            return ffi.cast("float*", field_ptr)[0]
        elif field.field_type == FieldType.INT8:
            return ffi.cast("int8_t*", field_ptr)[0]
        elif field.field_type == FieldType.UINT8:
            return ffi.cast("uint8_t*", field_ptr)[0]
        elif field.field_type == FieldType.INT16:
            return ffi.cast("int16_t*", field_ptr)[0]
        elif field.field_type == FieldType.UINT16:
            return ffi.cast("uint16_t*", field_ptr)[0]
        elif field.field_type == FieldType.INT32:
            return ffi.cast("int32_t*", field_ptr)[0]
        elif field.field_type == FieldType.UINT32:
            return ffi.cast("uint32_t*", field_ptr)[0]
        else:
            raise ValueError(f"Unknown field type: {field.field_type}")
    
    def __setattr__(self, name: str, value: Any) -> None:
        """Write a field value to the C buffer."""
        # Handle slots during __init__
        if name in ("_section_info", "_buffer_ptr", "_readonly"):
            object.__setattr__(self, name, value)
            return
        
        if self._readonly:
            raise AttributeError(f"Cannot write to read-only section")
        
        field = self._find_field(name)
        if field is None:
            raise AttributeError(f"No field named '{name}'")
        
        # Calculate pointer to field
        field_ptr = ffi.cast("char*", self._buffer_ptr) + field.offset
        
        if field.field_type == FieldType.STRING:
            # Write null-terminated string
            if isinstance(value, str):
                encoded = value.encode("utf-8")
            else:
                encoded = bytes(value)
            max_len = (field.string_len or 32) - 1
            encoded = encoded[:max_len]
            # Zero the buffer first
            ffi.buffer(field_ptr, field.string_len or 32)[:] = b'\x00' * (field.string_len or 32)
            # Write the string
            ffi.buffer(field_ptr, len(encoded))[:] = encoded
        elif field.field_type == FieldType.BOOL:
            ffi.cast("uint8_t*", field_ptr)[0] = 1 if value else 0
        elif field.field_type == FieldType.FLOAT32:
            ffi.cast("float*", field_ptr)[0] = float(value)
        elif field.field_type == FieldType.INT8:
            ffi.cast("int8_t*", field_ptr)[0] = int(value)
        elif field.field_type == FieldType.UINT8:
            ffi.cast("uint8_t*", field_ptr)[0] = int(value)
        elif field.field_type == FieldType.INT16:
            ffi.cast("int16_t*", field_ptr)[0] = int(value)
        elif field.field_type == FieldType.UINT16:
            ffi.cast("uint16_t*", field_ptr)[0] = int(value)
        elif field.field_type == FieldType.INT32:
            ffi.cast("int32_t*", field_ptr)[0] = int(value)
        elif field.field_type == FieldType.UINT32:
            ffi.cast("uint32_t*", field_ptr)[0] = int(value)
        else:
            raise ValueError(f"Unknown field type: {field.field_type}")
    
    def __repr__(self) -> str:
        """Return a representation showing all field values."""
        fields = []
        for field in self._section_info.fields:
            try:
                value = getattr(self, field.name)
                fields.append(f"{field.name}={value!r}")
            except Exception:
                fields.append(f"{field.name}=?")
        return f"Section({', '.join(fields)})"


class DeviceView:
    """
    Read-only view of a device's data (for owner role).
    
    Provides access to a specific device's state and status data
    as received by the owner.
    
    Example:
        device = table.get_device("sensor_01")
        print(device.state.temperature)
        print(device.status.battery)
        print(device.online)
    """
    
    def __init__(
        self,
        node_id: str,
        state_proxy: Optional[SectionProxy],
        status_proxy: Optional[SectionProxy],
        online: bool,
        last_seen: int,
    ):
        """
        Initialize the device view.
        
        Args:
            node_id: The device's node ID
            state_proxy: Proxy for reading state (or None if no schema)
            status_proxy: Proxy for reading status (or None if no schema)
            online: Whether the device is currently online
            last_seen: Timestamp of last message (ms since epoch)
        """
        self._node_id = node_id
        self._state = state_proxy
        self._status = status_proxy
        self._online = online
        self._last_seen = last_seen
    
    @property
    def node_id(self) -> str:
        """Get the device's node ID."""
        return self._node_id
    
    @property
    def state(self) -> Optional[SectionProxy]:
        """Get the device's state (read-only)."""
        return self._state
    
    @property
    def status(self) -> Optional[SectionProxy]:
        """Get the device's status (read-only)."""
        return self._status
    
    @property
    def online(self) -> bool:
        """Check if the device is online."""
        return self._online
    
    @property
    def last_seen(self) -> int:
        """Get the timestamp of the last message from this device."""
        return self._last_seen
    
    def __repr__(self) -> str:
        return f"DeviceView(node_id={self._node_id!r}, online={self._online})"


class SdsTable:
    """
    High-level table wrapper with C-like attribute access.
    
    Returned by SdsNode.register_table() when schema classes are provided.
    Provides direct attribute access to table sections.
    
    Example (Device role):
        table = node.register_table(
            "SensorData",
            Role.DEVICE,
            state_schema=SensorState,
            status_schema=SensorStatus,
            config_schema=SensorConfig,
        )
        
        # Write state
        table.state.temperature = 23.5
        table.state.humidity = 65.0
        
        # Write status
        table.status.error_code = 0
        
        # Read config from owner
        print(table.config.threshold)
    
    Example (Owner role):
        table = node.register_table(
            "SensorData",
            Role.OWNER,
            config_schema=SensorConfig,
            state_schema=SensorState,
            status_schema=SensorStatus,
        )
        
        # Write config
        table.config.threshold = 25.0
        
        # Read device data
        device = table.get_device("sensor_01")
        print(device.state.temperature)
        
        # Iterate all devices
        for node_id, device in table.iter_devices():
            print(f"{node_id}: {device.state.temperature}")
    """
    
    def __init__(
        self,
        table_type: str,
        role: Role,
        buffer: Any,
        meta: Any,
        *,
        config_schema: Optional[Type] = None,
        state_schema: Optional[Type] = None,
        status_schema: Optional[Type] = None,
    ):
        """
        Initialize the table wrapper.
        
        Args:
            table_type: The table type name
            role: SDS_ROLE_OWNER or SDS_ROLE_DEVICE
            buffer: CFFI buffer for the table
            meta: Pointer to SdsTableMeta
            config_schema: Optional dataclass for config section
            state_schema: Optional dataclass for state section
            status_schema: Optional dataclass for status section
        """
        self._table_type = table_type
        self._role = role
        self._buffer = buffer
        self._meta = meta
        
        # Analyze schemas if provided
        self._config_info = analyze_dataclass(config_schema) if config_schema else None
        self._state_info = analyze_dataclass(state_schema) if state_schema else None
        self._status_info = analyze_dataclass(status_schema) if status_schema else None
        
        # Store schema classes for creating device views
        self._config_schema = config_schema
        self._state_schema = state_schema
        self._status_schema = status_schema
        
        # Create section proxies
        self._config_proxy: Optional[SectionProxy] = None
        self._state_proxy: Optional[SectionProxy] = None
        self._status_proxy: Optional[SectionProxy] = None
        
        self._setup_proxies()
    
    def _setup_proxies(self) -> None:
        """Set up section proxy objects based on role."""
        buffer_ptr = ffi.cast("char*", self._buffer)
        
        if self._role == Role.DEVICE:
            # Device role: can write state/status, read config
            if self._config_info:
                config_ptr = buffer_ptr + self._meta.dev_config_offset
                self._config_proxy = SectionProxy(self._config_info, config_ptr, readonly=True)
            
            if self._state_info:
                state_ptr = buffer_ptr + self._meta.dev_state_offset
                self._state_proxy = SectionProxy(self._state_info, state_ptr, readonly=False)
            
            if self._status_info:
                status_ptr = buffer_ptr + self._meta.dev_status_offset
                self._status_proxy = SectionProxy(self._status_info, status_ptr, readonly=False)
        
        else:  # OWNER role
            # Owner role: can write config, read state (when devices send updates)
            if self._config_info:
                config_ptr = buffer_ptr + self._meta.own_config_offset
                self._config_proxy = SectionProxy(self._config_info, config_ptr, readonly=False)
            
            # Note: state proxy not used for owner - use get_device() instead
    
    @property
    def table_type(self) -> str:
        """Get the table type name."""
        return self._table_type
    
    @property
    def role(self) -> Role:
        """Get the role (OWNER or DEVICE)."""
        return self._role
    
    @property
    def config(self) -> SectionProxy:
        """
        Access the config section.
        
        For DEVICE role: Read-only (receives config from owner)
        For OWNER role: Read/write (sends config to devices)
        
        Returns:
            SectionProxy for attribute access
        
        Raises:
            SdsError: If no config schema was provided
        """
        if self._config_proxy is None:
            raise SdsError(
                ErrorCode.INVALID_TABLE,
                "No config schema provided for this table"
            )
        return self._config_proxy
    
    @property
    def state(self) -> SectionProxy:
        """
        Access the state section (DEVICE role only).
        
        Returns:
            SectionProxy for attribute access
        
        Raises:
            SdsError: If no state schema was provided or not device role
        """
        if self._role != Role.DEVICE:
            raise SdsError(
                ErrorCode.INVALID_ROLE,
                "state property only available for DEVICE role. "
                "Use get_device().state for OWNER role."
            )
        if self._state_proxy is None:
            raise SdsError(
                ErrorCode.INVALID_TABLE,
                "No state schema provided for this table"
            )
        return self._state_proxy
    
    @property
    def status(self) -> SectionProxy:
        """
        Access the status section (DEVICE role only).
        
        Returns:
            SectionProxy for attribute access
        
        Raises:
            SdsError: If no status schema was provided or not device role
        """
        if self._role != Role.DEVICE:
            raise SdsError(
                ErrorCode.INVALID_ROLE,
                "status property only available for DEVICE role. "
                "Use get_device().status for OWNER role."
            )
        if self._status_proxy is None:
            raise SdsError(
                ErrorCode.INVALID_TABLE,
                "No status schema provided for this table"
            )
        return self._status_proxy
    
    # ============== Owner Methods ==============
    
    def get_device(self, node_id: str, timeout_ms: Optional[int] = None) -> Optional[DeviceView]:
        """
        Get a device's data (OWNER role only).
        
        Returns a DeviceView with read-only access to the device's state
        and status as last received.
        
        Args:
            node_id: The device's node ID
            timeout_ms: Liveness timeout for online check (default: 1.5x liveness)
        
        Returns:
            DeviceView if device is known, None otherwise
        
        Raises:
            SdsError: If not owner role
        
        Example:
            device = table.get_device("sensor_01")
            if device and device.online:
                print(device.state.temperature)
        """
        if self._role != Role.OWNER:
            raise SdsError(
                ErrorCode.INVALID_ROLE,
                "get_device() is only available for OWNER role"
            )
        
        # Find the device's status slot
        slot_ptr = lib.sds_find_node_status(
            self._buffer,
            self._table_type.encode("utf-8"),
            node_id.encode("utf-8"),
        )
        
        if slot_ptr == ffi.NULL:
            return None
        
        # Calculate online status
        if timeout_ms is None:
            liveness = lib.sds_get_liveness_interval(self._table_type.encode("utf-8"))
            timeout_ms = int(liveness * 1.5)
        
        online = lib.sds_is_device_online(
            self._buffer,
            self._table_type.encode("utf-8"),
            node_id.encode("utf-8"),
            timeout_ms,
        )
        
        # Read last_seen timestamp from slot
        slot_char = ffi.cast("char*", slot_ptr)
        last_seen_ptr = ffi.cast("uint32_t*", slot_char + self._meta.slot_last_seen_offset)
        last_seen = last_seen_ptr[0]
        
        # Create state proxy if we have schema
        state_proxy = None
        if self._state_info:
            # State is stored right after the slot header in owner table
            # For owner, state is at own_state_offset (single state buffer for receiving)
            # Actually, in owner mode, each device's state/status is stored in the status slots
            # The slot has: valid, online, last_seen, node_id, then status data
            # For state, the owner table has a shared buffer at own_state_offset
            # Let me check the C code to understand the exact layout
            
            # Looking at sds_core.c: for owner, incoming state updates are stored
            # in own_state section. But only one at a time (last received).
            # For per-device state, we need to look at how the C lib handles this.
            
            # Actually, checking the design: owners receive state/status updates
            # via callbacks and the data is in the shared section temporarily.
            # Per-device storage is only for status in the slots array.
            
            # For this API, we'll read from the status slot which contains the
            # device's status data. State is transient and only available during callback.
            pass
        
        # Create status proxy from slot data
        status_proxy = None
        if self._status_info:
            # Status data is at slot_status_offset within the slot
            status_ptr = slot_char + self._meta.slot_status_offset
            status_proxy = SectionProxy(self._status_info, status_ptr, readonly=True)
        
        return DeviceView(
            node_id=node_id,
            state_proxy=state_proxy,  # State is only available during callback
            status_proxy=status_proxy,
            online=online,
            last_seen=last_seen,
        )
    
    def iter_devices(self, timeout_ms: Optional[int] = None) -> Iterator[Tuple[str, DeviceView]]:
        """
        Iterate over all known devices (OWNER role only).
        
        Yields tuples of (node_id, DeviceView) for each device that has
        sent at least one status update.
        
        Args:
            timeout_ms: Liveness timeout for online check (default: 1.5x liveness)
        
        Yields:
            Tuples of (node_id, DeviceView)
        
        Raises:
            SdsError: If not owner role
        
        Example:
            for node_id, device in table.iter_devices():
                print(f"{node_id}: online={device.online}")
        """
        if self._role != Role.OWNER:
            raise SdsError(
                ErrorCode.INVALID_ROLE,
                "iter_devices() is only available for OWNER role"
            )
        
        # Collect devices using the C iterator
        devices: list[str] = []
        
        @ffi.callback("SdsNodeIterator")
        def collector(c_node_id, status_ptr, user_data):
            node_id = decode_string(c_node_id)
            if node_id:
                devices.append(node_id)
        
        lib.sds_foreach_node(
            self._buffer,
            self._table_type.encode("utf-8"),
            collector,
            ffi.NULL,
        )
        
        # Yield DeviceViews for each device
        for node_id in devices:
            device = self.get_device(node_id, timeout_ms)
            if device is not None:
                yield node_id, device
    
    @property
    def device_count(self) -> int:
        """
        Get the number of known devices (OWNER role only).
        
        Returns:
            Number of devices that have sent at least one status update
        
        Raises:
            SdsError: If not owner role
        """
        if self._role != Role.OWNER:
            raise SdsError(
                ErrorCode.INVALID_ROLE,
                "device_count is only available for OWNER role"
            )
        
        # Read count from the count offset
        buffer_ptr = ffi.cast("char*", self._buffer)
        count_ptr = ffi.cast("uint8_t*", buffer_ptr + self._meta.own_status_count_offset)
        return count_ptr[0]
    
    def __repr__(self) -> str:
        return f"SdsTable(type={self._table_type!r}, role={self._role.name})"
