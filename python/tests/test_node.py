"""
Tests for sds.node module (SdsNode class).

Most tests require the CFFI extension and/or MQTT broker.
"""
import pytest

from sds import (
    SdsNode,
    Role,
    SdsError,
    SdsMqttError,
    ErrorCode,
    LogLevel,
    set_log_level,
    get_log_level,
)


class TestSdsNodeBasic:
    """Basic tests that don't require CFFI extension."""
    
    def test_node_class_exists(self):
        """SdsNode class can be imported."""
        assert SdsNode is not None
        assert callable(SdsNode)
    
    def test_role_enum(self):
        """Role enum is accessible."""
        assert Role.OWNER == 0
        assert Role.DEVICE == 1


@pytest.mark.requires_cffi
class TestSdsNodeInit:
    """Tests for SdsNode initialization (require CFFI but not MQTT)."""
    
    def test_node_init_fails_without_broker(self, unique_node_id):
        """SdsNode init fails gracefully when broker unavailable."""
        # This should fail with MQTT connection error
        with pytest.raises(SdsMqttError):
            SdsNode(
                unique_node_id,
                "non-existent-broker.invalid",
                auto_init=True
            )
    
    def test_node_no_auto_init(self, unique_node_id):
        """SdsNode can be created without auto_init."""
        # This should not fail because we don't try to connect
        node = SdsNode(
            unique_node_id,
            "localhost",
            auto_init=False
        )
        assert node.node_id == unique_node_id
        assert node.broker_host == "localhost"
        assert node.port == 1883
        assert not node._initialized
    
    def test_node_custom_port(self, unique_node_id):
        """SdsNode accepts custom port."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            port=1884,
            auto_init=False
        )
        assert node.port == 1884
    
    def test_node_with_credentials(self, unique_node_id):
        """SdsNode accepts credentials."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            username="user",
            password="pass",
            auto_init=False
        )
        assert node._username == "user"
        assert node._password == "pass"


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestSdsNodeWithBroker:
    """Tests that require a running MQTT broker."""
    
    def test_node_connects(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """SdsNode connects to MQTT broker."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            assert node.is_ready()
    
    def test_node_context_manager(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """SdsNode works as context manager."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            assert node.is_ready()
        # After exit, should be shutdown
        assert not node._initialized
    
    def test_node_shutdown(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """SdsNode.shutdown() works correctly."""
        node = SdsNode(unique_node_id, mqtt_broker_host, mqtt_broker_port)
        assert node.is_ready()
        node.shutdown()
        assert not node._initialized
        # Second shutdown should be safe
        node.shutdown()
    
    def test_node_get_stats(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """SdsNode.get_stats() returns statistics."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            stats = node.get_stats()
            assert "messages_sent" in stats
            assert "messages_received" in stats
            assert "reconnect_count" in stats
            assert "errors" in stats
    
    def test_node_poll(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """SdsNode.poll() processes events."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            # Poll should not raise
            node.poll()
            node.poll(timeout_ms=100)
    
    def test_node_get_table_count(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """SdsNode.get_table_count() returns count."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            count = node.get_table_count()
            assert isinstance(count, int)
            assert count >= 0


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestSdsNodeTables:
    """Tests for table registration (require CFFI and MQTT)."""
    
    def test_register_table_device(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Can register a table as device."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            # Note: This requires SensorData to be in the registry
            # which needs the generated types to be compiled
            try:
                node.register_table("SensorData", Role.DEVICE)
                assert node.get_table_count() == 1
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_register_table_owner(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Can register a table as owner."""
        with SdsNode(
            f"{unique_node_id}_owner",
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                node.register_table("SensorData", Role.OWNER)
                assert node.get_table_count() == 1
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_unregister_table(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Can unregister a table."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            try:
                node.register_table("SensorData", Role.DEVICE)
                assert node.get_table_count() == 1
                node.unregister_table("SensorData")
                assert node.get_table_count() == 0
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_register_table_not_found(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Registering unknown table raises error."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            with pytest.raises(SdsError) as exc_info:
                node.register_table("NonExistentTable", Role.DEVICE)
            assert exc_info.value.code == ErrorCode.TABLE_NOT_FOUND


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestSdsNodeCallbacks:
    """Tests for callback registration."""
    
    def test_on_config_decorator(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """@node.on_config() decorator works."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            callback_called = []
            
            @node.on_config("SensorData")
            def handle_config(table_type):
                callback_called.append(table_type)
            
            # Verify callback is registered
            assert "SensorData" in node._config_callbacks
    
    def test_on_state_decorator(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """@node.on_state() decorator works."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            @node.on_state("SensorData")
            def handle_state(table_type, from_node):
                pass
            
            assert "SensorData" in node._state_callbacks
    
    def test_on_status_decorator(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """@node.on_status() decorator works."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            @node.on_status("SensorData")
            def handle_status(table_type, from_node):
                pass
            
            assert "SensorData" in node._status_callbacks
    
    def test_on_error_decorator(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """@node.on_error() decorator works."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            @node.on_error
            def handle_error(error_code, context):
                pass
            
            assert node._error_callback is handle_error
    
    def test_on_error_callable(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """node.on_error() can be called directly."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            def handle_error(error_code, context):
                pass
            
            node.on_error(handle_error)
            assert node._error_callback is handle_error
    
    def test_on_version_mismatch_decorator(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """@node.on_version_mismatch() decorator works."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            @node.on_version_mismatch
            def handle_mismatch(table_type, device_id, local_ver, remote_ver):
                return False
            
            assert node._version_mismatch_callback is handle_mismatch


@pytest.mark.requires_cffi
class TestLogLevel:
    """Tests for log level control."""
    
    def test_log_level_enum(self):
        """LogLevel enum values are correct."""
        assert LogLevel.NONE == 0
        assert LogLevel.ERROR == 1
        assert LogLevel.WARN == 2
        assert LogLevel.INFO == 3
        assert LogLevel.DEBUG == 4
    
    def test_set_get_log_level(self):
        """set_log_level and get_log_level work correctly."""
        # Save original level
        original = get_log_level()
        
        try:
            set_log_level(LogLevel.DEBUG)
            assert get_log_level() == LogLevel.DEBUG
            
            set_log_level(LogLevel.WARN)
            assert get_log_level() == LogLevel.WARN
            
            set_log_level(LogLevel.ERROR)
            assert get_log_level() == LogLevel.ERROR
            
            set_log_level(LogLevel.NONE)
            assert get_log_level() == LogLevel.NONE
        finally:
            # Restore original level
            set_log_level(original)
    
    def test_set_log_level_before_init(self):
        """set_log_level can be called before creating SdsNode."""
        original = get_log_level()
        try:
            set_log_level(LogLevel.DEBUG)
            assert get_log_level() == LogLevel.DEBUG
        finally:
            set_log_level(original)
