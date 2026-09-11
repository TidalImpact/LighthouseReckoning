/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_EncryptionStore.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.1.0
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

#include "LHR_TypeDef.h"
