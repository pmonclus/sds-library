"""
Tests for sds.table module (SdsTable, SectionProxy, DeviceView).

Tests C-like attribute access to table sections.
"""
import pytest
from dataclasses import dataclass

from sds import (
    SdsNode,
    SdsTable,
    SectionProxy,
    DeviceView,
    Role,
    SdsError,
    ErrorCode,
    Field,
    FieldType,
)


# ============== Schema Dataclasses (for testing) ==============


@dataclass
class SampleConfig:
    """Sample config schema for tests."""
    command: int = Field(uint8=True, default=0)
    threshold: float = Field(float32=True, default=25.0)


@dataclass
class SampleState:
    """Sample state schema for tests."""
    temperature: float = Field(float32=True, default=0.0)
    humidity: float = Field(float32=True, default=0.0)
    count: int = Field(uint32=True, default=0)


@dataclass
class SampleStatus:
    """Sample status schema for tests."""
    error_code: int = Field(uint8=True, default=0)
    battery: int = Field(uint8=True, default=100)
    active: bool = Field(default=False)


# ============== Tests ==============


class TestSdsTableClass:
    """Basic tests for SdsTable class."""
    
    def test_sdstable_class_exists(self):
        """SdsTable class can be imported."""
        assert SdsTable is not None
    
    def test_section_proxy_class_exists(self):
        """SectionProxy class can be imported."""
        assert SectionProxy is not None
    
    def test_device_view_class_exists(self):
        """DeviceView class can be imported."""
        assert DeviceView is not None
    
    def test_field_helper(self):
        """Field helper creates correct metadata."""
        field = Field(uint8=True, default=5)
        assert field is not None


class TestFieldType:
    """Tests for FieldType enum."""
    
    def test_field_types_exist(self):
        """All field types are defined."""
        assert FieldType.INT8 is not None
        assert FieldType.UINT8 is not None
        assert FieldType.INT16 is not None
        assert FieldType.UINT16 is not None
        assert FieldType.INT32 is not None
        assert FieldType.UINT32 is not None
        assert FieldType.FLOAT32 is not None
        assert FieldType.BOOL is not None
        assert FieldType.STRING is not None


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestSdsTableDevice:
    """Tests for SdsTable with device role (require CFFI and MQTT)."""
    
    def test_register_table_returns_sdstable(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """register_table() returns SdsTable object."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.DEVICE,
                    state_schema=SampleState,
                )
                assert isinstance(table, SdsTable)
                assert table.table_type == "SensorData"
                assert table.role == Role.DEVICE
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_table_state_write(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Can write to table.state fields."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.DEVICE,
                    state_schema=SampleState,
                )
                
                # Write values
                table.state.temperature = 23.5
                table.state.humidity = 65.0
                table.state.count = 42
                
                # Read back
                assert table.state.temperature == pytest.approx(23.5)
                assert table.state.humidity == pytest.approx(65.0)
                assert table.state.count == 42
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_table_status_write(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Can write to table.status fields."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.DEVICE,
                    status_schema=SampleStatus,
                )
                
                # Write values
                table.status.error_code = 5
                table.status.battery = 80
                table.status.active = True
                
                # Read back
                assert table.status.error_code == 5
                assert table.status.battery == 80
                assert table.status.active is True
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_table_config_readonly(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Device cannot write to config (readonly)."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.DEVICE,
                    config_schema=SampleConfig,
                )
                
                # Reading should work
                _ = table.config.command
                
                # Writing should fail
                with pytest.raises(AttributeError, match="read-only"):
                    table.config.command = 10
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_table_invalid_field_raises(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Accessing invalid field raises AttributeError."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.DEVICE,
                    state_schema=SampleState,
                )
                
                with pytest.raises(AttributeError, match="No field named"):
                    _ = table.state.nonexistent_field
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_device_cannot_access_owner_methods(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Device role cannot use get_device() or iter_devices()."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.DEVICE,
                )
                
                with pytest.raises(SdsError, match="OWNER role"):
                    table.get_device("sensor_01")
                
                with pytest.raises(SdsError, match="OWNER role"):
                    list(table.iter_devices())
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestSdsTableOwner:
    """Tests for SdsTable with owner role (require CFFI and MQTT)."""
    
    def test_owner_config_write(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Owner can write to config."""
        with SdsNode(
            f"{unique_node_id}_owner",
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.OWNER,
                    config_schema=SampleConfig,
                )
                
                # Write config
                table.config.command = 5
                table.config.threshold = 30.0
                
                # Read back
                assert table.config.command == 5
                assert table.config.threshold == pytest.approx(30.0)
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_owner_cannot_access_state_directly(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Owner cannot access table.state (must use get_device())."""
        with SdsNode(
            f"{unique_node_id}_owner",
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.OWNER,
                    state_schema=SampleState,
                )
                
                with pytest.raises(SdsError, match="DEVICE role"):
                    _ = table.state
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_owner_get_device_returns_none_for_unknown(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """get_device() returns None for unknown device."""
        with SdsNode(
            f"{unique_node_id}_owner",
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.OWNER,
                )
                
                device = table.get_device("nonexistent_device")
                assert device is None
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_owner_iter_devices_empty(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """iter_devices() returns empty for no devices."""
        with SdsNode(
            f"{unique_node_id}_owner",
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.OWNER,
                )
                
                devices = list(table.iter_devices())
                assert devices == []
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_owner_device_count(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """device_count returns 0 when no devices."""
        with SdsNode(
            f"{unique_node_id}_owner",
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.OWNER,
                )
                
                assert table.device_count == 0
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestGetTable:
    """Tests for node.get_table()."""
    
    def test_get_table_returns_same_table(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """get_table() returns the same SdsTable instance."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                table1 = node.register_table("SensorData", Role.DEVICE)
                table2 = node.get_table("SensorData")
                
                assert table1 is table2
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_get_table_not_found(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """get_table() raises error for unregistered table."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            with pytest.raises(SdsError) as exc_info:
                node.get_table("NonExistent")
            assert exc_info.value.code == ErrorCode.TABLE_NOT_FOUND
