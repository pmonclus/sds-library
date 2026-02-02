"""
Tests for signed integer serialization/deserialization.

Validates that int8, int16, and uint16 types are correctly handled,
including negative values for signed types.
"""
import pytest
from dataclasses import dataclass

from sds.tables import Field, FieldType


class TestFieldTypes:
    """Tests for field type definitions."""
    
    def test_int8_field_type_exists(self):
        """INT8 field type is defined."""
        assert FieldType.INT8 is not None
        assert FieldType.INT8.value == "int8"
    
    def test_int16_field_type_exists(self):
        """INT16 field type is defined."""
        assert FieldType.INT16 is not None
        assert FieldType.INT16.value == "int16"
    
    def test_uint16_field_type_exists(self):
        """UINT16 field type is defined."""
        assert FieldType.UINT16 is not None
        assert FieldType.UINT16.value == "uint16"


class TestFieldHelper:
    """Tests for Field() helper with signed types."""
    
    def test_field_int8_flag(self):
        """Field(int8=True) creates INT8 field."""
        f = Field(int8=True, default=-50)
        assert f.metadata["sds_field_type"] == FieldType.INT8
        assert f.default == -50
    
    def test_field_int16_flag(self):
        """Field(int16=True) creates INT16 field."""
        f = Field(int16=True, default=-1000)
        assert f.metadata["sds_field_type"] == FieldType.INT16
        assert f.default == -1000
    
    def test_field_uint16_flag(self):
        """Field(uint16=True) creates UINT16 field."""
        f = Field(uint16=True, default=50000)
        assert f.metadata["sds_field_type"] == FieldType.UINT16
        assert f.default == 50000


class TestSignedIntegerRanges:
    """Tests for signed integer value ranges."""
    
    def test_int8_accepts_negative(self):
        """INT8 field accepts negative values."""
        field = Field(int8=True, default=-128)  # min int8
        assert field.default == -128
    
    def test_int8_accepts_positive(self):
        """INT8 field accepts positive values."""
        field = Field(int8=True, default=127)  # max int8
        assert field.default == 127
    
    def test_int16_accepts_negative(self):
        """INT16 field accepts negative values."""
        field = Field(int16=True, default=-32768)  # min int16
        assert field.default == -32768
    
    def test_int16_accepts_positive(self):
        """INT16 field accepts positive values."""
        field = Field(int16=True, default=32767)  # max int16
        assert field.default == 32767
    
    def test_uint16_accepts_large_values(self):
        """UINT16 field accepts values up to 65535."""
        field = Field(uint16=True, default=65535)  # max uint16
        assert field.default == 65535


class TestWifiRssiUseCase:
    """Tests for the WiFi RSSI use case from the bug report."""
    
    def test_wifi_rssi_field_definition(self):
        """WiFi RSSI field can be defined as int8 with negative default."""
        # Typical WiFi RSSI range: -30 to -90 dBm
        f = Field(int8=True, default=-62)
        assert f.metadata["sds_field_type"] == FieldType.INT8
        assert f.default == -62
    
    def test_int8_negative_value_preserved(self):
        """Negative int8 values are preserved in field metadata."""
        rssi_field = Field(int8=True, default=-75)
        # The field should correctly store -75, not 0 or 181 (unsigned interpretation)
        assert rssi_field.default == -75
        assert rssi_field.default < 0


class TestTableSectionInfoWithSignedTypes:
    """Tests for TableSectionInfo with signed integer fields."""
    
    def test_analyze_dataclass_with_int8(self):
        """analyze_dataclass correctly handles int8 fields."""
        from sds.tables import analyze_dataclass
        
        @dataclass
        class StatusWithInt8:
            wifi_rssi: int = Field(int8=True, default=-62)
            temperature_offset: int = Field(int8=True, default=0)
        
        info = analyze_dataclass(StatusWithInt8)
        assert info is not None
        
        # Find the wifi_rssi field
        rssi_field = next((f for f in info.fields if f.name == "wifi_rssi"), None)
        assert rssi_field is not None
        assert rssi_field.field_type == FieldType.INT8
    
    def test_analyze_dataclass_with_int16(self):
        """analyze_dataclass correctly handles int16 fields."""
        from sds.tables import analyze_dataclass
        
        @dataclass
        class StatusWithInt16:
            altitude: int = Field(int16=True, default=0)
            delta: int = Field(int16=True, default=-100)
        
        info = analyze_dataclass(StatusWithInt16)
        assert info is not None
        
        # Find the altitude field
        altitude_field = next((f for f in info.fields if f.name == "altitude"), None)
        assert altitude_field is not None
        assert altitude_field.field_type == FieldType.INT16
    
    def test_analyze_dataclass_with_uint16(self):
        """analyze_dataclass correctly handles uint16 fields."""
        from sds.tables import analyze_dataclass
        
        @dataclass
        class StatusWithUint16:
            port: int = Field(uint16=True, default=8080)
            counter: int = Field(uint16=True, default=0)
        
        info = analyze_dataclass(StatusWithUint16)
        assert info is not None
        
        # Find the port field
        port_field = next((f for f in info.fields if f.name == "port"), None)
        assert port_field is not None
        assert port_field.field_type == FieldType.UINT16


class TestSignedIntegerSerialization:
    """Tests for signed integer JSON serialization."""
    
    def test_int8_serialization_format(self):
        """INT8 values should use sds_json_add_int for correct signed handling."""
        from sds.tables import FieldType, _FIELD_FORMATS
        
        # Verify int8 uses signed format
        fmt, size = _FIELD_FORMATS[FieldType.INT8]
        assert fmt == "b"  # Signed byte
        assert size == 1
    
    def test_int16_serialization_format(self):
        """INT16 values should use signed short format."""
        from sds.tables import FieldType, _FIELD_FORMATS
        
        fmt, size = _FIELD_FORMATS[FieldType.INT16]
        assert fmt == "h"  # Signed short
        assert size == 2
    
    def test_uint16_serialization_format(self):
        """UINT16 values should use unsigned short format."""
        from sds.tables import FieldType, _FIELD_FORMATS
        
        fmt, size = _FIELD_FORMATS[FieldType.UINT16]
        assert fmt == "H"  # Unsigned short
        assert size == 2
