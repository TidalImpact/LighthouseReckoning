/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_DutyCycle.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.1.0
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Duty-cycle window tracking and enforcement to keep
 *   transmissions within the configured airtime budget.
 *
 * License:    MIT
 **************************************************************************/

#include "LighthouseReckoning.h"


// ================================================================
// Internal window handling
// ================================================================

void LighthouseReckoning::_updateDutyCycleWindow() {
    if (LHR_MILLIS() - _dutyCycleStartTime >= LHR_DUTY_CYCLE_WINDOW_MS) {
        _dutyCycleStartTime = LHR_MILLIS();
        _dutyCycleUsedMs    = 0;

        LHR_DEBUG_PRINTLN("[DUTY] Window reset");
    }
}


// ================================================================
// Internal calculations
// ================================================================

unsigned long LighthouseReckoning::_dutyCycleLimitMs() const {
    return (unsigned long)(LHR_DUTY_CYCLE_WINDOW_MS * (_dutyCyclePercent / 100.0f));
}

unsigned long LighthouseReckoning::_dutyCycleRemainingMs() const {
    unsigned long limit = _dutyCycleLimitMs();
    return (_dutyCycleUsedMs >= limit) ? 0 : (limit - _dutyCycleUsedMs);
}


// ================================================================
// Internal accounting
// ================================================================

void LighthouseReckoning::_recordDutyCycleUsage(size_t len) {
    if (!_useDutyCycleLimit) {
        return;
    }

    _updateDutyCycleWindow();

    // RadioLib returns airtime in microseconds. Convert to milliseconds.
    unsigned long airtimeMs =
        (unsigned long)(_radio->getTimeOnAir(len) / 1000);

    _dutyCycleUsedMs += airtimeMs;

    LHR_DEBUG_PRINTLN(
        "[DUTY] +%lu ms, used %lu/%lu ms this window",
        airtimeMs,
        _dutyCycleUsedMs,
        _dutyCycleLimitMs());
}


// ================================================================
// Public configuration
// ================================================================

void LighthouseReckoning::toggleDutyCycleLimit(bool state) {
    if (state) {
        setDutyCycleLimit(LHR_DEFAULT_DUTY_CYCLE_PERCENT);
    } else {
        setDutyCycleLimit(0.0f);
    }
}

void LighthouseReckoning::setDutyCycleLimit(float percent) {
    LockGuard guard(_lock);
    // Changing the duty-cycle limit resets the current accounting window.
    _useDutyCycleLimit  = (percent > 0.0f);
    _dutyCyclePercent   = percent;
    _dutyCycleStartTime = LHR_MILLIS();
    _dutyCycleUsedMs    = 0;
}

void LighthouseReckoning::setDutyCycleLimitMs(unsigned long msPerWindow) {
    setDutyCycleLimit(
        (100.0f * (float)msPerWindow) /
        (float)LHR_DUTY_CYCLE_WINDOW_MS);
}


// ================================================================
// Public getters
// ================================================================

float LighthouseReckoning::getDutyCycleUsage() {
    LockGuard guard(_lock);
    _updateDutyCycleWindow();
    unsigned long limit = _dutyCycleLimitMs();

    if (limit == 0) {
        return 0.0f;
    }

    return (100.0f * (float)_dutyCycleUsedMs) / (float)limit;
}

unsigned long LighthouseReckoning::getDutyCycleUsedMs() {
    LockGuard guard(_lock);
    _updateDutyCycleWindow();
    return _dutyCycleUsedMs;
}

unsigned long LighthouseReckoning::getDutyCycleRemainingMs() {
    LockGuard guard(_lock);
    _updateDutyCycleWindow();
    return _dutyCycleRemainingMs();
}

unsigned long LighthouseReckoning::getDutyCycleLimitMs() const {
    LockGuard guard(_lock);
    return _dutyCycleLimitMs();
}