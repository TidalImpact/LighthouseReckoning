/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_Packet.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.0.0
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Packet identifiers, header layouts, field offsets, and size
 *   definitions for the Lighthouse Reckoning wire protocol.
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


// ================================================================
// Protocol Magic
// ================================================================

constexpr uint8_t LHR_START_BYTE = 0xA4;     // Every packet starts with this


// ================================================================
// Packet Types
// ================================================================

typedef enum lhr_pkt_type : uint8_t {
    LHR_PKT_DATA     = 0xE0,   // Application data, routed to Home
    LHR_PKT_NDAT     = 0xE1,   // Neighbor advertisement (broadcast)
    LHR_PKT_RFCN     = 0xE2,   // Request neighbors to send NDAT
    LHR_PKT_DATA_RES = 0xE3,   // ACK for received DATA packet
} lhr_pkt_type_t;


// ================================================================
// Packet Byte Offsets — Unencrypted
// ================================================================

// ── DATA (LHR_PKT_DATA) ─────────────────────────────────────────
constexpr uint8_t LHR_DATA_OFFSET_MAGIC    = 0;   // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_DATA_OFFSET_TYPE     = 1;   // Packet type
constexpr uint8_t LHR_DATA_OFFSET_SOURCE   = 2;   // [2..5]  Origin device Id
constexpr uint8_t LHR_DATA_OFFSET_SENDER   = 6;   // [6..9] Sender device Id
constexpr uint8_t LHR_DATA_OFFSET_RECEIVER = 10;  // [10..13] Receiver device Id
constexpr uint8_t LHR_DATA_OFFSET_SEQ_NUM  = 14;  // [14] Sequence number (1 byte)
constexpr uint8_t LHR_DATA_OFFSET_TTL      = 15;  // [15] Time to live (1 byte)
constexpr uint8_t LHR_DATA_OFFSET_PAYLOAD  = 16;  // [16..] Start of application payload

// ── NDAT (LHR_PKT_NDAT) ─────────────────────────────────────────
constexpr uint8_t LHR_NDAT_OFFSET_MAGIC  = 0;  // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_NDAT_OFFSET_TYPE   = 1;  // Packet type
constexpr uint8_t LHR_NDAT_OFFSET_SENDER = 2;  // [2..5] Sender device ID
constexpr uint8_t LHR_NDAT_OFFSET_HOPS   = 6;  // Hops to Home

// ── RFCN (LHR_PKT_RFCN) ─────────────────────────────────────────
constexpr uint8_t LHR_RFCN_OFFSET_MAGIC = 0;  // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_RFCN_OFFSET_TYPE  = 1;  // Packet type

// ── DATA_RES (LHR_PKT_DATA_RES) ─────────────────────────────────
constexpr uint8_t LHR_DATARES_OFFSET_MAGIC    = 0;   // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_DATARES_OFFSET_TYPE     = 1;   // Packet type
constexpr uint8_t LHR_DATARES_OFFSET_SENDER   = 2;   // [2..5] Sender device ID
constexpr uint8_t LHR_DATARES_OFFSET_RECEIVER = 6;   // [6..9] Receiver device ID
constexpr uint8_t LHR_DATARES_OFFSET_SEQ_NUM  = 10;  // Sequence number (1 byte)


// ================================================================
// Packet Lengths — Unencrypted
// ================================================================
//
// Derived from the offsets above wherever possible, so header layout
// and packet length can never drift apart.

constexpr uint8_t LHR_DATA_HEADER_LEN = LHR_DATA_OFFSET_PAYLOAD;          // Fixed header length for DATA packets
constexpr uint8_t LHR_NDAT_LEN        = LHR_NDAT_OFFSET_HOPS + 1;         // Fixed total length of NDAT packet
constexpr uint8_t LHR_RFCN_LEN        = LHR_RFCN_OFFSET_TYPE + 1;         // Fixed total length of RFCN packet
constexpr uint8_t LHR_DATA_RES_LEN    = LHR_DATARES_OFFSET_SEQ_NUM + 1;   // Fixed total length of DATA_RES packet

constexpr uint8_t LHR_MIN_VALID_PACKET_SIZE = 2;  // Minimum size: magic byte + packet type byte


// ================================================================
// Packet Byte Offsets — Encrypted
// ================================================================

// ── DATA ─────────────────────────────────────────────────────────

constexpr uint8_t LHR_DATA_ENC_OFFSET_MAGIC       = 0;  // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_DATA_ENC_OFFSET_TYPE        = 1;  // Packet type
constexpr uint8_t LHR_DATA_ENC_OFFSET_SOURCE      = 2;  // [2..5] Origin device ID
constexpr uint8_t LHR_DATA_ENC_OFFSET_SENDER      = 6;  // [6..9] Sender device ID
constexpr uint8_t LHR_DATA_ENC_OFFSET_RECEIVER    = 10; // [10..13] Receiver device ID
constexpr uint8_t LHR_DATA_ENC_OFFSET_SEQ_NUM     = 14; // Sequence number (1 byte)
constexpr uint8_t LHR_DATA_ENC_OFFSET_TTL         = 15; // Time to live (1 byte)
constexpr uint8_t LHR_DATA_ENC_OFFSET_WIRECOUNTER = 16; // [16..18] Wire nonce counter (3 bytes)
constexpr uint8_t LHR_DATA_ENC_OFFSET_MIC         = 19; // [19..22] Message authentication code (4 bytes)
constexpr uint8_t LHR_DATA_ENC_OFFSET_PAYLOAD     = 23; // [23..] Start of encrypted application payload


// ── NDAT ─────────────────────────────────────────────────────────

constexpr uint8_t LHR_NDAT_ENC_OFFSET_MAGIC       = 0;  // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_NDAT_ENC_OFFSET_TYPE        = 1;  // Packet type
constexpr uint8_t LHR_NDAT_ENC_OFFSET_SENDER      = 2;  // [2..5] Sender device ID
constexpr uint8_t LHR_NDAT_ENC_OFFSET_HOPS        = 6;  // [6] Hops to Home (1 byte)
constexpr uint8_t LHR_NDAT_ENC_OFFSET_WIRECOUNTER = 7;  // [7..9] Wire nonce counter (3 bytes)
constexpr uint8_t LHR_NDAT_ENC_OFFSET_MIC         = 10; // [10..13] Message authentication code (4 bytes)


// ── RFCN ─────────────────────────────────────────────────────────

constexpr uint8_t LHR_RFCN_ENC_OFFSET_MAGIC       = 0; // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_RFCN_ENC_OFFSET_TYPE        = 1; // Packet type
constexpr uint8_t LHR_RFCN_ENC_OFFSET_SENDER      = 2; // [2..5] Sender device ID
constexpr uint8_t LHR_RFCN_ENC_OFFSET_WIRECOUNTER = 6; // [6..8] Wire nonce counter (3 bytes)
constexpr uint8_t LHR_RFCN_ENC_OFFSET_MIC         = 9; // [9..12] Message authentication code (4 bytes)


// ── DATA_RES ────────────────────────────────────────────────────

constexpr uint8_t LHR_DATARES_ENC_OFFSET_MAGIC       = 0;  // Start byte (LHR_START_BYTE)
constexpr uint8_t LHR_DATARES_ENC_OFFSET_TYPE        = 1;  // Packet type
constexpr uint8_t LHR_DATARES_ENC_OFFSET_SENDER      = 2;  // [2..5] Sender device ID
constexpr uint8_t LHR_DATARES_ENC_OFFSET_RECEIVER    = 6;  // [6..9] Receiver device ID
constexpr uint8_t LHR_DATARES_ENC_OFFSET_SEQ_NUM     = 10; // Sequence number (1 byte)
constexpr uint8_t LHR_DATARES_ENC_OFFSET_WIRECOUNTER = 11; // [11..13] Wire nonce counter (3 bytes)
constexpr uint8_t LHR_DATARES_ENC_OFFSET_MIC         = 14; // [14..17] Message authentication code (4 bytes)


// ================================================================
// Packet Lengths — Encrypted
// ================================================================
//
// Derived from the offsets above wherever possible, so header layout
// and packet length can never drift apart.

constexpr uint8_t LHR_DATA_ENC_HEADER_FIELDS_LEN = 6; // SOURCE(4) + SEQ_NUM(1) + TTL(1) — header fields that get encrypted together with the payload

constexpr uint8_t LHR_DATA_HEADER_ENC_LEN = LHR_DATA_ENC_OFFSET_PAYLOAD;                // Fixed header length for DATA packets
constexpr uint8_t LHR_NDAT_ENC_LEN        = LHR_NDAT_ENC_OFFSET_MIC     + LHR_MIC_LEN;  // Fixed total length of NDAT packet
constexpr uint8_t LHR_RFCN_ENC_LEN        = LHR_RFCN_ENC_OFFSET_MIC     + LHR_MIC_LEN;  // Fixed total length of RFCN packet
constexpr uint8_t LHR_DATARES_ENC_LEN     = LHR_DATARES_ENC_OFFSET_MIC  + LHR_MIC_LEN;  // Fixed total length of DATA_RES packet

constexpr uint8_t LHR_MIN_VALID_PACKET_SIZE_ENC = 13;  // Minimum size: magic byte + packet type byte

// ================================================================
// Limits
// ================================================================

#ifndef LORA_PHY_MAX_PACKET_SIZE
  #define LORA_PHY_MAX_PACKET_SIZE (255)
#endif

constexpr uint8_t LHR_MAX_PAYLOAD     = LORA_PHY_MAX_PACKET_SIZE - LHR_DATA_HEADER_LEN;     // Max application payload in DATA packet
constexpr uint8_t LHR_MAX_PAYLOAD_ENC = LORA_PHY_MAX_PACKET_SIZE - LHR_DATA_HEADER_ENC_LEN; // Max application payload in encrypted DATA packet
