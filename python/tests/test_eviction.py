"""
Tests for device eviction functionality.

Tests the eviction_grace_ms configuration and on_device_evicted callback.
"""
import pytest
from dataclasses import dataclass

from sds import SdsNode, Role, SdsError, ErrorCode
from sds.table import SdsTable, DeviceView
from sds.tables import Field


# ============== Test Schemas ==============

@dataclass
class MockStatus:
    """Simple status schema for eviction tests."""
    battery: int = Field(uint8=True, default=100)
    error_code: int = Field(uint8=True, default=0)


# ============== Unit Tests (no broker required) ==============


class TestEvictionConfiguration:
    """Tests for eviction configuration (no broker required)."""
    
    def test_eviction_grace_ms_default(self, unique_node_id):
        """Default eviction_grace_ms is 0 (disabled)."""
        node = SdsNode(unique_node_id, "localhost", auto_init=False)
        assert node.eviction_grace_ms == 0
    
    def test_eviction_grace_ms_custom(self, unique_node_id):
        """Custom eviction_grace_ms is stored correctly."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            auto_init=False,
            eviction_grace_ms=5000
        )
        assert node.eviction_grace_ms == 5000
    
    def test_eviction_grace_ms_zero_disables(self, unique_node_id):
        """Setting eviction_grace_ms=0 disables eviction."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            auto_init=False,
            eviction_grace_ms=0
        )
        assert node.eviction_grace_ms == 0


class TestEvictionCallback:
    """Tests for eviction callback registration (no broker required)."""
    
    def test_on_device_evicted_decorator_exists(self, unique_node_id):
        """on_device_evicted decorator method exists."""
        node = SdsNode(unique_node_id, "localhost", auto_init=False)
        assert hasattr(node, 'on_device_evicted')
        assert callable(node.on_device_evicted)
    
    def test_on_device_evicted_returns_decorator(self, unique_node_id):
        """on_device_evicted() returns a decorator function."""
        node = SdsNode(unique_node_id, "localhost", auto_init=False)
        decorator = node.on_device_evicted()
        assert callable(decorator)
    
    def test_on_device_evicted_decorator_registers_callback(self, unique_node_id):
        """Decorator registers the callback function."""
        node = SdsNode(unique_node_id, "localhost", auto_init=False)
        
        callback_registered = False
        
        @node.on_device_evicted()
        def handle_eviction(table_type: str, node_id: str):
            nonlocal callback_registered
            callback_registered = True
        
        # The callback should be stored
        assert node._eviction_callback is not None
        assert node._eviction_callback == handle_eviction


class TestDeviceViewEvictionPending:
    """Tests for eviction_pending field in DeviceView."""
    
    def test_device_view_has_eviction_pending(self):
        """DeviceView has eviction_pending attribute."""
        view = DeviceView(
            node_id="test_device",
            state_proxy=None,
            status_proxy=None,
            online=True,
            last_seen=12345,
            eviction_pending=False,
        )
        assert hasattr(view, 'eviction_pending')
        assert view.eviction_pending == False
    
    def test_device_view_eviction_pending_true(self):
        """DeviceView correctly reports eviction_pending=True."""
        view = DeviceView(
            node_id="test_device",
            state_proxy=None,
            status_proxy=None,
            online=False,
            last_seen=12345,
            eviction_pending=True,
        )
        assert view.eviction_pending == True
    
    def test_device_view_repr_includes_eviction_pending(self):
        """DeviceView repr includes eviction_pending when True."""
        view = DeviceView(
            node_id="test_device",
            state_proxy=None,
            status_proxy=None,
            online=False,
            last_seen=12345,
            eviction_pending=True,
        )
        repr_str = repr(view)
        assert "eviction_pending=True" in repr_str
    
    def test_device_view_repr_excludes_eviction_pending_when_false(self):
        """DeviceView repr excludes eviction_pending when False."""
        view = DeviceView(
            node_id="test_device",
            state_proxy=None,
            status_proxy=None,
            online=True,
            last_seen=12345,
            eviction_pending=False,
        )
        repr_str = repr(view)
        assert "eviction_pending" not in repr_str


# ============== Integration Tests (require broker) ==============


@pytest.mark.requires_mqtt
class TestEvictionWithBroker:
    """Integration tests for eviction (require MQTT broker)."""
    
    def test_get_eviction_grace_returns_configured_value(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """get_eviction_grace() returns the configured value."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            eviction_grace_ms=3000,
        ) as node:
            assert node.get_eviction_grace() == 3000
    
    def test_get_eviction_grace_zero_when_disabled(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """get_eviction_grace() returns 0 when eviction disabled."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            eviction_grace_ms=0,
        ) as node:
            assert node.get_eviction_grace() == 0
    
    def test_eviction_callback_setup_with_broker(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Eviction callback can be set up when connected to broker."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            eviction_grace_ms=5000,
        ) as node:
            evictions = []
            
            @node.on_device_evicted()
            def handle_eviction(table_type: str, node_id: str):
                evictions.append((table_type, node_id))
            
            # Callback should be registered
            assert node._eviction_callback is not None
            
            # Run a few poll cycles to ensure no crashes
            for _ in range(5):
                node.poll(timeout_ms=10)
    
    def test_owner_table_with_eviction_registers(
        self,
        unique_node_id,
        mqtt_broker_host,
        mqtt_broker_port,
    ):
        """Owner table registration works with eviction enabled."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            eviction_grace_ms=2000,
        ) as node:
            try:
                table = node.register_table(
                    "SensorData",
                    Role.OWNER,
                )
                assert table is not None
                assert table.role == Role.OWNER
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
