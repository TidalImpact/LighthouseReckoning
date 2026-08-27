/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_EncryptionStore.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.0.0
 * Platform:   ESP32 / RP2040 (Pico)
 * Description:
 *   Platform-specific persistent storage for the security nonce counter.
 *   Methods are members of LighthouseReckoning.
 *
 * License:    MIT
 **************************************************************************/

#include "LighthouseReckoning.h"


#if LHR_ENCRYPTION_SUPPORTED

// ================================================================
// ESP32 — Preferences / NVS
// ================================================================

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Preferences.h>

static Preferences prefs;
static const char* NVS_NAMESPACE = "lhr_sec";
static const char* NVS_KEY       = "nonce";


bool LighthouseReckoning::_storageInit() {
    return prefs.begin(NVS_NAMESPACE, false);
}


bool LighthouseReckoning::_loadNonceCounter(uint32_t* counter) {
    if (counter == nullptr) {
        return false;
    }

    if (!prefs.isKey(NVS_KEY)) {
        *counter = 0;
        return false;
    }

    *counter = prefs.getUInt(NVS_KEY, 0);
    return true;
}


bool LighthouseReckoning::_storeNonceCounter(uint32_t counter) {
    return prefs.putUInt(NVS_KEY, counter) > 0;
}


bool LighthouseReckoning::_setNonceCounter(uint32_t counter) {
    return _storeNonceCounter(counter);
}


bool LighthouseReckoning::_resetNonceCounter() {
    return _storeNonceCounter(0);
}


// ================================================================
// RP2040 / Pico — EEPROM emulation
// ================================================================

#elif defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040) || defined(PICO_RP2350)

#include <EEPROM.h>

static const int EEPROM_SIZE  = 32;
static const int COUNTER_ADDR = 0;


bool LighthouseReckoning::_storageInit() {
    EEPROM.begin(EEPROM_SIZE);
    return true;
}


bool LighthouseReckoning::_loadNonceCounter(uint32_t* counter) {
    if (counter == nullptr) {
        return false;
    }

    EEPROM.get(COUNTER_ADDR, *counter);

    // 0xFFFFFFFF = erased/uninitialised flash pattern → treat as "no value stored yet"
    if (*counter == 0xFFFFFFFFUL) {
        *counter = 0;
        return false;
    }

    return true;
}


bool LighthouseReckoning::_storeNonceCounter(uint32_t counter) {
    EEPROM.put(COUNTER_ADDR, counter);
    return EEPROM.commit();
}


bool LighthouseReckoning::_setNonceCounter(uint32_t counter) {
    return _storeNonceCounter(counter);
}


bool LighthouseReckoning::_resetNonceCounter() {
    return _storeNonceCounter(0);
}

#endif // board selection

#endif // LHR_ENCRYPTION_SUPPORTED