"""
Tests for sds.types module (enums and exceptions).

These tests don't require the CFFI extension to be built.
"""
import pytest

from sds.types import (
    Role,
    ErrorCode,
    SdsError,
    SdsNotInitializedError,
    SdsAlreadyInitializedError,
    SdsConfigError,
    SdsMqttError,
    SdsTableError,
    SdsCapacityError,
    SdsPlatformError,
    check_error,
)


class TestRole:
    """Tests for the Role enum."""
    
    def test_role_values(self):
        """Role enum has correct integer values."""
        assert Role.OWNER == 0
        assert Role.DEVICE == 1
    
    def test_role_is_int_enum(self):
        """Role can be used as an integer."""
        assert int(Role.OWNER) == 0
        assert int(Role.DEVICE) == 1
    
    def test_role_comparison(self):
        """Role can be compared to integers."""
        assert Role.OWNER == 0
        assert Role.DEVICE == 1
        assert Role.OWNER != Role.DEVICE


class TestErrorCode:
    """Tests for the ErrorCode enum."""
    
    def test_error_code_ok(self):
        """OK error code is 0."""
        assert ErrorCode.OK == 0
    
    def test_error_code_categories(self):
        """Error codes have expected values."""
        # Initialization errors (1-3)
        assert ErrorCode.NOT_INITIALIZED == 1
        assert ErrorCode.ALREADY_INITIALIZED == 2
        assert ErrorCode.INVALID_CONFIG == 3
        
        # Connection errors (4-5)
        assert ErrorCode.MQTT_CONNECT_FAILED == 4
        assert ErrorCode.MQTT_DISCONNECTED == 5
        
        # Table errors (6-11)
        assert ErrorCode.TABLE_NOT_FOUND == 6
        assert ErrorCode.TABLE_ALREADY_REGISTERED == 7
    
    def test_error_code_is_int_enum(self):
        """ErrorCode can be used as an integer."""
        assert int(ErrorCode.OK) == 0
        assert int(ErrorCode.NOT_INITIALIZED) == 1


class TestExceptions:
    """Tests for exception classes."""
    
    def test_sds_error_basic(self):
        """SdsError can be created with code and message."""
        err = SdsError(1, "test message")
        assert err.code == ErrorCode.NOT_INITIALIZED
        assert err.message == "test message"
        assert "test message" in str(err)
        assert "1" in str(err) or "NOT_INITIALIZED" in str(err)
    
    def test_sds_error_code_only(self):
        """SdsError with code only gets default message."""
        err = SdsError(0)  # OK
        assert err.code == ErrorCode.OK
    
    def test_sds_error_unknown_code(self):
        """SdsError handles unknown error codes."""
        err = SdsError(999, "unknown error")
        assert err.code == 999  # Kept as int
        assert err.message == "unknown error"
    
    def test_sds_error_from_code_ok(self):
        """from_code returns None for OK."""
        result = SdsError.from_code(ErrorCode.OK)
        assert result is None
    
    def test_sds_error_from_code_not_initialized(self):
        """from_code returns correct subclass."""
        err = SdsError.from_code(ErrorCode.NOT_INITIALIZED)
        assert isinstance(err, SdsNotInitializedError)
        assert err.code == ErrorCode.NOT_INITIALIZED
    
    def test_sds_error_from_code_already_initialized(self):
        """from_code returns AlreadyInitializedError."""
        err = SdsError.from_code(ErrorCode.ALREADY_INITIALIZED)
        assert isinstance(err, SdsAlreadyInitializedError)
    
    def test_sds_error_from_code_invalid_config(self):
        """from_code returns ConfigError."""
        err = SdsError.from_code(ErrorCode.INVALID_CONFIG)
        assert isinstance(err, SdsConfigError)
    
    def test_sds_error_from_code_mqtt_connect_failed(self):
        """from_code returns MqttError."""
        err = SdsError.from_code(ErrorCode.MQTT_CONNECT_FAILED)
        assert isinstance(err, SdsMqttError)
    
    def test_sds_error_from_code_mqtt_disconnected(self):
        """from_code returns MqttError."""
        err = SdsError.from_code(ErrorCode.MQTT_DISCONNECTED)
        assert isinstance(err, SdsMqttError)
    
    def test_sds_error_from_code_table_not_found(self):
        """from_code returns TableError."""
        err = SdsError.from_code(ErrorCode.TABLE_NOT_FOUND)
        assert isinstance(err, SdsTableError)
    
    def test_sds_error_from_code_table_already_registered(self):
        """from_code returns TableError."""
        err = SdsError.from_code(ErrorCode.TABLE_ALREADY_REGISTERED)
        assert isinstance(err, SdsTableError)
    
    def test_sds_error_from_code_max_tables(self):
        """from_code returns TableError."""
        err = SdsError.from_code(ErrorCode.MAX_TABLES_REACHED)
        assert isinstance(err, SdsTableError)
    
    def test_sds_error_from_code_invalid_table(self):
        """from_code returns TableError."""
        err = SdsError.from_code(ErrorCode.INVALID_TABLE)
        assert isinstance(err, SdsTableError)
    
    def test_sds_error_from_code_invalid_role(self):
        """from_code returns TableError."""
        err = SdsError.from_code(ErrorCode.INVALID_ROLE)
        assert isinstance(err, SdsTableError)
    
    def test_sds_error_from_code_owner_exists(self):
        """from_code returns TableError."""
        err = SdsError.from_code(ErrorCode.OWNER_EXISTS)
        assert isinstance(err, SdsTableError)
    
    def test_sds_error_from_code_max_nodes(self):
        """from_code returns CapacityError."""
        err = SdsError.from_code(ErrorCode.MAX_NODES_REACHED)
        assert isinstance(err, SdsCapacityError)
    
    def test_sds_error_from_code_buffer_full(self):
        """from_code returns CapacityError."""
        err = SdsError.from_code(ErrorCode.BUFFER_FULL)
        assert isinstance(err, SdsCapacityError)
    
    def test_sds_error_from_code_section_too_large(self):
        """from_code returns CapacityError."""
        err = SdsError.from_code(ErrorCode.SECTION_TOO_LARGE)
        assert isinstance(err, SdsCapacityError)
    
    def test_sds_error_from_code_platform_not_set(self):
        """from_code returns PlatformError."""
        err = SdsError.from_code(ErrorCode.PLATFORM_NOT_SET)
        assert isinstance(err, SdsPlatformError)
    
    def test_sds_error_from_code_platform_error(self):
        """from_code returns PlatformError."""
        err = SdsError.from_code(ErrorCode.PLATFORM_ERROR)
        assert isinstance(err, SdsPlatformError)
    
    def test_exception_inheritance(self):
        """All exceptions inherit from SdsError."""
        assert issubclass(SdsNotInitializedError, SdsError)
        assert issubclass(SdsAlreadyInitializedError, SdsError)
        assert issubclass(SdsConfigError, SdsError)
        assert issubclass(SdsMqttError, SdsError)
        assert issubclass(SdsTableError, SdsError)
        assert issubclass(SdsCapacityError, SdsError)
        assert issubclass(SdsPlatformError, SdsError)
    
    def test_exception_is_exception(self):
        """All exceptions inherit from Exception."""
        assert issubclass(SdsError, Exception)


class TestCheckError:
    """Tests for the check_error function."""
    
    def test_check_error_ok(self):
        """check_error does nothing for OK."""
        check_error(ErrorCode.OK)  # Should not raise
    
    def test_check_error_not_initialized(self):
        """check_error raises for NOT_INITIALIZED."""
        with pytest.raises(SdsNotInitializedError):
            check_error(ErrorCode.NOT_INITIALIZED)
    
    def test_check_error_already_initialized(self):
        """check_error raises for ALREADY_INITIALIZED."""
        with pytest.raises(SdsAlreadyInitializedError):
            check_error(ErrorCode.ALREADY_INITIALIZED)
    
    def test_check_error_mqtt_connect_failed(self):
        """check_error raises for MQTT_CONNECT_FAILED."""
        with pytest.raises(SdsMqttError):
            check_error(ErrorCode.MQTT_CONNECT_FAILED)
    
    def test_check_error_table_not_found(self):
        """check_error raises for TABLE_NOT_FOUND."""
        with pytest.raises(SdsTableError):
            check_error(ErrorCode.TABLE_NOT_FOUND)
    
    def test_check_error_preserves_code(self):
        """check_error preserves the error code."""
        try:
            check_error(ErrorCode.NOT_INITIALIZED)
        except SdsError as e:
            assert e.code == ErrorCode.NOT_INITIALIZED
