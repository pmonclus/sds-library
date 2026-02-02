# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.1] - 2026-02-01

### Added

- **Raw MQTT Subscribe API**: Receive custom messages through the SDS connection
  - `sds_subscribe_raw()` - Subscribe to arbitrary MQTT topics with wildcards
  - `sds_unsubscribe_raw()` - Unsubscribe from topics
  - `SdsRawMessageCallback` - Callback type for received messages
  - Supports `+` (single-level) and `#` (multi-level) wildcards
  - Topics starting with `sds/` are reserved and rejected
  - Maximum 8 concurrent raw subscriptions
  - Python bindings: `subscribe_raw()`, `unsubscribe_raw()` methods

### Tests

- 10 new C unit tests for raw subscribe functionality
- 5 new Python integration tests for raw subscribe

## [0.5.0] - 2026-02-02

### Added

- **Delta Sync**: Only transmit changed fields in state/status messages
  - Enable with `enable_delta_sync` in `SdsConfig` (C) or `SdsNode` constructor (Python)
  - Configurable float tolerance via `delta_float_tolerance`
  - Reduces bandwidth by up to 90% for tables with many fields
  
- **1KB Section Support**: Increased maximum section size from 256 bytes to 1KB
  - `SDS_MSG_BUFFER_SIZE` increased to 2048 bytes
  - `SDS_SHADOW_SIZE` fallback increased to 1024 bytes
  
- **Raw MQTT Publish API**: Send custom messages through the SDS connection
  - `sds_is_connected()` - Check if MQTT connection is active
  - `sds_publish_raw()` - Publish arbitrary MQTT messages
  - Useful for logging, diagnostics, or custom application messages
  
- **Field Metadata Infrastructure**: Per-field change detection for delta sync
  - `SdsFieldType` enum for field data types
  - `SdsFieldMeta` struct for field descriptors
  - Codegen automatically generates field metadata from schema

### Changed

- `SdsTableMeta` extended with field metadata pointers and counts
- `SdsConfig` extended with `enable_delta_sync` and `delta_float_tolerance`
- Codegen generates `SdsFieldMeta` arrays for all table sections

### Notes

- Config messages are always sent in full (retained on broker for new subscribers)
- Status liveness heartbeats are always sent in full
- Delta sync requires field metadata from codegen; manual registration uses full sync

## [0.4.4] - 2026-01-31

### Added

- `sds_set_schema_version()` API for setting schema version at runtime
- Python binding for `set_schema_version()`

## [0.4.3] - 2026-01-30

### Fixed

- Fixed int8/int16/uint16 serialization in Python bindings

## [0.4.2] - 2026-01-29

### Added

- Python eviction tests
- Device eviction callbacks

## [0.4.1] - 2026-01-28

### Added

- Initial Homebrew formula
- Arduino library packaging

## [0.4.0] - 2026-01-27

### Added

- Registry-based simple API (`sds_register_table()`)
- Python bindings with CFFI
- Schema-driven code generation
- Liveness detection and LWT handling
- Device eviction with grace period
