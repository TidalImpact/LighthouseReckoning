/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LighthouseReckoning.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.0.0
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Core lifecycle, configuration, status API, and the main update()
 *   dispatch loop for the Lighthouse Reckoning class.
 *
 * License:    MIT
 **************************************************************************/

#include "LighthouseReckoning.h"


// ================================================================
// Internal State Management
// ================================================================

// Resets all runtime state before a new Home or Node initialization.
void LighthouseReckoning::_reset() {
    _role = LHR_ROLE_NONE;
    _radio = nullptr;
    _deviceId = 0;
    _resetNeighborTable();
    _resetDataResState();
    _forwardLen             = 0;
    _rxLen                  = 0;
    _packetReceived         = false;
    _working                = false;
    _beaconDue              = true;
    _lastBeaconMs           = 0;
    _ndatPending            = false;
    _ndatScheduledMs        = 0;
    _ndatJitterMs           = 0;
    _lastPacketSenderId     = 0;
    _lastAdvertisedHops     = LHR_HOPS_NO_ROUTE;
    _txInProgress           = false;
    _txDone                 = false;
    _minimumAcceptedHops    = 0;
}


// ================================================================
// Initialization
// ================================================================

lhr_init_result_t LighthouseReckoning::beginAsHome(PhysicalLayer* radio, uint32_t deviceId) {
    if (deviceId == LHR_FORBIDDEN_NODE_ID) {
        LHR_DEBUG_PRINTLN("[INIT] Never use the Id 0x00000000. It is strictly reserved for internal use. Using this Id will interfere with core infrastructure and may result in undefined behavior, communication failures, and other system problems.");
        return LHR_INIT_ERR_INVALID_ARGS;
    }
    if (radio == nullptr) {
        LHR_DEBUG_PRINTLN("[INIT] Radio pointer is null, cannot initialize");
        return LHR_INIT_ERR_RADIO_NULL; 
    }
    _reset();
    _radio      = radio;
    _deviceId   = deviceId;
    _hopsToHome = LHR_HOPS_AS_HOME; 
    _role       = LHR_ROLE_HOME;
    _randState = deviceId;              // Seeds the XORshift PRNG (see _random() in LHR_Tx.cpp).
    _sendNDAT();
    LHR_DEBUG_PRINTLN("[INIT] Home init");
    return LHR_INIT_OK;
}

lhr_init_result_t LighthouseReckoning::beginAsNode(PhysicalLayer* radio, uint32_t deviceId) {
    if (deviceId == LHR_FORBIDDEN_NODE_ID) {
        LHR_DEBUG_PRINTLN("[INIT] Never use the Id 0x00000000. It is strictly reserved for internal use. Using this Id will interfere with core infrastructure and may result in undefined behavior, communication failures, and other system problems.");
        return LHR_INIT_ERR_INVALID_ARGS;
    }
    if (radio == nullptr) {
        LHR_DEBUG_PRINTLN("[INIT] Radio pointer is null, cannot initialize");
        return LHR_INIT_ERR_RADIO_NULL; 
    }
    _reset();
    _radio      = radio;
    _deviceId   = deviceId;
    _hopsToHome = LHR_HOPS_AS_RELAY_INIT;
    _role       = LHR_ROLE_NODE;
    _randState = deviceId;              // Seeds the XORshift PRNG (see _random() in LHR_Tx.cpp).
    _sendRFCN();
    return LHR_INIT_OK;
}

// ================================================================
// Configuration API
// ================================================================

void LighthouseReckoning::setTTL(uint8_t ttl) {
    _ttl = ttl;
}

void LighthouseReckoning::setRetryDelays(unsigned long resendDelayMs, unsigned long rfcnWaitMs) {
    _dataResendDelayMs  = resendDelayMs;
    _rfcnWaitDelayMs    = rfcnWaitMs;
}

void LighthouseReckoning::setBeaconInterval(unsigned long intervalMs) {
    _beaconIntervalMs = intervalMs;
}

void LighthouseReckoning::setTxWatchdogTimeout(unsigned long timeoutMs) {
    _txMaxDurationMs = timeoutMs;
}

void LighthouseReckoning::setMaxLocalRetries(uint8_t maxRetries) {
    _maxLocalRetries = (maxRetries == 0) ? 1 : maxRetries;
}


// ================================================================
// Status and Routing Information
// ================================================================

lhr_node_role_t LighthouseReckoning::getRole() const {
    return _role;
}

uint8_t LighthouseReckoning::getHopsToHome() const {
    return _hopsToHome;
}

uint8_t LighthouseReckoning::getNeighborCount() const {
    return _neighborCount;
}


uint32_t LighthouseReckoning::getBestNeighborId() const {
    return _bestNeighborId;
}

uint32_t LighthouseReckoning::getSecondBestNeighborId() const {
    return _secondBestNeighborId;
}

bool LighthouseReckoning::isBusy() const {
    return _waitingForDataRes || _txInProgress;
}


// ================================================================
// Interrupt and Callback Handling
// ================================================================

void LighthouseReckoning::handleDio1Rise() {
    if (_txInProgress) {
        _txDone = true;
    }
    else if (!_working) {
        _packetReceived = true;
    }
}

void LighthouseReckoning::onDataReceived(LHR_DataReceivedCallback cb) {
    _dataReceivedCb = cb;
}


// ================================================================
// Data Transmission API
// ================================================================

lhr_err_t LighthouseReckoning::sendData(uint8_t* payload, size_t len) {
    return sendData(payload, len, _ttl);
}

lhr_err_t LighthouseReckoning::sendData(uint8_t* payload, size_t len, uint8_t ttl) {
    if (_radio == nullptr)                          return LHR_ERR_RADIO_NOT_INIT;
    if (payload == nullptr)                         return LHR_ERR_ARGS;
    if (len == 0)                                   return LHR_ERR_ARGS;
    if (ttl == 0)                                   return LHR_ERR_ZERO_TTL;
    if (_role == LHR_ROLE_HOME)                     return LHR_ERR_WRONG_ROLE;          // Home node does not send data
    if (_role == LHR_ROLE_NONE)                     return LHR_ERR_NOT_CONFIGURED;      // The Node has not been configured as either a home or relay node
    size_t headerLenForLimits = LHR_DATA_HEADER_LEN;
#if LHR_ENCRYPTION_SUPPORTED
    if (_encryptionEnabled) {
        headerLenForLimits = LHR_DATA_ENC_OFFSET_PAYLOAD;
    }
#endif
    size_t maxPayload = LORA_PHY_MAX_PACKET_SIZE - headerLenForLimits;
    if (len > maxPayload)                      return LHR_ERR_TOO_LONG;            // The payload is too long
    if (_waitingForDataRes)                         return LHR_ERR_BUSY;
    if (_bestNeighborId == LHR_FORBIDDEN_NODE_ID)   return LHR_ERR_NO_ROUTE;
    if (_useDutyCycleLimit) {
        _updateDutyCycleWindow();
        unsigned long airtimeMs = (unsigned long)(_radio->getTimeOnAir(headerLenForLimits + len) / 1000);
        if (_dutyCycleRemainingMs() == 0 || _dutyCycleRemainingMs() < airtimeMs) {
            return LHR_ERR_DUTY_CYCLE_EXHAUSTED;
        }
    }
    
    
    uint8_t buf[LORA_PHY_MAX_PACKET_SIZE];
    size_t totalLen;
#if LHR_ENCRYPTION_SUPPORTED
    if (_encryptionEnabled) {
        lhr_err_t err = _buildEncryptedDataPacket(buf, ttl, payload, len);
        if (err != LHR_OK) {
            LHR_DEBUG_PRINTLN("[DATA] Encrypted build failed, err=%d", err);
            return err;
        }
        totalLen = LHR_DATA_ENC_OFFSET_PAYLOAD + len;
    } else
#endif
    {
        totalLen = LHR_DATA_HEADER_LEN + len;
        _buildDataHeader(buf, ttl);
        memcpy(&buf[LHR_DATA_OFFSET_PAYLOAD], payload, len);
    }


    _storeForwardPacket(buf, totalLen);
    _lastPacketSenderId = LHR_FORBIDDEN_NODE_ID;            // Set to the forbidden Id (never a real neighbor) so that
                                                            // _forwardDataPacket()'s loop-avoidance check (_lastPacketSenderId ==
                                                            // _bestNeighborId) never accidentally triggers for a packet we
                                                            // originated ourselves — that check exists only to avoid sending a
                                                            // *forwarded* packet back to the neighbor that just sent it to us.
    _waitingForDataRes = true;   
    return LHR_OK;                                          // NOTE: LHR_OK here means "accepted into the send queue", not "sent".
                                                            // Actual transmission starts on the next update() call. This is
                                                            // documented in the sendData() header comment; kept as LHR_OK rather
                                                            // than introducing a separate status code since lhr_err_t is an error
                                                            // enum, not a status enum.                                          
}


// ================================================================
// Main Update Loop
// ================================================================

// Main non-blocking state machine.
// Handles RX, TX completion, retries, routing advertisements and beacons.
lhr_update_result_t LighthouseReckoning::update() {
    if (_role == LHR_ROLE_NONE) {
        // Node has not been configured as either Home or Relay yet.
        return LHR_UPDATE_NOT_CONFIGURED;
    } 

    if (_txInProgress) {
        LHR_DEBUG_PRINTLN("[UPDATE] TX in progress");
        if (_txDone) {
            LHR_DEBUG_PRINTLN("[UPDATE] TX finished");
            _radio->finishTransmit();     // clears IRQ flags, sets mode to standby
            _txInProgress = false;
            _txDone = false;
            _working = false;
            _startReceive();
            return LHR_UPDATE_TX_COMPLETE;
        }
        // Safety recovery: force the radio back into RX mode if a transmit operation
        // exceeds the maximum allowed duration and appears to be stuck.
        else if (LHR_MILLIS() - _txStartMs >= _txMaxDurationMs) { 
            LHR_DEBUG_PRINTLN("[TX] Watchdog: stuck in TX for >%lums, forcing recovery", _txMaxDurationMs);
            _radio->finishTransmit();
            _txInProgress = false;
            _txDone       = false;
            _working      = false;
            _startReceive();
            return LHR_UPDATE_TX_ERROR;
        }
        return LHR_UPDATE_IDLE;     // nothing else runs while a TX is in flight 
    }
    

    if (_packetReceived) {
        LHR_DEBUG_PRINTLN("[UPDATE] Packet received");
        return _checkLoraData();
    }
    
    if (_waitingForDataRes) {
        lhr_update_result_t retryResult = _confirmDataRes();
        if (retryResult != LHR_UPDATE_IDLE) return retryResult;
        if (_txInProgress) return LHR_UPDATE_TX_RESEND;
    }

    if (_ndatPending && !_waitingForDataRes && (LHR_MILLIS() - _ndatScheduledMs) >= _ndatJitterMs) {
        _ndatPending = false;
        LHR_DEBUG_PRINTLN("[UPDATE] Sending pending NDAT");
        if (_hopsToHome == LHR_HOPS_NO_ROUTE) {
            LHR_DEBUG_PRINTLN("[UPDATE] NDAT skipped (no route)");
            // No route to Home. Skip NDAT because it provides no useful
            // routing information and only wastes airtime.
            return LHR_UPDATE_IDLE;
        }
        #if LHR_ENCRYPTION_SUPPORTED
            if (_encryptionEnabled) {
                uint8_t buf[LHR_NDAT_ENC_LEN];
                lhr_err_t err = _buildEncryptedNDAT(buf);
                if (err != LHR_OK) {
                    LHR_DEBUG_PRINTLN("[NDAT] Encrypted build failed, err=%d", err);
                    return LHR_UPDATE_TX_ERROR;
                }
                if (_transmit(buf, LHR_NDAT_ENC_LEN) != LHR_TX_OK) {
                    return LHR_UPDATE_TX_ERROR;
                }
                _lastTriggeredNdatMs    = LHR_MILLIS();
                _lastAdvertisedHops     = _hopsToHome;              // Store the last advertised hop count to detect changes and advertise a new route faster when the current route changes.
                return LHR_UPDATE_TX_NDAT;
            }
        #endif // LHR_ENCRYPTION_SUPPORTED
        uint8_t buf[LHR_NDAT_LEN];
        _buildNDAT(buf);
        if (_transmit(buf, LHR_NDAT_LEN) != LHR_TX_OK) {
            return LHR_UPDATE_TX_ERROR;
        }
        _lastTriggeredNdatMs    = LHR_MILLIS();
        _lastAdvertisedHops     = _hopsToHome;              // Store the last advertised hop count to detect changes and advertise a new route faster when the current route changes.
        return LHR_UPDATE_TX_NDAT;
    }

    if (LHR_MILLIS() - _lastBeaconMs >= _beaconIntervalMs) {
        LHR_DEBUG_PRINTLN("[UPDATE] Beacon due");
        _beaconDue = true;
        // Deliberately not returning here: the beacon can still be sent
        // within this same update() call (see the _beaconDue check below).
        // Returning LHR_UPDATE_TX_BEACON_SCHEDULED here would delay actual
        // transmission by one extra update() cycle.
    }

    if (_beaconDue && !_waitingForDataRes) {
        LHR_DEBUG_PRINTLN("[UPDATE] Sending beacon");
        _pruneStaleNeighbors();
        _sendBeacon();
        _beaconDue = false;
        _lastBeaconMs = LHR_MILLIS();
        return LHR_UPDATE_TX_BEACON_SEND;
    }

    if (!_packetReceived && !_waitingForDataRes && !_beaconDue) {
        _working = false;
        return LHR_UPDATE_IDLE;
    }

    return LHR_UPDATE_IDLE;
}