"""
Concurrency tests for SDS Python library.

Tests thread safety of SdsNode and SdsTable.
"""
import pytest
import threading
import time
from unittest.mock import MagicMock, patch


class TestThreadSafety:
    """Tests for thread safety."""
    
    def test_sds_node_has_lock(self):
        """Verify SdsNode has a threading lock."""
        from sds.node import SdsNode
        
        # Create node without auto_init to avoid MQTT connection
        with patch.object(SdsNode, 'init'):
            node = SdsNode("test_node", "localhost", auto_init=False)
            assert hasattr(node, '_lock')
            assert isinstance(node._lock, type(threading.RLock()))
    
    def test_section_proxy_accepts_lock(self):
        """Verify SectionProxy can be created with a lock."""
        from sds.table import SectionProxy
        from sds.tables import TableSectionInfo, TableFieldInfo, FieldType
        from sds._bindings import ffi
        
        # Create a mock section info
        section_info = TableSectionInfo(
            total_size=4,
            fields=[
                TableFieldInfo(
                    name="value",
                    field_type=FieldType.INT32,
                    offset=0,
                    size=4,
                )
            ]
        )
        
        # Create a CFFI buffer
        buffer = ffi.new("char[4]")
        
        lock = threading.RLock()
        
        # This should not raise
        proxy = SectionProxy(section_info, buffer, lock=lock)
        assert proxy._lock is lock
    
    def test_sds_table_accepts_lock(self):
        """Verify SdsTable stores lock parameter."""
        from sds.table import SdsTable
        from sds.types import Role
        from sds._bindings import ffi
        
        lock = threading.RLock()
        buffer = ffi.new("char[64]")
        
        # Create minimal table - meta=None and no schemas means no proxies setup
        table = SdsTable(
            table_type="TestTable",
            role=Role.DEVICE,
            buffer=buffer,
            meta=None,
            lock=lock,
        )
        
        assert table._lock is lock


class TestNodeValidation:
    """Tests for node_id validation."""
    
    def test_empty_node_id_raises(self):
        """Empty node_id should raise SdsValidationError."""
        from sds import SdsNode, SdsValidationError
        
        with pytest.raises(SdsValidationError, match="cannot be empty"):
            SdsNode("", "localhost", auto_init=False)
    
    def test_long_node_id_raises(self):
        """Node ID exceeding max length should raise."""
        from sds import SdsNode, SdsValidationError
        from sds.node import MAX_NODE_ID_LEN
        
        long_id = "a" * (MAX_NODE_ID_LEN + 1)
        with pytest.raises(SdsValidationError, match="exceeds maximum length"):
            SdsNode(long_id, "localhost", auto_init=False)
    
    def test_invalid_chars_in_node_id_raises(self):
        """Invalid characters in node_id should raise."""
        from sds import SdsNode, SdsValidationError
        
        with pytest.raises(SdsValidationError, match="invalid characters"):
            SdsNode("test node", "localhost", auto_init=False)  # space
        
        with pytest.raises(SdsValidationError, match="invalid characters"):
            SdsNode("test.node", "localhost", auto_init=False)  # dot
    
    def test_valid_node_id_accepted(self):
        """Valid node_id values should be accepted."""
        from sds import SdsNode
        
        # These should not raise
        with patch.object(SdsNode, 'init'):
            SdsNode("test_node", "localhost", auto_init=False)
            SdsNode("test-node", "localhost", auto_init=False)
            SdsNode("TestNode123", "localhost", auto_init=False)
            SdsNode("a" * 31, "localhost", auto_init=False)  # max length


class TestLogging:
    """Tests for logging configuration."""
    
    def test_configure_logging_export(self):
        """configure_logging should be exported from sds package."""
        from sds import configure_logging
        assert callable(configure_logging)
    
    def test_logger_exists(self):
        """Logger should exist for sds package."""
        import logging
        logger = logging.getLogger("sds")
        assert logger is not None
    
    def test_node_module_has_logger(self):
        """node.py module should have a logger."""
        from sds import node
        assert hasattr(node, 'logger')


class TestConnectionRetry:
    """Tests for connection retry logic."""
    
    def test_retry_parameters_stored(self):
        """Retry parameters should be stored on the node."""
        from sds import SdsNode
        
        with patch.object(SdsNode, 'init'):
            node = SdsNode(
                "test_node",
                "localhost",
                auto_init=False,
                connect_timeout_ms=10000,
                retry_count=5,
                retry_delay_ms=2000,
            )
            
            assert node._connect_timeout_ms == 10000
            assert node._retry_count == 5
            assert node._retry_delay_ms == 2000
    
    def test_default_retry_parameters(self):
        """Default retry parameters should have sensible values."""
        from sds import SdsNode
        
        with patch.object(SdsNode, 'init'):
            node = SdsNode("test_node", "localhost", auto_init=False)
            
            assert node._connect_timeout_ms == 5000
            assert node._retry_count == 3
            assert node._retry_delay_ms == 1000
