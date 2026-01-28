"""
Table helpers for defining SDS table schemas in Python.

This module provides utilities for defining table schemas using Python
dataclasses and automatically generating the necessary metadata for
registration with the C library.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, fields, field
from enum import Enum
from typing import (
    Any,
    Callable,
    ClassVar,
    Dict,
    Generic,
    Optional,
    Type,
    TypeVar,
    Union,
    get_type_hints,
)

from sds._bindings import ffi, lib


# Type variable for generic table classes
T = TypeVar("T")


class FieldType(Enum):
    """
    Supported field types for table sections.
    
    These map to C types used in the SDS library.
    """
    INT8 = "int8"
    UINT8 = "uint8"
    INT16 = "int16"
    UINT16 = "uint16"
    INT32 = "int32"
    UINT32 = "uint32"
    FLOAT32 = "float32"
    BOOL = "bool"
    STRING = "string"  # Fixed-length string


# Mapping from FieldType to struct format and size
_FIELD_FORMATS = {
    FieldType.INT8: ("b", 1),
    FieldType.UINT8: ("B", 1),
    FieldType.INT16: ("h", 2),
    FieldType.UINT16: ("H", 2),
    FieldType.INT32: ("i", 4),
    FieldType.UINT32: ("I", 4),
    FieldType.FLOAT32: ("f", 4),
    FieldType.BOOL: ("?", 1),
}


def Field(
    field_type: Optional[FieldType] = None,
    *,
    uint8: bool = False,
    int8: bool = False,
    uint16: bool = False,
    int16: bool = False,
    uint32: bool = False,
    int32: bool = False,
    float32: bool = False,
    string_len: Optional[int] = None,
    default: Any = None,
) -> Any:
    """
    Define a field with explicit type information.
    
    This is used to specify the exact C type for a field when the Python
    type annotation is ambiguous.
    
    Args:
        field_type: The field type (alternative to bool flags)
        uint8: If True, field is uint8_t
        int8: If True, field is int8_t
        uint16: If True, field is uint16_t
        int16: If True, field is int16_t
        uint32: If True, field is uint32_t
        int32: If True, field is int32_t
        float32: If True, field is float (32-bit)
        string_len: If set, field is a fixed-length string
        default: Default value for the field
    
    Returns:
        Field metadata for use in dataclass
    
    Example:
        @dataclass
        class SensorConfig:
            command: int = Field(uint8=True, default=0)
            threshold: float = Field(float32=True, default=25.0)
            name: str = Field(string_len=32, default="")
    """
    # Determine field type from bool flags
    if field_type is None:
        if uint8:
            field_type = FieldType.UINT8
        elif int8:
            field_type = FieldType.INT8
        elif uint16:
            field_type = FieldType.UINT16
        elif int16:
            field_type = FieldType.INT16
        elif uint32:
            field_type = FieldType.UINT32
        elif int32:
            field_type = FieldType.INT32
        elif float32:
            field_type = FieldType.FLOAT32
        elif string_len is not None:
            field_type = FieldType.STRING
    
    # Create field metadata
    metadata = {
        "sds_field_type": field_type,
        "sds_string_len": string_len,
    }
    
    if default is not None:
        return field(default=default, metadata=metadata)
    else:
        return field(default_factory=lambda: _get_default_for_type(field_type), metadata=metadata)


def _get_default_for_type(field_type: Optional[FieldType]) -> Any:
    """Get the default value for a field type."""
    if field_type is None:
        return None
    if field_type == FieldType.STRING:
        return ""
    if field_type == FieldType.BOOL:
        return False
    if field_type == FieldType.FLOAT32:
        return 0.0
    return 0


@dataclass
class TableFieldInfo:
    """Information about a single field in a table section."""
    name: str
    field_type: FieldType
    offset: int
    size: int
    string_len: Optional[int] = None


@dataclass
class TableSectionInfo:
    """Information about a table section (config, state, or status)."""
    fields: list[TableFieldInfo]
    total_size: int
    
    def serialize(self, data: Any, buffer: bytes) -> bytes:
        """Serialize a dataclass instance to bytes."""
        result = bytearray(self.total_size)
        
        for field_info in self.fields:
            value = getattr(data, field_info.name, None)
            if value is None:
                continue
            
            if field_info.field_type == FieldType.STRING:
                # Encode string with null terminator
                encoded = value.encode("utf-8")[:field_info.string_len - 1]
                result[field_info.offset:field_info.offset + len(encoded)] = encoded
            else:
                fmt, _ = _FIELD_FORMATS[field_info.field_type]
                packed = struct.pack(fmt, value)
                result[field_info.offset:field_info.offset + len(packed)] = packed
        
        return bytes(result)
    
    def deserialize(self, buffer: bytes, cls: Type[T]) -> T:
        """Deserialize bytes to a dataclass instance."""
        kwargs = {}
        
        for field_info in self.fields:
            if field_info.field_type == FieldType.STRING:
                # Read null-terminated string
                raw = buffer[field_info.offset:field_info.offset + field_info.string_len]
                null_idx = raw.find(b'\x00')
                if null_idx >= 0:
                    raw = raw[:null_idx]
                kwargs[field_info.name] = raw.decode("utf-8", errors="replace")
            else:
                fmt, size = _FIELD_FORMATS[field_info.field_type]
                raw = buffer[field_info.offset:field_info.offset + size]
                kwargs[field_info.name] = struct.unpack(fmt, raw)[0]
        
        return cls(**kwargs)


def analyze_dataclass(cls: Type) -> TableSectionInfo:
    """
    Analyze a dataclass to extract field information.
    
    Args:
        cls: A dataclass type
        
    Returns:
        TableSectionInfo with field layout
    """
    field_infos = []
    offset = 0
    
    # Get type hints for proper type inference
    hints = get_type_hints(cls) if hasattr(cls, "__annotations__") else {}
    
    for f in fields(cls):
        # Check for explicit field type in metadata
        metadata = f.metadata or {}
        field_type = metadata.get("sds_field_type")
        string_len = metadata.get("sds_string_len")
        
        # Infer type from annotation if not explicit
        if field_type is None:
            hint = hints.get(f.name, f.type)
            field_type = _infer_field_type(hint)
        
        # Calculate size
        if field_type == FieldType.STRING:
            if string_len is None:
                string_len = 32  # Default string length
            size = string_len
        else:
            _, size = _FIELD_FORMATS[field_type]
        
        field_infos.append(TableFieldInfo(
            name=f.name,
            field_type=field_type,
            offset=offset,
            size=size,
            string_len=string_len,
        ))
        
        offset += size
    
    return TableSectionInfo(fields=field_infos, total_size=offset)


def _infer_field_type(hint: Any) -> FieldType:
    """Infer field type from Python type hint."""
    # Handle Optional types
    if hasattr(hint, "__origin__"):
        if hint.__origin__ is Union:
            # Get the non-None type from Optional
            args = [a for a in hint.__args__ if a is not type(None)]
            if args:
                hint = args[0]
    
    # Map Python types to field types
    if hint is bool:
        return FieldType.BOOL
    elif hint is int:
        return FieldType.INT32  # Default to 32-bit
    elif hint is float:
        return FieldType.FLOAT32
    elif hint is str:
        return FieldType.STRING
    else:
        return FieldType.INT32  # Fallback


class TableSection(Generic[T]):
    """
    Wrapper for a table section that provides Pythonic access.
    
    This class wraps the C memory buffer for a section and provides
    attribute access that automatically serializes/deserializes.
    """
    
    def __init__(self, section_info: TableSectionInfo, cls: Type[T], buffer: Any):
        self._info = section_info
        self._cls = cls
        self._buffer = buffer
        self._cache: Optional[T] = None
    
    def get(self) -> T:
        """Get the current section data as a dataclass instance."""
        # Read from C buffer
        raw = ffi.buffer(self._buffer, self._info.total_size)[:]
        return self._info.deserialize(raw, self._cls)
    
    def set(self, data: T) -> None:
        """Set the section data from a dataclass instance."""
        serialized = self._info.serialize(data, b"")
        ffi.memmove(self._buffer, serialized, len(serialized))
    
    def update(self, **kwargs: Any) -> None:
        """Update specific fields in the section."""
        current = self.get()
        for key, value in kwargs.items():
            if hasattr(current, key):
                setattr(current, key, value)
        self.set(current)


def create_json_serializer(section_info: TableSectionInfo) -> Callable:
    """
    Create a JSON serializer function for a table section.
    
    This creates a function compatible with the SdsSerializeFunc callback.
    """
    @ffi.callback("void(void*, SdsJsonWriter*)")
    def serialize(section_ptr, writer):
        # Read section data
        raw = ffi.buffer(section_ptr, section_info.total_size)[:]
        
        for field_info in section_info.fields:
            if field_info.field_type == FieldType.STRING:
                # Read null-terminated string
                raw_str = raw[field_info.offset:field_info.offset + field_info.string_len]
                null_idx = raw_str.find(b'\x00')
                if null_idx >= 0:
                    raw_str = raw_str[:null_idx]
                value = raw_str.decode("utf-8", errors="replace")
                lib.sds_json_add_string(writer, field_info.name.encode(), value.encode())
            elif field_info.field_type == FieldType.BOOL:
                value = struct.unpack("?", raw[field_info.offset:field_info.offset + 1])[0]
                lib.sds_json_add_bool(writer, field_info.name.encode(), value)
            elif field_info.field_type == FieldType.FLOAT32:
                value = struct.unpack("f", raw[field_info.offset:field_info.offset + 4])[0]
                lib.sds_json_add_float(writer, field_info.name.encode(), value)
            elif field_info.field_type in (FieldType.INT8, FieldType.INT16, FieldType.INT32):
                fmt, size = _FIELD_FORMATS[field_info.field_type]
                value = struct.unpack(fmt, raw[field_info.offset:field_info.offset + size])[0]
                lib.sds_json_add_int(writer, field_info.name.encode(), value)
            else:  # Unsigned types
                fmt, size = _FIELD_FORMATS[field_info.field_type]
                value = struct.unpack(fmt, raw[field_info.offset:field_info.offset + size])[0]
                lib.sds_json_add_uint(writer, field_info.name.encode(), value)
    
    return serialize


def create_json_deserializer(section_info: TableSectionInfo) -> Callable:
    """
    Create a JSON deserializer function for a table section.
    
    This creates a function compatible with the SdsDeserializeFunc callback.
    """
    @ffi.callback("void(void*, SdsJsonReader*)")
    def deserialize(section_ptr, reader):
        result = bytearray(section_info.total_size)
        
        for field_info in section_info.fields:
            key = field_info.name.encode()
            
            if field_info.field_type == FieldType.STRING:
                buf = ffi.new(f"char[{field_info.string_len}]")
                if lib.sds_json_get_string_field(reader, key, buf, field_info.string_len):
                    value = ffi.string(buf).decode("utf-8")
                    encoded = value.encode("utf-8")[:field_info.string_len - 1]
                    result[field_info.offset:field_info.offset + len(encoded)] = encoded
            elif field_info.field_type == FieldType.BOOL:
                out = ffi.new("bool*")
                if lib.sds_json_get_bool_field(reader, key, out):
                    result[field_info.offset] = 1 if out[0] else 0
            elif field_info.field_type == FieldType.FLOAT32:
                out = ffi.new("float*")
                if lib.sds_json_get_float_field(reader, key, out):
                    packed = struct.pack("f", out[0])
                    result[field_info.offset:field_info.offset + 4] = packed
            elif field_info.field_type in (FieldType.INT8, FieldType.INT16, FieldType.INT32):
                out = ffi.new("int32_t*")
                if lib.sds_json_get_int_field(reader, key, out):
                    fmt, size = _FIELD_FORMATS[field_info.field_type]
                    packed = struct.pack(fmt, out[0])
                    result[field_info.offset:field_info.offset + size] = packed
            elif field_info.field_type == FieldType.UINT8:
                out = ffi.new("uint8_t*")
                if lib.sds_json_get_uint8_field(reader, key, out):
                    result[field_info.offset] = out[0]
            else:  # Other unsigned types
                out = ffi.new("uint32_t*")
                if lib.sds_json_get_uint_field(reader, key, out):
                    fmt, size = _FIELD_FORMATS[field_info.field_type]
                    packed = struct.pack(fmt, out[0])
                    result[field_info.offset:field_info.offset + size] = packed
        
        # Copy result to section
        ffi.memmove(section_ptr, bytes(result), section_info.total_size)
    
    return deserialize
