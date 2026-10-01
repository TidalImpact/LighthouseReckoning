/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_RamConfig.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.2.0
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Compile-time feature toggles that trade functionality for static
 *   RAM. Each toggle documents its exact RAM cost and what behavior
 *   changes when it is disabled.
 *
 *   All toggles are enabled by default. Disable individual features
 *   to free up RAM on constrained boards (e.g. classic AVR Arduinos).
 *
 *   This file has no logic — only preprocessor configuration, same
 *   role as LHR_TypeDef.h but scoped to memory trade-offs specifically.
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
// Usage
// ================================================================
// Toggles are enabled by default. To disable one and free its RAM,
// comment out its #define directly below:
//
//   #define LHR_ENABLE_SOME_FEATURE   // enabled (default)
//   // #define LHR_ENABLE_SOME_FEATURE   // disabled — saves N bytes
//
// Edit this file directly — no sketch-side configuration needed.


// ================================================================
// Own-data send buffer
// ================================================================
// When enabled: sendData() writes into its own buffer, separate from
// the forward buffer used for relaying other nodes' packets. This
// means sendData() can queue one local data packet even while a
// forwarded packet from another node is in flight, instead of
// returning LHR_ERR_BUSY.
//
// When disabled: sendData() and packet forwarding share one buffer
// (v1.2.0 behavior). sendData() returns LHR_ERR_BUSY whenever any
// transmission — own or forwarded — is already in progress.
//
// RAM cost when enabled: ~259 bytes
//   = LHR_DATA_HEADER_LEN (16) + LHR_MAX_PAYLOAD (239) for the buffer
//     itself, plus a few bytes for the length member. Scales with
//     LORA_PHY_MAX_PACKET_SIZE if overridden.
//
// Disable on RAM-constrained boards (e.g. classic AVR Arduinos) where
// this buffer's RAM is needed elsewhere.
#define LHR_ENABLE_OWN_DATA_BUFFER
