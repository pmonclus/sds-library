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
    
    def test_delta_sync_disabled_by_default(self, unique_node_id):
        """Delta sync is disabled by default."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            auto_init=False
        )
        assert node.delta_sync_enabled is False
        assert node.delta_float_tolerance == 0.001
    
    def test_delta_sync_can_be_enabled(self, unique_node_id):
        """Delta sync can be enabled via parameter."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            enable_delta_sync=True,
            auto_init=False
        )
        assert node.delta_sync_enabled is True
    
    def test_delta_float_tolerance_configurable(self, unique_node_id):
        """Delta float tolerance can be customized."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            enable_delta_sync=True,
            delta_float_tolerance=0.5,
            auto_init=False
        )
        assert node.delta_float_tolerance == 0.5


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
@pytest.mark.requires_mqtt
class TestRawPublishAPI:
    """Tests for raw MQTT publish API."""
    
    def test_is_connected_when_connected(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """is_connected() returns True when connected to broker."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            assert node.is_connected() is True
    
    def test_publish_raw_success(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """publish_raw() sends message successfully."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            result = node.publish_raw(
                f"test/{unique_node_id}/raw",
                '{"msg": "hello"}',
                qos=0
            )
            assert result is True
    
    def test_publish_raw_with_bytes(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """publish_raw() accepts bytes payload."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            result = node.publish_raw(
                f"test/{unique_node_id}/bytes",
                b'\x00\x01\x02\x03',
                qos=0
            )
            assert result is True
    
    def test_publish_raw_retained(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """publish_raw() can set retained flag."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            result = node.publish_raw(
                f"test/{unique_node_id}/retained",
                "retained message",
                retained=True
            )
            assert result is True
    
    def test_publish_raw_empty_topic_raises(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """publish_raw() raises ValueError for empty topic."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            with pytest.raises(ValueError):
                node.publish_raw("", "message")
    
    def test_publish_raw_invalid_qos_raises(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """publish_raw() raises ValueError for invalid QoS."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            with pytest.raises(ValueError):
                node.publish_raw("test/topic", "message", qos=5)


@pytest.mark.requires_cffi
class TestIsConnectedWithoutBroker:
    """Tests for is_connected() without MQTT broker."""
    
    def test_is_connected_false_when_not_initialized(self, unique_node_id):
        """is_connected() returns False when not initialized."""
        node = SdsNode(
            unique_node_id,
            "localhost",
            auto_init=False
        )
        assert node.is_connected() is False


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestRawSubscribeAPI:
    """Tests for raw MQTT subscribe API."""
    
    def test_subscribe_raw_success(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """subscribe_raw() succeeds with valid topic."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            received = []
            def on_message(topic, payload):
                received.append((topic, payload))
            
            result = node.subscribe_raw(f"test/{unique_node_id}/sub", on_message)
            assert result is True
    
    def test_subscribe_raw_rejects_sds_prefix(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """subscribe_raw() rejects topics starting with sds/."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            def on_message(topic, payload):
                pass
            
            with pytest.raises(ValueError):
                node.subscribe_raw("sds/test/config", on_message)
    
    def test_subscribe_raw_empty_topic_raises(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """subscribe_raw() raises ValueError for empty topic."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            def on_message(topic, payload):
                pass
            
            with pytest.raises(ValueError):
                node.subscribe_raw("", on_message)
    
    def test_unsubscribe_raw_success(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """unsubscribe_raw() succeeds after subscribe."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            def on_message(topic, payload):
                pass
            
            topic = f"test/{unique_node_id}/unsub"
            node.subscribe_raw(topic, on_message)
            result = node.unsubscribe_raw(topic)
            assert result is True
    
    def test_unsubscribe_raw_fails_when_not_subscribed(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """unsubscribe_raw() returns False when not subscribed."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port
        ) as node:
            result = node.unsubscribe_raw("test/not_subscribed")
            assert result is False


@pytest.mark.requires_cffi
@pytest.mark.requires_mqtt
class TestDeltaSyncWithBroker:
    """Tests for delta sync behavior with a running MQTT broker."""
    
    def test_delta_sync_node_connects(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Node with delta sync enabled connects successfully."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            enable_delta_sync=True
        ) as node:
            assert node.is_ready()
            assert node.delta_sync_enabled is True
    
    def test_delta_sync_with_custom_tolerance(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Node with custom float tolerance connects successfully."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            enable_delta_sync=True,
            delta_float_tolerance=0.1
        ) as node:
            assert node.is_ready()
            assert node.delta_float_tolerance == 0.1
    
    def test_delta_sync_register_table_device(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Can register table as device with delta sync enabled."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            enable_delta_sync=True
        ) as node:
            try:
                node.register_table("SensorData", Role.DEVICE)
                assert node.get_table_count() == 1
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_delta_sync_register_table_owner(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Can register table as owner with delta sync enabled."""
        with SdsNode(
            f"{unique_node_id}_owner",
            mqtt_broker_host,
            mqtt_broker_port,
            enable_delta_sync=True
        ) as node:
            try:
                node.register_table("SensorData", Role.OWNER)
                assert node.get_table_count() == 1
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_delta_sync_stats_available(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Stats are available with delta sync enabled."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            enable_delta_sync=True
        ) as node:
            stats = node.get_stats()
            assert "messages_sent" in stats
            assert isinstance(stats["messages_sent"], int)
    
    def test_delta_sync_poll_works(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Poll works correctly with delta sync enabled."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            enable_delta_sync=True
        ) as node:
            try:
                node.register_table("SensorData", Role.DEVICE)
                # Poll multiple times to ensure sync happens
                for _ in range(3):
                    node.poll(timeout_ms=100)
            except SdsError as e:
                if e.code == ErrorCode.TABLE_NOT_FOUND:
                    pytest.skip("SensorData table not in registry")
                raise
    
    def test_delta_sync_preserves_other_config(self, unique_node_id, mqtt_broker_host, mqtt_broker_port):
        """Delta sync config doesn't interfere with other config options."""
        with SdsNode(
            unique_node_id,
            mqtt_broker_host,
            mqtt_broker_port,
            enable_delta_sync=True,
            delta_float_tolerance=0.05,
            eviction_grace_ms=5000,
            connect_timeout_ms=3000,
            retry_count=2
        ) as node:
            assert node.is_ready()
            assert node.delta_sync_enabled is True
            assert node.delta_float_tolerance == 0.05
            # Other config should work too
            assert node._eviction_grace_ms == 5000


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
