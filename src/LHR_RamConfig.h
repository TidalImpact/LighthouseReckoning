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