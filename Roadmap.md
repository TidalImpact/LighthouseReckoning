# Lighthouse Reckoning — Roadmap

## Ongoing — Runs alongside every release

- Hardware testing across STM32 and other supported boards
- Chip evaluation and compatibility testing for other LoRa chips
- Stability, long-running and field tests
- Keep documentation and debug logging up to date

---

## v1.1 — Security

- AES-128-CCM
- PSK / key handling
- Authentication / integrity
- Nonce management

---

## v1.2 — Dual-Core Safety

- Shared-state protection
- Atomics / locks / critical sections

---

## v1.3 — Data Queue

- Data packet queuing
- Multiple pending packets
- Retry tracking per packet
- **Sleep mode:** allow the node to tell the caller how long it can safely remain asleep before it needs to wake up and call `update()` again

---

## v1.4 — Neighbour Evaluation & Routing Foundation

- Split neighbour table into two parts:
  - **Routing-neighbour table:** nodes we actually route through
  - **Incoming-neighbour table:** nodes sending to us
- Weighted neighbour score
- Initial score factors:
  - Hops to home
  - SNR
  - RSSI
- Track retry reliability as additional neighbour information
- Keep the score extensible for additional metrics
- Debug logging

---

## v1.5 — Learning: Timing

- Per-neighbour timestamp logging
- Interval estimation
- Early **"free window" estimation**
  - Identify periods where the LoRa chip does not need to be active
  - Primarily collect this information as groundwork for future SF/BW switching
  - Potentially use the information later to reduce LoRa power consumption

---

## v1.6 — Experimental: Mobility Detection

- Detect whether a node is stationary or moving
- RSSI/SNR trends
- Link stability
- Neighbour churn
- Stationary / transitioning / mobile states
- Confidence / hysteresis

---

## v1.7 — Experimental: Link Behaviour

- Track how link quality changes over time
- Identify stable vs. unstable links
- Track how quickly link quality changes
- Correlate link behaviour with retries and observed RSSI/SNR
- Build a better understanding of individual link behaviour

---

## v1.8 — Experimental: Spreading Factor (SF) / Bandwidth (BW) Suitability

- Learn which **SF/BW combinations** are likely to work for each link without actually switching them
- Compare lower SF/BW combinations for:
  - Shorter airtime
  - Higher data rate
- Compare higher SF/BW combinations for:
  - Increased robustness
  - Better reliability
- Learn the trade-off between reliability, airtime and data rate
- Use RSSI/SNR, retry history and observed link behaviour as inputs
- Try to find an efficient way to **guess or calculate a good SF/BW combination**
- Pure data collection to prepare for Multi-SF/Multi-BW in V2

---

# V2.0 — Protocol Redesign

- Advanced packet format
- Multi-SF / Multi-BW
- SF/BW fallback and negotiation
- Failure detection
- Load balancing
- Load as a routing-score factor
- Route avoidance for overloaded/unreliable relays
- Advanced routing using learned data:
  - Neighbour reliability
  - Timing / intervals
  - Free windows
  - Mobility
  - SF/BW suitability
  - Predicted neighbour behaviour