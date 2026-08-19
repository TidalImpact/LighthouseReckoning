# LighthouseReckoning - LoRa Mesh Networking for Arduino, ESP32 and RP2040

LighthouseReckoning is a LoRa mesh networking library for Arduino-family microcontrollers. It lets any number of sensor or relay nodes automatically find a multi-hop path back to a single "Home" node, without Wi-Fi or the internet.

## Why "Lighthouse Reckoning"?

The idea behind the name: a lighthouse doesn't go looking for ships, it just stands still so ships can find their way home. The **Home** node works the same way. It doesn't move and doesn't need to discover anyone; it's simply the point every other device is trying to reach.

Every other device is a **vessel**. Most of them can't reach Home directly, so they rely on nearby vessels to relay their data, hop by hop, the way a signal might travel down a coastline. "Reckoning" is the navigational term for constantly figuring out the best next step from whatever information is available right now, which is exactly what the routing layer does as neighbors, signal strength, and hop counts change over time.

## Getting Started

LighthouseReckoning is a software library for Arduino, ESP32, RP2040 and other Arduino-compatible boards. Once installed, it turns your boards into a self-routing mesh: one Home node collects data, and any number of Sensor/Relay nodes send data toward it, automatically finding the shortest available path.

This guide covers installing and using LighthouseReckoning with the Arduino IDE or PlatformIO.

## What You Need

- A computer with the Arduino IDE or PlatformIO installed (Windows, macOS, or Linux)
- An Arduino-compatible microcontroller board (tested on RP2040 and ESP32)
- A LoRa radio module supported by [RadioLib](https://github.com/jgromes/RadioLib) — validated on the SX126x family (e.g. SX1262); other RadioLib-supported radios should work for basic send/receive
- A USB cable to connect the board to your computer
- Basic familiarity with the Arduino IDE (installing libraries, selecting a board, uploading a sketch)

## Installation

1. Install [RadioLib](https://github.com/jgromes/RadioLib) via the Arduino Library Manager or PlatformIO — it's the only dependency.
2. Download or clone this repository into your Arduino `libraries` folder (or add it as a PlatformIO `lib_deps` entry pointing at this repo).
3. Include it in your sketch alongside your RadioLib radio driver: `#include <LighthouseReckoning.h>`

## Usage

There are two kinds of node: exactly one **Home** node, and any number of **Sensor/Relay** nodes.

1. **Connect your board:** Plug your Arduino-compatible board into a USB port on your computer.
2. **Wire up your LoRa module:** Connect the radio's CS, DIO1, RST, and BUSY pins as required by your board.
3. **Choose a role:** Call `beginAsNode()` on Sensor/Relay boards, or `beginAsHome()` on the single Home board.
4. **Give each node an ID:** Every node needs a unique Node ID (any value except `0x00000000`).
5. **Upload and run:** Call `update()` once per loop; it's non-blocking, so it never stalls your other code. Nodes automatically beacon their distance to Home and route data toward it hop by hop.
6. **Send and receive data:** Use `sendData()` on nodes, and handle incoming payloads on the Home node with an `onDataReceived()` callback.

Full working examples are in [`examples/RP2040/BasicNode`](https://github.com/TidalImpact/LighthouseReckoning/blob/main/examples/RP2040/BasicNode) and [`examples/RP2040/HomeNode`](https://github.com/TidalImpact/LighthouseReckoning/blob/main/examples/RP2040/HomeNode), with ESP32 equivalents under [`examples/ESP32`](https://github.com/TidalImpact/LighthouseReckoning/blob/main/examples/ESP32).

## Features

- **Automatic multi-hop routing:** Nodes discover their own path to Home via periodic and reactive neighbor beacons — no manual route configuration.
- **Hop-by-hop reliability:** Each transmission is confirmed by the next hop rather than relying on a single end-to-end acknowledgment, so Home doesn't need to track return paths to every node.
- **Non-blocking:** One `update()` call per loop is enough; the radio never blocks your other code.
- **Optional duty cycle budgeting** for regions with airtime regulations.
- **Small footprint:** 2-byte minimum packet size, 16-byte DATA header, and no dependencies beyond RadioLib.

## Configuration

Everything below is optional — the defaults are sensible enough that the examples work out of the box.

| Setting                  | Method                                           | Default                       |
| ------------------------ | ------------------------------------------------ | ------------------------------ |
| TTL for outgoing DATA    | `setTTL()`                                       | 8                              |
| Resend delay / RFCN wait | `setRetryDelays()`                               | 5000 ms / 5000 ms              |
| Max retry cycles         | `setMaxLocalRetries()`                           | 3                              |
| Beacon interval          | `setBeaconInterval()`                            | 60000 ms                       |
| TX watchdog timeout      | `setTxWatchdogTimeout()`                         | 10000 ms                       |
| Duty cycle limiting      | `toggleDutyCycleLimit()` / `setDutyCycleLimit()` | off; 1% (EU868) when enabled   |

## Troubleshooting

- **Board or radio not detected:** Check your wiring against the pins used in `beginAsNode()`/`beginAsHome()`, and make sure RadioLib is installed and initialized before LighthouseReckoning.
- **`sendData()` returns `LHR_ERR_BUSY`:** A previous packet is still awaiting confirmation — don't call `sendData()` every loop iteration, send on your own interval instead.
- **Nodes never reach Home:** Make sure each node is within range of at least one other node that eventually connects to Home, and that all nodes share the same radio settings (frequency, spreading factor, bandwidth, coding rate).
- **Updates:** Check the [CHANGELOG](https://github.com/TidalImpact/LighthouseReckoning/blob/main/CHANGELOG.md) for release notes and known limitations.

## Documentation

- **[docs/PROTOCOL.md](https://github.com/TidalImpact/LighthouseReckoning/blob/main/docs/PROTOCOL.md)** — the protocol specification: packet formats, routing, reliability, and timing, independent of this implementation.
- **[docs/PROTOCOL_REFERENCE.md](https://github.com/TidalImpact/LighthouseReckoning/blob/main/docs/PROTOCOL_REFERENCE.md)** — the same spec annotated with this library's constants, defaults, and full public API.
- **[fieldtests/2026-08-09_forced-chain-test](https://github.com/TidalImpact/LighthouseReckoning/blob/main/fieldtests/2026-08-09_forced-chain-test)** — a real multi-hop field test with the raw radio log and analysis, showing the routing behave under a forced 4-hop chain.

## Security

Version 1 is intentionally focused on getting core distance-vector routing and multi-hop retries solid first. Encryption is planned for a later version and is **not** implemented yet — don't rely on this for confidential data in its current state.

## Research Use

The Lower Saxony Ministry for Science and Culture (Germany) funds the "Central Laboratories for Digital Innovations in Lower Saxony" (ZDIN). Within ZDIN, the Central Laboratory for Water uses the Lighthouse Reckoning protocol in the sub-project Adam4EvesWine (Ad-hoc Data Acquisition Mesh for Enhanced Versatile Explorations of Waters In Near-shore Extent).

## Contributing

Bug reports and feature requests are welcome. The issue templates in [`.github/ISSUE_TEMPLATE`](https://github.com/TidalImpact/LighthouseReckoning/blob/main/.github/ISSUE_TEMPLATE) will guide you through what's useful to include — hardware, radio configuration, and logs for bug reports.

## Author

- **Creator / Lead Developer:** Fynn Jannis Schulz
- **Co-Design and field application setup:** Jan Schulz

## License

This project is licensed under the MIT License.

## Keywords

Arduino, embedded, ESP32, IoT, LoRa, LoRa mesh, mesh networks, RadioLib, RP2040, SX1262