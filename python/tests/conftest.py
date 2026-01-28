"""
pytest configuration and fixtures for SDS tests.
"""
import os
import pytest
from typing import Generator

# Check if CFFI extension is available
CFFI_AVAILABLE = False
try:
    from sds._bindings import lib, ffi
    CFFI_AVAILABLE = True
except ImportError:
    pass


def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line(
        "markers", "requires_cffi: mark test as requiring CFFI extension"
    )
    config.addinivalue_line(
        "markers", "requires_mqtt: mark test as requiring MQTT broker"
    )


def pytest_collection_modifyitems(config, items):
    """Skip tests based on available features."""
    skip_cffi = pytest.mark.skip(reason="CFFI extension not built")
    skip_mqtt = pytest.mark.skip(reason="MQTT broker not available")
    
    # Check if MQTT is available (try to connect)
    mqtt_available = os.environ.get("SDS_TEST_MQTT", "0") == "1"
    
    for item in items:
        if "requires_cffi" in item.keywords and not CFFI_AVAILABLE:
            item.add_marker(skip_cffi)
        if "requires_mqtt" in item.keywords and not mqtt_available:
            item.add_marker(skip_mqtt)


@pytest.fixture
def mqtt_broker_host() -> str:
    """Get MQTT broker hostname from environment or default."""
    return os.environ.get("SDS_MQTT_HOST", "localhost")


@pytest.fixture
def mqtt_broker_port() -> int:
    """Get MQTT broker port from environment or default."""
    return int(os.environ.get("SDS_MQTT_PORT", "1883"))


@pytest.fixture
def unique_node_id(request) -> str:
    """Generate a unique node ID for each test (max 31 chars)."""
    # Create a short hash of the test name to keep within 31 char limit
    import hashlib
    test_hash = hashlib.md5(request.node.name.encode()).hexdigest()[:8]
    return f"py_{test_hash}_{os.getpid() % 10000}"
