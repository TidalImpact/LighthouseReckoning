/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_DutyCycle.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.2.0
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Shared includes for duty-cycle / airtime-limiting logic.
 *
 *   NOTE: LighthouseReckoning is currently a single class — the actual
 *   duty-cycle related method declarations live in LighthouseReckoning.h,
 *   not here. This file is currently unused.
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