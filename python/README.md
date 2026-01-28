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
- **Cross-Platform**: Linux (x86_64, ARM64) and macOS
- **Type Hints**: Full type annotations for IDE support

## Requirements

- Python 3.8+
- MQTT broker (e.g., Mosquitto)
- libpaho-mqtt development headers (for building from source)

## API Reference

### SdsNode

The main class for interacting with SDS.

```python
class SdsNode:
    def __init__(self, node_id: str, broker_host: str, port: int = 1883,
                 username: str | None = None, password: str | None = None):
        """Initialize SDS node and connect to MQTT broker."""
    
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
