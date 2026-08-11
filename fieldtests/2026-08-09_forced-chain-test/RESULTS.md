# Field Test: Lighthouse Reckoning – Forced Multi-Hop Chain Test

**Conducted by:** Fynn Jannis Schulz — 2026-08-09

## Goal

Validate the routing behavior of the `LighthouseReckoning` library under a **forced multi-hop chain**.

The four field nodes were all physically located within roughly 10–20 m of each other and could easily have reached HOME directly over the radio link. The observed chain was therefore **not** a result of limited radio range; it was deliberately enforced through the library configuration.


## Test Setup

`lhr.setMinimumAcceptedHops()` was configured with a different value on each of the four nodes. This parameter defines the minimum hop count a node requires before it is allowed to consider itself connected to HOME: a value of `0` means that a direct link to HOME is acceptable, while higher values require the node to route through additional relay hops before it considers the route valid.

The four nodes were configured with values `0`, `1`, `2`, and `3`, respectively, enforcing the following linear relay chain even though all nodes were within direct radio range of HOME:

```text
AA162254 → 15641222 → E294D3F3 → E6E1D689 → HOME
```

* **HOME**: pure logger/receiver: its node ID is not relevant for this test
* **E6E1D689**: the only node allowed to use a direct link to HOME
* **AA162254**: required to route through all three other nodes

The purpose of this setup was to observe multi-hop behavior, including TTL decrementing, relay timing, and packet loss across multiple hops, without radio range affecting the results.

Duty-cycle limiting was disabled for this test (`toggleDutyCycleLimit()` was not enabled), so airtime budgeting is not a factor in the results below.

## Radio Configuration

All four field nodes and HOME used identical radio settings:

| Parameter | Value |
|---|---|
| Frequency | 868.0 MHz |
| Bandwidth | 125 kHz |
| Spreading Factor | SF9 |
| Coding Rate | 4/8 |
| TX Power | 22 dBm |
| Preamble Length | 16 symbols |
| Sync Word | 0xA4 |
| CRC | Enabled |
| Low Data Rate Optimization | Disabled |

These settings directly affect airtime per packet and therefore the collision probability discussed under [Packet Loss](#packet-loss) below; the same forced-chain topology tested with a different SF/BW combination would be expected to show slightly different timing and loss characteristics.

## Test Duration

* **Start:** 2026-08-09, 14:49:44
* **End:** 2026-08-09, 16:42:57
* **Total duration:** approximately 1 hour 53 minutes (6,793 s)

## TTL Behavior

TTL is decremented on every local send attempt. The starting value of `8` is identical across all nodes; the value observed at HOME therefore reflects the number of transmissions required to reach it:

| Node     | Hops to HOME | TTL (expected) | TTL (observed)            |
| -------- | -----------: | -------------: | ------------------------- |
| E6E1D689 |            0 |              8 | 8 — consistent throughout |
| E294D3F3 |            1 |              7 | 7 — consistent throughout |
| 15641222 |            2 |              6 | 6 — consistent throughout |
| AA162254 |            3 |              5 | 5 — consistent throughout |

No unexpected TTL values were observed during the entire test. The routing therefore followed the forced chain consistently.

## Send Intervals

Each node transmits according to its own fixed base interval:

| Node     | Avg. interval |    Min |    Max |
| -------- | ------------: | -----: | -----: |
| E6E1D689 |        ~120 s | ~115 s | ~125 s |
| E294D3F3 |        ~120 s | ~115 s | ~125 s |
| 15641222 |         ~92 s |  ~59 s | ~180 s |
| AA162254 |         ~61 s |  ~40 s | ~180 s |

Each node fires its local send timer on a fixed schedule: 120 s, 120 s, 90 s, and 60 s respectively.

The variation in observed arrival intervals does not indicate that these local timers are changing. Instead, it reflects the time required for packets to propagate through the relay chain. Queueing at relays, retry cycles, and RFCN-triggered route recovery can introduce additional timing variation between the original transmission and its arrival at HOME.

E6E1D689 and E294D3F3 show only about ±5 s of variation, while the two longer paths vary more. This is expected to some degree because the longer paths have more relay stages where delays can occur.

Despite this variation, packets continued to arrive successfully, indicating that the retry and relay mechanisms were generally able to recover from normal timing contention.

Two isolated shorter-than-base intervals were also observed:

* `AA162254`: 14:57:04 → 14:57:44
* `15641222`: 15:56:58 → 15:57:57

Looking at each case in context, the short interval is preceded by a
packet that itself arrived late relative to the node's established
cadence. For `AA162254`, the packet expected at approximately 14:56:44
arrived at 14:57:04 (approximately 20 s late). For `15641222`, the
packet expected at approximately 15:56:27 arrived at 15:56:58
(approximately 31 s late).

In both cases, the following packet — 14:57:44 and 15:57:57,
respectively — arrived almost exactly at the time dictated by the
node's original send schedule. The apparent short interval is therefore
not an early transmission. The preceding packet was delayed by the
library's retry/relay process, while the node's underlying send
schedule remained unchanged.

This behavior demonstrates the retry mechanism operating as intended:
the delayed packet required additional retry/relay attempts before it
was successfully delivered, but ultimately reached HOME rather than
being dropped. In these two cases, the packets eventually reached HOME
within the available retry budget. The packets counted as losses did not.

The two instances occurred approximately one hour apart and were not
associated with packet loss. Since duty-cycle limiting was disabled
during the test, it is not considered a contributing factor. These
events are therefore treated as successful retry recoveries and are not
included in the packet-loss analysis.

## Packet Loss

| Node     | Estimated packet losses | Time(s)                                                    |
| -------- | ----------------------: | ---------------------------------------------------------- |
| AA162254 |                       1 | ~16:32 (16:31:44 → 16:34:44)                               |
| 15641222 |                       2 | ~14:56 (14:54:57 → 14:57:57), ~16:31 (16:30:57 → 16:33:57) |
| E294D3F3 |                       0 | –                                                          |
| E6E1D689 |                       0 | –                                                          |

Three packet losses were observed in total. Two occurred almost simultaneously, affecting `AA162254` and `15641222` around 16:31–16:34.

Because all four nodes were physically close together and transmitted independently, the most likely explanation is **airtime contention**: overlapping DATA transmissions and/or periodic NDAT beacons may have occupied the channel when a relay needed to transmit or receive.

The library was tested with its default, out-of-the-box configuration, without custom TTL, retry, or beacon settings. The default retry limit was three cycles.

In all three loss instances, the packet following the gap returned to the normal schedule. There is no indication of a delayed packet being held by retries and subsequently followed by a rapid catch-up transmission. This is consistent with the affected packet exhausting its available retry attempts without successfully completing the required relay path.

However, because the test only records whether the packet ultimately reaches HOME, the exact failed transmission cannot be identified for the multi-hop nodes.

For `AA162254` and `15641222`, a recorded loss could therefore have occurred at any point along their respective relay paths. The logs establish that the packet did not reach HOME, but do not identify the individual relay segment where the failure occurred.

By contrast, `E294D3F3` and `E6E1D689` transmit directly or with only one hop to HOME. Neither showed any packet loss during the test.

## Conclusion

Over approximately 1 hour and 53 minutes, the forced multi-hop chain maintained the expected TTL behavior throughout the entire test. No unexpected TTL values were observed.

Three packet losses were identified. Two occurred at approximately the same time on `AA162254` and `15641222`, which is consistent with a shared channel-level cause such as airtime contention from overlapping DATA transmissions or background NDAT beacons.

The test used the default retry configuration of three cycles. The observed gaps are consistent with packets exhausting their retry opportunities without successfully completing the required relay path, although the exact failed relay segment cannot be determined from HOME-level reception data alone.

The two nodes closest to HOME, `E294D3F3` and `E6E1D689`, experienced no packet loss. Since all nodes were physically within direct radio range of HOME, the observed losses are therefore unlikely to be attributable to insufficient radio range.

The test shows that the library's core routing mechanism operates reliably under a deliberately enforced multi-hop topology, including correct TTL propagation and successful packet delivery through multiple relay stages. The small number of observed losses appears to be associated with channel contention rather than a systematic routing failure.