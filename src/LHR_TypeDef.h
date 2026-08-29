/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_TypeDef.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.0.0
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Shared typedefs, error codes, and data structures for the
 *   Lighthouse Reckoning library. Included by all LHR_* modules.
 *
 *   This file has no logic — only type definitions.
 *
 * License:    MIT
 **************************************************************************/


#pragma once
#include <stdint.h>
#include <stdlib.h>


// ================================================================
// Debug Output
// ================================================================

#ifdef LHR_DEBUG

  #ifdef ARDUINO

    #define LHR_DEBUG_PRINT(M, ...)    Serial.printf(M, ##__VA_ARGS__)
    #define LHR_DEBUG_PRINTLN(M, ...)  Serial.printf(M "\r\n", ##__VA_ARGS__)

  #else

    #include <cstdio>
    #define LHR_DEBUG_PRINT(M, ...)    fprintf(stdout, M, ##__VA_ARGS__)
    #define LHR_DEBUG_PRINTLN(M, ...)  fprintf(stdout, M "\n", ##__VA_ARGS__)

  #endif

#else

  #define LHR_DEBUG_PRINT(M, ...)
  #define LHR_DEBUG_PRINTLN(M, ...)

#endif


// ================================================================
// Platform Abstraction — Timing
// ================================================================

#ifdef ARDUINO
    #define LHR_MILLIS()     millis()
    #define LHR_DELAY(ms)    delay(ms)
#else
    #include <chrono>
    #include <thread>
    #define LHR_MILLIS() ((unsigned long)(std::chrono::duration_cast\
        <std::chrono::milliseconds>(std::chrono::steady_clock::now()\
        .time_since_epoch()).count()))
    #define LHR_DELAY(ms) std::this_thread::sleep_for\
        (std::chrono::milliseconds(ms))
#endif


// ================================================================
// Platform capability — persistent counter support
// ================================================================
//
// Does this MCU provide the non-volatile storage we need for a
// monotonic nonce counter?  If not, the entire security path is
// compiled out.
//

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32) || \
    defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040) || defined(PICO_RP2350)
  #define LHR_ENCRYPTION_SUPPORTED 1
#else
  #define LHR_ENCRYPTION_SUPPORTED 0   // classic AVR Arduinos etc. → no encryption code
#endif


// ================================================================
// Platform Abstraction — Critical Sections
// ================================================================

#if defined(ESP_PLATFORM)

  #define LHR_ENTER_CRITICAL() noInterrupts()
  #define LHR_EXIT_CRITICAL()  interrupts()

#elif defined(ARDUINO_ARCH_RP2040)

  #define LHR_ENTER_CRITICAL() noInterrupts()
  #define LHR_EXIT_CRITICAL()  interrupts()

#elif defined(STM32F1xx) || defined(STM32F4xx)

  #define LHR_ENTER_CRITICAL() __disable_irq()
  #define LHR_EXIT_CRITICAL()  __enable_irq()

#elif defined(ARDUINO)

  #define LHR_ENTER_CRITICAL() noInterrupts()
  #define LHR_EXIT_CRITICAL()  interrupts()

#else

  #define LHR_ENTER_CRITICAL()
  #define LHR_EXIT_CRITICAL()

#endif


// ================================================================
// Protocol Constants — Node / Routing Sentinels
// ================================================================

constexpr uint32_t LHR_FORBIDDEN_NODE_ID = 0x00000000;
// Reserved invalid node Id. Never assign this value to a real device.
// Used throughout the routing logic as a sentinel for "no valid node".
// Also prevents the XORshift32 PRNG seed (_randState = deviceId in
// beginAsHome/beginAsNode) from being 0, which would result in constant output
// (see _random() in LHR_Tx.cpp).

constexpr uint32_t LHR_DEFAULT_BEACON_INTERVAL_MS = 60000;     // Default beacon interval
constexpr uint8_t LHR_DEFAULT_TTL = 8;                          // Default TTL for outgoing DATA packets

constexpr uint8_t LHR_HOPS_AS_HOME       = 0;                   // Assigned when node starts as Home
constexpr uint8_t LHR_HOPS_NO_ROUTE      = 255;                 // Route lost at runtime or init without route
constexpr uint8_t LHR_HOPS_AS_RELAY_INIT = LHR_HOPS_NO_ROUTE;   // Relay starts without a known route (same value as LHR_HOPS_NO_ROUTE)

constexpr float LHR_RSSI_UNKNOWN = -999.0f;
// Sentinel value outside any physically plausible RSSI range
// (real LoRa RSSI is typically -20 to -140 dBm). Used to mark
// "no RSSI reading available" without needing a separate flag.


// ================================================================
// Protocol Constants — Timing / Retries
// ================================================================

constexpr uint32_t LHR_DEFAULT_RESEND_DELAY_MS = 5000;   // ms before first resend attempt
constexpr uint32_t LHR_DEFAULT_RFCN_WAIT_MS    = 5000;   // ms to wait after sending RFCN

constexpr uint8_t LHR_DEFAULT_MAX_LOCAL_RETRIES = 3;
// Maximum number of DATA_RES retry cycles before dropping a packet.
// Timing depends on resend/RFCN delay configuration.
// default configuration takes roughly 15 seconds per cycle

constexpr uint8_t LHR_SEEN_CACHE_SIZE = 8;
// Ring buffer size for duplicate-packet detection (see _isDuplicateData()
// in LHR_Rx.cpp). 8 entries covers typical retry bursts; increase if
// many concurrent senders are expected to produce overlapping retries.


constexpr uint32_t LHR_NONCE_BATCH_SIZE = 100;
// Nonce counter reservation batch size for persistent storage writes.
// On enableEncryption() and whenever the RAM counter catches up to the
// last reserved boundary, the store is advanced by this amount so a
// crash between writes can never cause nonce reuse — worst case this
// many counter values are skipped


// ================================================================
// Protocol Constants — Duty Cycle
// ================================================================

#ifndef LHR_DUTY_CYCLE_WINDOW_MS
  #define LHR_DUTY_CYCLE_WINDOW_MS (3600000UL)   // 1h — regulatory
#endif
constexpr float LHR_DEFAULT_DUTY_CYCLE_PERCENT = 1.0f;    // EU868 g1 sub-band default
// NOTE: these defaults are EU868-specific. For other regions (US915,
// AU915, etc.) call setDutyCycleLimit()/setDutyCycleLimitMs() with
// values matching the local regulatory duty cycle.


// ================================================================
// Protocol Constants — AES-128-CCM
// ================================================================

// ── Nonce ────────────────────────────────────────────────────────
constexpr uint8_t LHR_NONCE_OFFSET_DEVICEID      = 0; // [0..3] Device ID
constexpr uint8_t LHR_NONCE_OFFSET_NONCE_COUNTER = 4; // [4..7] Nonce Counter
constexpr uint8_t LHR_NONCE_OFFSET_PADDING       = 8; // [8..12] Padding

// ================================================================
// Result / Error Enums
// ================================================================

typedef enum lhr_init_result : uint8_t {
    LHR_INIT_OK                 = 0,   // Initialization successful
    LHR_INIT_ERR_RADIO_NULL     = 1,   // Radio pointer is nullptr
    LHR_INIT_ERR_INVALID_ARGS   = 2,   // Invalid initialization parameters
} lhr_init_result_t;

typedef enum lhr_err : uint8_t {
    LHR_OK                        =  0,   // Success
    LHR_ERR_TOO_LONG              =  1,   // Payload exceeds LHR_MAX_PAYLOAD
    LHR_ERR_NO_ROUTE              =  2,   // No neighbor known yet
    LHR_ERR_BUSY                  =  3,   // Waiting for DATA_RES, can't send now
    LHR_ERR_ARGS                  =  4,   // Null pointer or zero length passed
    LHR_ERR_EXTERNAL              =  5,   // External error (e.g. radio send failed)
    LHR_ERR_FULL_NEIGHBORS        =  6,   // Neighbor table is full, can't add new neighbor
    LHR_ERR_RADIO_NOT_INIT        =  7,   // Radio not initialized
    LHR_ERR_ZERO_TTL              =  8,   // TTL is zero, can't send packet
    LHR_ERR_WRONG_ROLE            =  9,   // Operation is not supported by the current node role
    LHR_ERR_NOT_CONFIGURED        =  10,  // begin() was not called before use
    LHR_ERR_DUTY_CYCLE_EXHAUSTED  =  11,  // Duty cycle budget exhausted, cannot send now

    // --- Encryption / LHR_Encryption ---
    LHR_ERR_INVALID_KEY_LEN       =  12,  // Key length doesn't match cipher requirement (e.g. != 16 for AES-128)
    LHR_ERR_NO_KEY_SET            =  13,  // enableEncryption() called before setEncryptionKey()
    LHR_ERR_ENCRYPT_FAIL          =  14,  // Underlying encrypt operation failed
    LHR_ERR_DECRYPT_FAIL          =  15,  // Underlying decrypt operation failed
    LHR_ERR_AUTH_FAIL             =  16,  // Auth tag mismatch — packet rejected (tampered or wrong key)
    LHR_ERR_NONCE_EXHAUSTED       =  17,  // Nonce counter reached its maximum value

    // --- Encryption Storage / LHR_EncryptionStore ---
    LHR_ERR_STORE_NOT_INIT        =  18,  // beginStore() not called before use
    LHR_ERR_STORE_WRITE_FAIL      =  19,  // NVS/EEPROM/Flash write failed
    LHR_ERR_STORE_READ_FAIL       =  20,  // NVS/EEPROM/Flash read failed
} lhr_err_t;

typedef enum lhr_tx_result : uint8_t {
    LHR_TX_OK             = 0,
    LHR_TX_CHANNEL_BUSY   = 1,   // CAD detected activity
    LHR_TX_DUTY_CYCLE     = 2,   // over budget
    LHR_TX_RADIO_ERROR    = 3,   // startTransmit() failed -- see getLastRadioError()
    LHR_TX_RADIO_WORKING  = 4,   // Radio is currently sending
} lhr_tx_result_t;

typedef enum lhr_update_result : uint8_t {
    LHR_UPDATE_IDLE                   = 0,  // Nothing happened
    LHR_UPDATE_RX_DATA                = 1,  // DATA packet received
    LHR_UPDATE_RX_NDAT                = 2,  // NDAT packet received, neighbor table updated
    LHR_UPDATE_RX_RFCN                = 3,  // RFCN received, NDAT sent in response
    LHR_UPDATE_RX_DATARES             = 4,  // DATA_RES received but did not confirm the outstanding transmission (wrong Receiver ID or wrong Sequence Number)
    LHR_UPDATE_RX_ERROR               = 5,  // External error (e.g. radio send failed)
    LHR_UPDATE_TX_COMPLETE            = 6,  // Data packet successfully sent
    LHR_UPDATE_TX_BEACON_SCHEDULED    = 7,  // Beacon scheduled for later transmission
    LHR_UPDATE_TX_BEACON_SEND         = 8,  // Beacon sent (NDAT or RFCN)
    LHR_UPDATE_TX_RESEND              = 9,  // Packet resent after timeout
    LHR_UPDATE_TX_NDAT                = 10, // NDAT sent after receiving RFCN
    LHR_UPDATE_TX_ERROR               = 11, // Radio reported a transmit failure
    LHR_UPDATE_NOT_CONFIGURED         = 12, // begin() was not called before use
    LHR_UPDATE_TX_DATA_CONFIRMED      = 13, // DATA_RES matched pending ack — hop confirmed
    LHR_UPDATE_TX_DATA_FAILED         = 14, // Retries exhausted, packet dropped
} lhr_update_result_t;

typedef enum lhr_node_role : uint8_t {
    LHR_ROLE_NONE = 0,   // Not initialized yet
    LHR_ROLE_HOME = 1,   // Node currently acts as home
    LHR_ROLE_NODE = 2,   // Node currently acts as relay
} lhr_node_role_t;


// ================================================================
// Experimental
// ================================================================

constexpr uint8_t LHR_HOP_META_TAG  = 0x70;
constexpr uint8_t LHR_HOP_META_VLEN = 0x06;
constexpr uint8_t LHR_HOP_META_LEN  = 2 + LHR_HOP_META_VLEN;