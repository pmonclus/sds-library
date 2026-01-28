# Python Wrapper Implementation Plan

## Status: ✅ FULL FEATURE PARITY

### Implementation Summary

The Python wrapper has achieved full feature parity with the C library:

**Files Created/Modified:**
```
python/
├── pyproject.toml           # Build configuration
├── setup.py                 # Editable installs
├── MANIFEST.in              # Source distribution
├── README.md                # Documentation (updated for C-like syntax)
├── sds/
│   ├── __init__.py          # Public API exports (updated)
│   ├── _bindings.py         # Low-level CFFI bindings
│   ├── _build_ffi.py        # CFFI compilation (includes sds_platform.h)
│   ├── _cdefs.h             # C declarations (added SdsLogLevel)
│   ├── node.py              # SdsNode class with all callbacks
│   ├── table.py             # NEW: SdsTable, SectionProxy, DeviceView
│   ├── tables.py            # Dataclass-based table helpers
│   └── types.py             # Role, LogLevel enums, exceptions
└── tests/
    ├── conftest.py          # pytest fixtures
    ├── test_imports.py      # 10 tests
    ├── test_node.py         # 36 tests (was 19)
    ├── test_table.py        # NEW: 17 tests for C-like syntax
    └── test_types.py        # 34 tests

examples/python/
├── README.md
├── simple_device.py         # Updated with C-like syntax
└── simple_owner.py          # Updated with C-like syntax

.github/workflows/
└── python-wheels.yml        # CI/CD for wheel building
```

**Test Results:** 87 tests (58 passed, 29 skipped without MQTT broker)

**Features (Full Parity):**
- **C-like attribute access**: `table.state.temperature = 23.5`
- CFFI bindings to all core C library functions
- `SdsNode` class with context manager support
- `SdsTable` class for direct table section access
- Decorator-based callbacks (`@node.on_config`, `@node.on_state`, `@node.on_status`)
- Error callback (`@node.on_error`)
- Version mismatch callback (`@node.on_version_mismatch`)
- Log level control (`set_log_level()`, `get_log_level()`)
- Owner device iteration (`table.iter_devices()`, `table.get_device()`)
- Full exception hierarchy matching C error codes
- Dataclass-based table field helpers
- Cross-platform build support (macOS/Linux)
- GitHub Actions CI/CD workflow

### Phase 5: Full Feature Parity (COMPLETED)

| Step | Description | Status |
|------|-------------|--------|
| 5.1 | C-like table access (`table.state.temperature = x`) | ✅ Done |
| 5.2 | Owner data access (`get_device()`, `iter_devices()`) | ✅ Done |
| 5.3 | Error callback (`@node.on_error`) | ✅ Done |
| 5.4 | Version mismatch callback | ✅ Done |
| 5.5 | Log level control (`LogLevel`, `set_log_level`) | ✅ Done |
| 5.6 | Tests for new features (24 new tests) | ✅ Done |
| 5.7 | Updated examples with C-like syntax | ✅ Done |

---

## Overview

This document outlines the plan to create a Python wrapper for the SDS C library using CFFI. The wrapper will provide Python applications with native access to SDS functionality while maintaining a single source of truth (the C implementation).

## Goals

| Goal | Description |
|------|-------------|
| **Zero protocol drift** | Python uses the exact same logic as C |
| **Pythonic API** | Feel natural to Python developers |
| **Cross-platform** | Support Linux (ARM/x86_64) and macOS |
| **Minimal maintenance** | Thin wrapper, not reimplementation |
| **Easy installation** | `pip install sds-library` |

## Why Wrap (Not Reimplement)

A pure Python reimplementation was considered but rejected due to:

1. **Maintenance burden** - Two implementations require duplicate bug fixes
2. **Protocol drift risk** - Subtle behavioral differences over time
3. **Test duplication** - Need 177+ tests in Python too
4. **Edge cases** - JSON escaping, integer overflow, etc. already solved in C

Wrapping provides:
- Single source of truth (C is the spec)
- Bug fixes apply once
- Inherited test coverage
- Guaranteed behavioral parity

## Architecture

```
┌─────────────────────────────────────────┐
│           Python Application            │
├─────────────────────────────────────────┤
│         sds (Python package)            │
│  ┌─────────────┐  ┌──────────────────┐  │
│  │   node.py   │  │   tables.py      │  │
│  │  (Pythonic  │  │  (dataclasses    │  │
│  │    API)     │  │   for tables)    │  │
│  └──────┬──────┘  └────────┬─────────┘  │
│         │                  │            │
│  ┌──────▼──────────────────▼─────────┐  │
│  │       _bindings.py (CFFI)         │  │
│  └──────────────┬────────────────────┘  │
├─────────────────┼───────────────────────┤
│                 │                       │
│         ┌───────▼───────┐               │
│         │   libsds.so   │  (C library)  │
│         └───────────────┘               │
└─────────────────────────────────────────┘
```

## Directory Structure

```
sds-library/
├── python/
│   ├── pyproject.toml           # Build configuration
│   ├── setup.py                 # For editable installs
│   ├── MANIFEST.in              # Include C sources
│   ├── README.md                # Python-specific docs
│   │
│   ├── sds/
│   │   ├── __init__.py          # Public API exports
│   │   ├── _bindings.py         # CFFI bindings to libsds
│   │   ├── _build_ffi.py        # CFFI compilation script
│   │   ├── node.py              # SdsNode class
│   │   ├── types.py             # Role enum, exceptions
│   │   ├── tables.py            # Table dataclass helpers
│   │   └── _cdefs.h             # C declarations for CFFI
│   │
│   └── tests/
│       ├── conftest.py          # pytest fixtures
│       ├── test_init.py         # Initialization tests
│       ├── test_tables.py       # Table registration tests
│       ├── test_publish.py      # State/status publishing
│       ├── test_subscribe.py    # Config/state receiving
│       ├── test_errors.py       # Error handling
│       └── test_integration.py  # Full workflow with MQTT
│
├── src/                         # (existing C source)
├── include/                     # (existing C headers)
└── ...
```

---

## Implementation Phases

### Phase 1: Foundation (Core Bindings)

**Objective:** Get basic C library calls working from Python

#### Step 1.1: Project Setup

| Task | Status |
|------|--------|
| Create `python/` directory structure | ✅ Done |
| Create `pyproject.toml` with CFFI build dependencies | ✅ Done |
| Create `_cdefs.h` with C function declarations | ✅ Done |
| Set up CFFI ABI-level binding (dlopen approach first) | ✅ Done (API mode) |

**Deliverables:**
- Working `pip install -e ./python` in development
- Can call `sds_init()` from Python

#### Step 1.2: Core Function Bindings

| Task | Status |
|------|--------|
| Bind `sds_init()` and `sds_cleanup()` | ✅ Done |
| Bind `sds_register_table()` | ✅ Done |
| Bind `sds_publish_state()` and `sds_publish_status()` | ✅ Done |
| Bind `sds_poll()` | ✅ Done |
| Bind error functions (`sds_error_string()`, etc.) | ✅ Done |

**C Functions to Expose:**

```c
// Lifecycle
int sds_init(SdsContext** ctx, const char* node_id, 
             const char* broker_host, int broker_port);
void sds_cleanup(SdsContext* ctx);

// Table management
int sds_register_table(SdsContext* ctx, const char* table_type,
                       SdsRole role, const SdsTableMeta* meta);

// Publishing
int sds_publish_state(SdsContext* ctx, const char* table_type,
                      const void* state);
int sds_publish_status(SdsContext* ctx, const char* table_type,
                       const void* status);
int sds_publish_config(SdsContext* ctx, const char* table_type,
                       const char* target_node, const void* config);

// Event loop
int sds_poll(SdsContext* ctx, int timeout_ms);

// Queries
bool sds_is_device_online(SdsContext* ctx, const char* table_type,
                          const char* node_id);
int sds_foreach_node(SdsContext* ctx, const char* table_type,
                     SdsNodeCallback cb, void* user_data);

// Utilities
const char* sds_error_string(int error_code);
```

#### Step 1.3: Memory Management

| Task | Status |
|------|--------|
| Handle `SdsContext` lifecycle (prevent double-free) | ✅ Done |
| Manage string encoding (Python str → C char*) | ✅ Done |
| Handle struct allocation for table data | ✅ Done |

**Actual tests:** 50 passed, 13 skipped (need MQTT broker)

---

### Phase 2: Pythonic API Layer

**Objective:** Make the wrapper feel natural to Python developers

#### Step 2.1: SdsNode Class

| Task | Status |
|------|--------|
| Create context manager support (`with SdsNode(...) as node:`) | ✅ Done |
| Implement `__enter__` / `__exit__` for cleanup | ✅ Done |
| Add property accessors for node info | ✅ Done |
| Implement `register_table()` with enum for role | ✅ Done |

**Target API:**

```python
from sds import SdsNode, Role

with SdsNode("sensor_01", "localhost") as node:
    node.register_table("SensorData", Role.DEVICE)
    
    while True:
        node.publish_state("SensorData", temperature=23.5, humidity=65.0)
        node.poll(timeout_ms=1000)
```

#### Step 2.2: Table Type Helpers

| Task | Status |
|------|--------|
| Create dataclass-based table definitions | ✅ Done |
| Auto-generate `SdsTableMeta` from Python class | ⬜ Future |
| Support schema versioning | ⬜ Future |
| Validate field types at registration | ✅ Done |

**Target API:**

```python
from sds import Table, Field

@Table(sync_interval_ms=1000)
class SensorData:
    class Config:
        command: int = Field(uint8=True)
        threshold: float = Field(float32=True)
    
    class State:
        temperature: float
        humidity: float
    
    class Status:
        error_code: int
        battery_percent: int
```

#### Step 2.3: Callback System

| Task | Status |
|------|--------|
| Implement config received callback | ✅ Done |
| Implement state received callback (for owners) | ✅ Done |
| Implement status received callback (for owners) | ✅ Done |
| Handle Python exceptions in callbacks safely | ✅ Done |

**Target API:**

```python
@node.on_config("SensorData")
def handle_config(config: SensorDataConfig):
    print(f"Received config: threshold={config.threshold}")

@node.on_state("SensorData") 
def handle_state(node_id: str, state: SensorDataState):
    print(f"Device {node_id}: temp={state.temperature}")
```

**Estimated tests:** 25-30

---

### Phase 3: Build & Distribution

**Objective:** Make installation simple across platforms

#### Step 3.1: CFFI API-Level Build

| Task | Status |
|------|--------|
| Switch from ABI (dlopen) to API (compiled) mode | ✅ Done |
| Compile C extension during wheel build | ✅ Done |
| Include all C sources in sdist | ✅ Done |
| Test build on Linux x86_64 | ⬜ CI |

#### Step 3.2: Cross-Platform Wheels

| Task | Status |
|------|--------|
| Set up cibuildwheel configuration | ✅ Done |
| Build wheel: Linux x86_64 (manylinux) | ⬜ CI |
| Build wheel: Linux aarch64 (manylinux) | ⬜ CI |
| Build wheel: macOS x86_64 | ⬜ CI |
| Build wheel: macOS arm64 (Apple Silicon) | ✅ Done (local) |
| Test wheel installation on each platform | ⬜ CI |

**pyproject.toml:**

```toml
[build-system]
requires = ["setuptools>=45", "wheel", "cffi>=1.0.0"]
build-backend = "setuptools.build_meta"

[project]
name = "sds-library"
version = "1.0.0"
description = "Python bindings for the SDS (Synchronized Data Structures) library"
requires-python = ">=3.8"
dependencies = ["cffi>=1.0.0"]

[project.optional-dependencies]
dev = ["pytest>=7.0", "pytest-cov", "mypy"]

[tool.cibuildwheel]
build = ["cp38-*", "cp39-*", "cp310-*", "cp311-*", "cp312-*"]
skip = ["*-musllinux_*", "*-win32", "*-manylinux_i686"]

[tool.cibuildwheel.linux]
before-all = "yum install -y mosquitto-devel || apt-get install -y libmosquitto-dev"

[tool.cibuildwheel.macos]
before-all = "brew install mosquitto"
```

#### Step 3.3: CI/CD Integration

| Task | Status |
|------|--------|
| Add GitHub Actions workflow for wheel building | ✅ Done |
| Add workflow for PyPI publishing | ✅ Done |
| Add workflow for testing against MQTT broker | ✅ Done |

**GitHub Actions Workflow:**

```yaml
# .github/workflows/python-wheels.yml
name: Build Python Wheels

on:
  push:
    tags: ['v*']
  pull_request:
    paths: ['python/**', 'src/**', 'include/**']

jobs:
  build_wheels:
    name: Build wheels on ${{ matrix.os }}
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]

    steps:
      - uses: actions/checkout@v4
      
      - name: Build wheels
        uses: pypa/cibuildwheel@v2.16
        with:
          package-dir: python
        
      - uses: actions/upload-artifact@v4
        with:
          name: wheels-${{ matrix.os }}
          path: ./wheelhouse/*.whl

  test_wheels:
    needs: build_wheels
    runs-on: ubuntu-latest
    services:
      mosquitto:
        image: eclipse-mosquitto:2
        ports:
          - 1883:1883
    steps:
      - uses: actions/checkout@v4
      - uses: actions/download-artifact@v4
      - name: Install and test
        run: |
          pip install wheels-ubuntu-latest/*.whl
          pytest python/tests/
```

**Estimated tests:** 5-10 (installation/import tests)

---

### Phase 4: Documentation & Examples

**Objective:** Make it easy for Python developers to adopt

#### Step 4.1: Documentation

| Task | Status |
|------|--------|
| Write `python/README.md` with quickstart | ✅ Done |
| Add docstrings to all public classes/methods | ✅ Done |
| Create API reference (Sphinx or mkdocs) | ⬜ Future |
| Add type hints for IDE support | ✅ Done |

#### Step 4.2: Examples

| Task | Status |
|------|--------|
| Create `examples/python/simple_device.py` | ✅ Done |
| Create `examples/python/simple_owner.py` | ✅ Done |
| Create `examples/python/custom_table.py` | ⬜ Future |
| Create `examples/python/mixed_fleet.py` (Python + C nodes) | ⬜ Future |

**Example: simple_device.py**

```python
#!/usr/bin/env python3
"""Simple SDS device example."""

import time
import random
from sds import SdsNode, Role

def main():
    with SdsNode("py_sensor_01", "localhost") as node:
        # Register as a device for SensorData table
        node.register_table("SensorData", Role.DEVICE)
        
        # Handle config updates from owner
        @node.on_config("SensorData")
        def on_config(config):
            print(f"Config received: command={config.command}")
        
        # Main loop
        while True:
            # Publish sensor readings
            node.publish_state("SensorData",
                temperature=20.0 + random.uniform(-5, 5),
                humidity=50.0 + random.uniform(-10, 10)
            )
            
            # Publish device status
            node.publish_status("SensorData",
                error_code=0,
                battery_percent=95,
                uptime_seconds=int(time.time())
            )
            
            # Process incoming messages
            node.poll(timeout_ms=1000)

if __name__ == "__main__":
    main()
```

**Example: simple_owner.py**

```python
#!/usr/bin/env python3
"""Simple SDS owner example."""

from sds import SdsNode, Role

def main():
    with SdsNode("py_owner_01", "localhost") as node:
        # Register as owner for SensorData table
        node.register_table("SensorData", Role.OWNER)
        
        # Handle state updates from devices
        @node.on_state("SensorData")
        def on_state(node_id: str, state):
            print(f"[{node_id}] temp={state.temperature:.1f}°C, "
                  f"humidity={state.humidity:.1f}%")
        
        # Handle status updates from devices
        @node.on_status("SensorData")
        def on_status(node_id: str, status):
            print(f"[{node_id}] battery={status.battery_percent}%, "
                  f"online={status.online}")
        
        # Main loop
        print("Owner running, waiting for device updates...")
        while True:
            node.poll(timeout_ms=1000)
            
            # Periodically check online devices
            for device in node.get_online_devices("SensorData"):
                print(f"  Device online: {device.node_id}")

if __name__ == "__main__":
    main()
```

**Estimated effort:** Documentation only, no additional tests

---

## Testing Strategy

### Unit Tests (~50 tests)

| Category | Tests | Description |
|----------|-------|-------------|
| Initialization | 8 | `sds_init`, `sds_cleanup`, error cases |
| Table Registration | 10 | Register, duplicate, invalid role |
| Publishing | 12 | State, status, config publishing |
| Callbacks | 10 | Config/state/status received |
| Error Handling | 5 | Error codes, exceptions |
| Memory Safety | 5 | No leaks, double-free protection |

### Integration Tests (~15 tests)

| Category | Tests | Description |
|----------|-------|-------------|
| MQTT Connectivity | 5 | Connect, disconnect, reconnect |
| Full Workflow | 5 | Owner + device scenarios |
| Cross-Language | 5 | Python device ↔ C owner |

### Test Requirements

- **Mock tests:** No MQTT broker needed (mock at CFFI level)
- **Integration tests:** Require Mosquitto broker
- **CI:** Run both in GitHub Actions

---

## Success Metrics

| Metric | Target |
|--------|--------|
| API Coverage | 100% of public C functions wrapped |
| Test Coverage | >90% of Python code |
| Platforms | Linux x86_64, Linux aarch64, macOS x86_64, macOS arm64 |
| Python Versions | 3.8, 3.9, 3.10, 3.11, 3.12 |
| Installation | `pip install sds-library` works without build tools |
| Documentation | README + API reference + examples |

---

## Dependencies

### Build Dependencies

| Package | Version | Purpose |
|---------|---------|---------|
| cffi | >=1.0.0 | C bindings |
| setuptools | >=45 | Build system |
| wheel | latest | Wheel building |

### Runtime Dependencies

| Package | Version | Purpose |
|---------|---------|---------|
| cffi | >=1.0.0 | C bindings (runtime) |

### Development Dependencies

| Package | Version | Purpose |
|---------|---------|---------|
| pytest | >=7.0 | Testing |
| pytest-cov | latest | Coverage |
| mypy | latest | Type checking |
| black | latest | Code formatting |
| ruff | latest | Linting |

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| CFFI complexity with callbacks | Medium | High | Start with ABI mode, add callbacks incrementally |
| Cross-platform build issues | Medium | Medium | Use cibuildwheel, test on CI early |
| Memory leaks at boundary | Low | High | Use context managers, add leak tests |
| API changes in C library | Low | Medium | Version lock, integration tests |

---

## Implementation Order

### Recommended Sequence

1. **Phase 1.1** - Project setup (foundation)
2. **Phase 1.2** - Core bindings (get something working)
3. **Phase 2.1** - SdsNode class (usable API)
4. **Phase 3.1** - API-level build (proper packaging)
5. **Phase 2.2** - Table helpers (ergonomics)
6. **Phase 2.3** - Callbacks (full functionality)
7. **Phase 3.2** - Cross-platform wheels (distribution)
8. **Phase 4** - Documentation & examples
9. **Phase 3.3** - CI/CD (automation)

### Minimum Viable Product (MVP)

Phases 1 + 2.1 + 3.1 provide a usable wrapper:
- Can init/cleanup
- Can register tables
- Can publish state/status
- Can poll for messages
- Works on developer's machine

---

## Estimated Effort

| Phase | Tasks | Estimated Effort |
|-------|-------|------------------|
| Phase 1: Foundation | 12 | 2-3 days |
| Phase 2: Pythonic API | 12 | 3-4 days |
| Phase 3: Build & Distribution | 10 | 2-3 days |
| Phase 4: Documentation | 8 | 1-2 days |
| **Total** | **42** | **8-12 days** |

---

## Open Questions

1. **Table definition approach:** Should we require Python table classes that mirror C structs, or auto-generate from the existing codegen?

2. **Async support:** Should we provide an async API (`async def poll()`) for asyncio compatibility?

3. **Logging integration:** Should Python logs go through C's logging, or use Python's `logging` module?

4. **Package name:** `sds-library`, `pysds`, or `sds-python`?

---

## References

- [CFFI Documentation](https://cffi.readthedocs.io/)
- [cibuildwheel Documentation](https://cibuildwheel.readthedocs.io/)
- [Python Packaging Guide](https://packaging.python.org/)
- SDS C Library: `include/sds.h`, `src/sds_core.c`
