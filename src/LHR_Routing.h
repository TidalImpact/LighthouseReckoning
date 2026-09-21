/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_Routing.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.2.0
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Neighbor table management and next-hop selection logic.
 *   Maintains per-neighbor RSSI and hop-count, and selects
 *   the best relay toward the Home node.
 *
 *
 * License:    MIT
 **************************************************************************/

#pragma once

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include <stdint.h>
  #include <stddef.h>
#endif

#include "LHR_TypeDef.h"
#include "LHR_Packet.h"


// ================================================================
// Neighbor Table Configuration
// ================================================================

#ifndef LHR_MAX_NEIGHBORS
  // Max entries in neighbor table. Each entry is sizeof(lhr_neighbor_t)
  // (16 bytes) — 8 entries costs 128 bytes RAM. Reduce on
  // memory-constrained targets (e.g. AVR) via a project-level #define
  // before including this header.
  #define LHR_MAX_NEIGHBORS (8)  // Max entries in neighbor table
#endif

#ifndef LHR_NEIGHBOR_TIMEOUT_BEACONS
  // Number of missed beacon intervals before a neighbor is considered stale.
  // Actual timeout in ms = _beaconIntervalMs * LHR_NEIGHBOR_TIMEOUT_BEACONS
  // (see _pruneStaleNeighbors() in LHR_Routing.cpp).
  #define LHR_NEIGHBOR_TIMEOUT_BEACONS (3)
#endif


// ================================================================
// Neighbor Entry
// ================================================================

typedef struct {
    uint32_t Id;                  // Neighbor device ID (4 bytes)
    uint8_t  hops_to_home;        // How many hops this neighbor is from Home
    float    rssi;                // Signal strength in dBm
    unsigned long last_seen_ms;   // Timestamp of last received NDAT (millis)
} lhr_neighbor_t;