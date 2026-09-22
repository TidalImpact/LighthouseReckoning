# Changelog

All notable changes to this project will be documented in this file.

## [1.2.0] - 2026-09-22

### Added

* Cross-core (dual-core) safety for RP2040 and ESP32: public API functions (`beginAsHome()`, `beginAsNode()`, `sendData()`, `update()`, `isBusy()`, duty-cycle getters/setters, encryption functions) and select internal/debug functions (`printNeighborTable()`) are now guarded by an internal recursive lock.

### Changed

* Updated file headers and protocol/version references to v1.2.0.
* README now references the general Zenodo DOI instead of a version-specific one.

### Fixed

* Fixed unused-parameter compiler warnings in `_handleNDAT()` and `_handleRFCN()` on MCUs without encryption support.

## [1.1.0] - 2026-09-11

### Added

* Optional AES-128-CCM encryption and authentication for all packet types: `DATA`, `NDAT`, `RFCN`, and `DATA_RES`.
* Persistent nonce counter storage with crash-safe counter reservation on ESP32 and RP2040.
* Encryption-related public API: `setEncryptionKey()`, `enableEncryption()`, and `isEncryptionEnabled()`.
* New error codes for encryption, authentication, nonce, and persistent storage failures.
* Protocol documentation for encrypted packet formats, nonce handling, encryption mode, and security considerations.

### Changed

* Encrypted DATA packets now use a larger packet header, and maximum payload size and duty-cycle airtime calculations account for the additional encryption overhead.
* Relayed encrypted DATA packets are decrypted and authenticated before forwarding and re-encrypted with a new nonce.
* Updated protocol version and API documentation to reflect the v1.1.0 encryption features.

### Known Limitations

* Encryption is currently supported only on ESP32 and RP2040.
* Every node in a given mesh must use the same encryption key. Nodes with encryption enabled and nodes without encryption enabled cannot interoperate.
* Replay protection is not currently implemented.
* Encryption is hop-by-hop rather than end-to-end; relaying nodes decrypt and re-encrypt packets before forwarding them.

## [1.0.0] - 2026-08-11

Initial public release of Lighthouse Reckoning.
Development started in late June 2026; this repository was made public in August 2026.

### Added

#### Core Protocol

* Single-sink LoRa mesh networking with one Home node and multiple Sensor/Relay nodes.
* Distance-vector routing based on hop count and RSSI.
* Four packet types: `DATA`, `NDAT`, `RFCN`, and `DATA_RES`.
* Hop-by-hop delivery confirmation using `DATA_RES`.
* Neighbor discovery and route advertisement.
* Reactive route discovery and recovery using `RFCN`.
* Duplicate packet detection.
* Loop avoidance during packet forwarding.
* Configurable TTL for DATA packets.

#### Reliability

* Configurable DATA retry handling with timeout and route recovery.
* Maximum local retry limit.
* TX watchdog for recovery from stalled transmissions.
* Non-blocking packet processing through `update()`.

#### Duty Cycle

* Optional duty-cycle management.
* Percentage-based and absolute airtime limits.
* Configurable duty-cycle window and airtime budget.
* Radio airtime accounting based on RadioLib's time-on-air calculation.

#### Radio / Transport

* RadioLib-based radio abstraction using `PhysicalLayer`.
* SX126x validation, including SX1262.
* Channel Activity Detection (CAD) before transmission.
* Randomized jitter for NDAT responses to reduce collisions.
* Interrupt-driven radio event handling.

#### Public API

* `beginAsHome()` and `beginAsNode()` for node initialization.
* `sendData()` for application data transmission.
* `update()` and `handleDio1Rise()` for non-blocking operation.
* Configuration for TTL, retries, beacon timing, watchdog timeout, and duty cycle.
* Status and routing information getters.
* `onDataReceived()` callback for received application data.
* Radio error reporting through `getLastRadioError()`.

#### Debug Utilities

* Minimum-hop-distance filtering for multi-hop testing.
* Optional per-hop routing metadata (experimental — not part of the stable wire format).
* Neighbor table inspection and debug output.

#### Platform Support

* Raspberry Pi Pico 2 / RP2040.
* ESP32.
* Platform abstraction for timing and critical sections.
* Generic Arduino and additional platform support where compatible.

#### Examples

* `HomeNode` — Home node and data reception.
* `BasicNode` — Basic Sensor/Relay node.
* `RelayNode` — Relay configuration.
* `NodeAckTracking` — Monitoring hop confirmations.
* `NodeConfiguration` — Library configuration.

#### Documentation

* `docs/PROTOCOL.md` — normative protocol specification.
* `docs/PROTOCOL_REFERENCE.md` — protocol specification with implementation references.
* `README.md` — project overview, installation, usage, and configuration.

### Known Limitations

* No encryption or authentication.
* `DATA_RES` provides hop-by-hop confirmation only; it does not provide end-to-end confirmation that a packet reached Home.
* A relay processes only one forwarded DATA packet at a time and does not queue additional packets.
* Routing metadata is experimental and is not part of the stable wire protocol.
