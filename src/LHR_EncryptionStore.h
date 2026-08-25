/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_EncryptionStore.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.0.0
 * Platform:   ESP32 / RP2040 (Pico) — encryption path only
 * Description:
 *   Security primitives for Lighthouse Reckoning (v1.1 roadmap).
 *   This header currently exposes the persistent nonce-counter storage
 *   backend. All encryption-related code is gated behind
 *   LHR_ENCRYPTION_SUPPORTED so unsupported MCUs (classic AVR, etc.)
 *   compile cleanly with zero overhead.
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
