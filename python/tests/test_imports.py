"""
Tests for package imports.

These tests verify the public API is correctly exported.
"""
import pytest


class TestPackageImports:
    """Tests for importing the sds package."""
    
    def test_import_package(self):
        """Can import the sds package."""
        import sds
        assert hasattr(sds, "__version__")
    
    def test_version(self):
        """Package has version string."""
        from sds import __version__
        assert isinstance(__version__, str)
        assert len(__version__) > 0
    
    def test_import_role(self):
        """Can import Role enum."""
        from sds import Role
        assert hasattr(Role, "OWNER")
        assert hasattr(Role, "DEVICE")
    
    def test_import_error_code(self):
        """Can import ErrorCode enum."""
        from sds import ErrorCode
        assert hasattr(ErrorCode, "OK")
        assert hasattr(ErrorCode, "NOT_INITIALIZED")
    
    def test_import_exceptions(self):
        """Can import all exception classes."""
        from sds import (
            SdsError,
            SdsNotInitializedError,
            SdsAlreadyInitializedError,
            SdsConfigError,
            SdsMqttError,
            SdsTableError,
            SdsCapacityError,
            SdsPlatformError,
        )
        assert issubclass(SdsNotInitializedError, SdsError)
    
    def test_import_check_error(self):
        """Can import check_error function."""
        from sds import check_error
        assert callable(check_error)
    
    def test_import_sds_node(self):
        """Can import SdsNode class."""
        from sds import SdsNode
        assert callable(SdsNode)
    
    def test_all_exports(self):
        """__all__ contains expected exports."""
        import sds
        expected = [
            "__version__",
            "SdsNode",
            "Role",
            "ErrorCode",
            "SdsError",
            "SdsNotInitializedError",
            "SdsAlreadyInitializedError",
            "SdsConfigError",
            "SdsMqttError",
            "SdsTableError",
            "SdsCapacityError",
            "SdsPlatformError",
            "check_error",
        ]
        for name in expected:
            assert name in sds.__all__, f"{name} not in __all__"


class TestTypesImports:
    """Tests for importing from sds.types."""
    
    def test_import_types_module(self):
        """Can import sds.types module."""
        from sds import types
        assert hasattr(types, "Role")
        assert hasattr(types, "ErrorCode")
    
    def test_import_types_directly(self):
        """Can import types directly."""
        from sds.types import Role, ErrorCode
        assert Role.OWNER == 0
        assert ErrorCode.OK == 0
