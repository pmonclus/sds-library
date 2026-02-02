# SDS Library - Python Bindings

Python bindings for the SDS (Synchronized Data Structures) library, providing
a Pythonic interface for IoT state synchronization over MQTT.

## Installation

```bash
pip install sds-library
```

For development installation:

```bash
cd python
pip install -e ".[dev]"
```

## Quick Start

### Generate Types from Schema

The code generator creates Python types from your `.sds` schema file:

```bash
# Generate Python types (and optionally C types)
python tools/sds_codegen.py schema.sds --python -o python/
```

This creates `sds_types.py` with dataclasses matching your C structs.

### Device Node (Using Generated Types)

```python
from sds import SdsNode, Role
from sds_types import SensorData  # Generated from schema.sds

with SdsNode("py_sensor_01", "localhost") as node:
    # Register with schema bundle - no manual dataclass needed!
    table = node.register_table("SensorData", Role.DEVICE, schema=SensorData)
    
    @node.on_config("SensorData")
    def handle_config(table_type):
        print(f"Config: threshold={table.config.threshold}")
    
    while True:
        # C-like attribute access
        table.state.temperature = 23.5
        table.state.humidity = 65.0
        table.status.error_code = 0
        
        node.poll(timeout_ms=1000)
```

### Device Node (Manual Schema Definition)

If you prefer not to use the code generator:

```python
from dataclasses import dataclass
from sds import SdsNode, Role, Field

@dataclass
class SensorState:
    temperature: float = Field(float32=True)
    humidity: float = Field(float32=True)

with SdsNode("py_sensor_01", "localhost") as node:
    table = node.register_table(
        "SensorData", Role.DEVICE,
        state_schema=SensorState,
    )
    table.state.temperature = 23.5
```

### Owner Node (C-like Syntax)

```python
from sds import SdsNode, Role

with SdsNode("py_owner_01", "localhost") as node:
    table = node.register_table(
        "SensorData",
        Role.OWNER,
        status_schema=SensorStatus,  # For reading device status
    )
    
    # Set config
    table.config.threshold = 25.0
    
    @node.on_status("SensorData")
    def handle_status(table_type, from_node):
        device = table.get_device(from_node)
        if device and device.online:
            print(f"{from_node}: battery={device.status.battery_percent}%")
    
    while True:
        node.poll(timeout_ms=1000)
        
        # Iterate all devices
        for node_id, device in table.iter_devices():
            print(f"{node_id}: online={device.online}")
```

## Features

- **C-like Syntax**: Access table data directly (`table.state.temperature = 23.5`)
- **Zero Protocol Drift**: Python uses the exact same C implementation
- **Pythonic API**: Context managers, decorators, and keyword arguments
- **Thread-Safe**: All operations protected by locks for multi-threaded use
- **Cross-Platform**: Linux (x86_64, ARM64) and macOS
- **Type Hints**: Full type annotations for IDE support
- **Device Eviction**: Automatic cleanup of offline device slots

## Device Eviction

When devices disconnect unexpectedly, SDS receives MQTT LWT (Last Will and Testament) 
messages and can automatically evict them from status slots after a grace period.
This prevents slots from being permanently consumed by devices that never reconnect.

```python
# Enable eviction with 60-second grace period
with SdsNode("owner", "localhost", eviction_grace_ms=60000) as node:
    table = node.register_table("SensorData", Role.OWNER, schema=SensorData)
    
    @node.on_device_evicted()
    def handle_eviction(table_type: str, node_id: str):
        print(f"Device {node_id} was evicted from {table_type}")
    
    while True:
        node.poll(timeout_ms=1000)
        
        # Check if a device has eviction pending
        device = table.get_device("sensor_01")
        if device and device.eviction_pending:
            print("Device will be evicted soon if it doesn't reconnect")
```

## Thread Safety

SdsNode is fully thread-safe. Multiple threads can safely:
- Call `poll()` concurrently
- Access table data (`table.state.temperature`)
- Register tables and callbacks

```python
import threading
from sds import SdsNode, Role

with SdsNode("my_node", "localhost") as node:
    table = node.register_table("SensorData", Role.DEVICE, schema=SensorData)
    
    def polling_thread():
        while True:
            node.poll(timeout_ms=100)
    
    def update_thread():
        while True:
            table.state.temperature = read_sensor()
            time.sleep(1)
    
    # Both threads can safely access the node and table
    t1 = threading.Thread(target=polling_thread)
    t2 = threading.Thread(target=update_thread)
    t1.start()
    t2.start()
```

**Note:** Callbacks are executed while holding the lock. Avoid blocking
operations in callbacks to prevent deadlocks.

## Logging Configuration

SDS uses Python's standard `logging` module. By default, log messages go
to a `NullHandler` (silent). To enable logging:

```python
from sds import configure_logging
import logging

# Quick setup - logs to stderr at INFO level
configure_logging(level=logging.INFO)

# Or configure manually
logging.getLogger("sds").setLevel(logging.DEBUG)
logging.getLogger("sds").addHandler(logging.StreamHandler())
```

## Connection Resilience

SdsNode includes built-in connection retry with exponential backoff:

```python
from sds import SdsNode

node = SdsNode(
    "my_node",
    "mqtt.example.com",
    retry_count=5,           # Try 5 times (default: 3)
    retry_delay_ms=2000,     # Start with 2 second delay (default: 1000)
    connect_timeout_ms=10000 # 10 second timeout (default: 5000)
)
```

On connection failure, retries are attempted with exponential backoff
(delay doubles each retry). Non-connection errors are not retried.

## Error Handling Best Practices

```python
from sds import (
    SdsNode, SdsMqttError, SdsTableError, 
    SdsValidationError, SdsError
)

try:
    with SdsNode("my_node", "localhost") as node:
        table = node.register_table("SensorData", Role.DEVICE)
        
        while True:
            try:
                node.poll()
            except SdsMqttError:
                # MQTT connection lost - will auto-reconnect
                logging.warning("MQTT disconnected, waiting...")
                time.sleep(1)
                
except SdsValidationError as e:
    # Invalid node_id or configuration
    logging.error(f"Configuration error: {e}")
except SdsMqttError as e:
    # Failed to connect after all retries
    logging.error(f"Could not connect to MQTT: {e}")
except SdsError as e:
    # Other SDS errors
    logging.error(f"SDS error: {e}")
```

## Requirements

- Python 3.8+
- MQTT broker (e.g., Mosquitto)
- libpaho-mqtt development headers (for building from source)

## Configuration Options

### Delta Sync (v0.5.0+)

Enable delta sync to only send changed fields, reducing bandwidth:

```python
with SdsNode(
    "sensor_01", 
    "localhost",
    enable_delta_sync=True,           # Only send changed fields
    delta_float_tolerance=0.01        # Ignore tiny float changes
) as node:
    # ...
```

**Benefits:**
- Reduced bandwidth (only changed fields transmitted)
- Lower power consumption on battery devices
- Works automatically with codegen-generated tables

**Limitations:**
- Config messages are always full (retained on broker)
- Status heartbeats are always full (liveness detection)
- Manual schema definitions use full sync

### Eviction Grace Period

Configure how long to wait before evicting offline devices:

```python
with SdsNode(
    "owner", 
    "localhost",
    eviction_grace_ms=30000  # 30 seconds before eviction
) as node:
    @node.on_device_evicted()
    def handle_eviction(table_type, node_id):
        print(f"Device {node_id} evicted from {table_type}")
```

### Raw MQTT Publish/Subscribe (v0.5.0+)

Send and receive custom MQTT messages through the SDS-managed connection. Useful for
logging, diagnostics, or application-specific messages that don't fit the table model.

**Publishing:**
```python
with SdsNode("sensor_01", "localhost") as node:
    # Check connection status
    if node.is_connected():
        # Publish a log message
        node.publish_raw(
            f"log/{node.node_id}",
            '{"level": "info", "msg": "Sensor started"}',
            qos=0,
            retained=False
        )
        
        # Publish binary data
        node.publish_raw("sensor/raw_data", b'\x00\x01\x02\x03')
```

**Subscribing:**
```python
def on_log(topic: str, payload: bytes):
    print(f"Log from {topic}: {payload.decode()}")

with SdsNode("controller", "localhost") as node:
    # Subscribe to all logs (+ matches any single level)
    node.subscribe_raw("log/+", on_log)
    
    # Or use # for multi-level wildcard
    node.subscribe_raw("sensors/#", on_sensor_data)
    
    # Main loop
    while True:
        node.poll()
        time.sleep(0.1)
    
    # Unsubscribe when done
    node.unsubscribe_raw("log/+")
```

**Methods:**

| Method | Description |
|--------|-------------|
| `is_connected()` | Returns `True` if connected to MQTT broker |
| `publish_raw(topic, payload, qos=0, retained=False)` | Publish arbitrary MQTT message |
| `subscribe_raw(topic, callback, qos=0)` | Subscribe to topic pattern with callback |
| `unsubscribe_raw(topic)` | Unsubscribe from a topic pattern |

**Notes:**
- Topics starting with `sds/` are reserved and will raise `ValueError`
- `payload` for publish can be `str` (UTF-8 encoded) or `bytes`
- Callback receives `(topic: str, payload: bytes)`
- Maximum 8 concurrent raw subscriptions
- Wildcard subscriptions (`log/+`) count as 1 subscription regardless of matching topics

## API Reference

### SdsNode

The main class for interacting with SDS.

```python
class SdsNode:
    def __init__(self, node_id: str, broker_host: str, port: int = 1883,
                 username: str | None = None, password: str | None = None,
                 eviction_grace_ms: int = 0):
        """
        Initialize SDS node and connect to MQTT broker.
        
        Args:
            eviction_grace_ms: Grace period before evicting offline devices (0 = disabled).
                              For OWNER roles, when a device disconnects (LWT), an eviction
                              timer starts. If the device doesn't reconnect within this period,
                              it's evicted from status slots, freeing the slot for new devices.
        """
    
    def register_table(self, table_type: str, role: Role,
                       sync_interval_ms: int | None = None,
                       config_schema: Type | None = None,
                       state_schema: Type | None = None,
                       status_schema: Type | None = None) -> SdsTable:
        """Register a table and return SdsTable for C-like access."""
    
    def get_table(self, table_type: str) -> SdsTable:
        """Get a previously registered table."""
    
    def unregister_table(self, table_type: str) -> None:
        """Unregister a table."""
    
    def poll(self, timeout_ms: int = 0) -> None:
        """Process MQTT messages and sync changes."""
    
    def is_ready(self) -> bool:
        """Check if connected to MQTT broker."""
    
    def on_error(self, callback) -> None:
        """Register error callback."""
    
    def on_version_mismatch(self, callback) -> None:
        """Register version mismatch callback."""
```

### SdsTable

Provides C-like attribute access to table sections.

```python
class SdsTable:
    table_type: str          # The table type name
    role: Role               # OWNER or DEVICE
    
    # Device role properties
    state: SectionProxy      # Read/write state section
    status: SectionProxy     # Read/write status section
    config: SectionProxy     # Read-only config (from owner)
    
    # Owner role properties
    config: SectionProxy     # Read/write config section
    device_count: int        # Number of known devices
    
    def get_device(self, node_id: str) -> DeviceView | None:
        """Get a device's data (owner role only)."""
    
    def iter_devices(self) -> Iterator[tuple[str, DeviceView]]:
        """Iterate over all known devices (owner role only)."""
```

### Role Enum

```python
class Role(Enum):
    OWNER = 0   # Publishes config, receives state/status
    DEVICE = 1  # Receives config, publishes state/status
```

### LogLevel Enum

```python
class LogLevel(Enum):
    NONE = 0    # Disable all logging
    ERROR = 1   # Error conditions only
    WARN = 2    # Warnings and errors
    INFO = 3    # Informational messages
    DEBUG = 4   # All messages

# Functions
set_log_level(level: LogLevel) -> None
get_log_level() -> LogLevel
```

### Field Helper

Define field types for schema dataclasses:

```python
from sds import Field, FieldType

@dataclass
class MyConfig:
    command: int = Field(uint8=True)
    value: float = Field(float32=True)
    name: str = Field(string_len=32)
```

### SectionProxy Convenience Methods

```python
# Convert section to dictionary
data = table.state.to_dict()
# {'temperature': 23.5, 'humidity': 65.0}

# Set multiple fields from dictionary
table.state.from_dict({'temperature': 24.0, 'humidity': 60.0})
```

### Exceptions

```python
class SdsError(Exception):
    """Base exception for SDS errors."""
    
class SdsNotInitializedError(SdsError):
    """Raised when SDS is not initialized."""
    
class SdsMqttError(SdsError):
    """Raised on MQTT connection errors."""
    
class SdsTableError(SdsError):
    """Raised on table registration errors."""

class SdsValidationError(SdsError):
    """Raised when input validation fails (e.g., invalid node_id)."""
```

## Building from Source

### Prerequisites

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install libpaho-mqtt-dev python3-dev
```

**Linux (RHEL/CentOS):**
```bash
sudo yum install paho-c-devel python3-devel
```

**macOS:**
```bash
brew install libpaho-mqtt
```

### Build

```bash
cd python
pip install build
python -m build
```

## Testing

```bash
cd python
pip install -e ".[dev]"
pytest
```

For integration tests (requires MQTT broker):
```bash
pytest -m requires_mqtt
```

## License

MIT License - see LICENSE file for details.
