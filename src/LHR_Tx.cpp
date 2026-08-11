/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_Tx.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Radio transmission primitives, beacon/RFCN/NDAT sending, DATA packet
 *   forwarding, and the DATA_RES retry/confirmation state machine.
 *
 * License:    MIT
 **************************************************************************/

#include "LighthouseReckoning.h"


// ================================================================
// Pseudorandom number generator
// ================================================================

// Generates a random value.
// Value in the range [min, max).
uint32_t LighthouseReckoning::_random(uint32_t min, uint32_t max) {
    if (max <= min) return min;
    _randState ^= _randState << 13;
    _randState ^= _randState >> 17;
    _randState ^= _randState <<  5;
    return min + (_randState % (max - min));
}


// ================================================================
// Radio primitives
// ================================================================

lhr_tx_result_t LighthouseReckoning::_transmit(const uint8_t* buf, size_t len) {
    if (_txInProgress) {
        return LHR_TX_RADIO_WORKING;
    }

    LHR_DEBUG_PRINT("[TX] Raw Packet (");
    LHR_DEBUG_PRINT("%u", (unsigned int)len);
    LHR_DEBUG_PRINTLN(" bytes):");

    for (size_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) {
            LHR_DEBUG_PRINT("0");
        }
        LHR_DEBUG_PRINT("%02X", buf[i]);
        LHR_DEBUG_PRINT(" ");
    }
    LHR_DEBUG_PRINTLN();

    if (_useDutyCycleLimit) {
        _updateDutyCycleWindow();
        unsigned long airtimeMs = (unsigned long)(_radio->getTimeOnAir(len) / 1000);
        if (_dutyCycleRemainingMs() == 0 || _dutyCycleRemainingMs() < airtimeMs) {
            LHR_DEBUG_PRINTLN("[DUTY] Over budget, skipping transmit");
            _startReceive();
            return LHR_TX_DUTY_CYCLE;
        }
    }
    _working        = true;
    // Channel Activity Detection (CAD): avoid transmitting over active traffic.
    int16_t cadState = _radio->scanChannel();
    if (cadState != RADIOLIB_CHANNEL_FREE) {
        LHR_DEBUG_PRINTLN("[TX] Channel busy, skipping this attempt");
        _working = false;
        _startReceive();
        return LHR_TX_CHANNEL_BUSY;   // existing retry timers will naturally try again later
    }
    _txInProgress   = true;
    _txStartMs      = LHR_MILLIS();
    int16_t state   = _radio->startTransmit(buf, len);
    _lastRadioError = state;
    if (state != RADIOLIB_ERR_NONE) {
        _txInProgress = false;
        _working = false;
        _startReceive();
        return LHR_TX_RADIO_ERROR;
    }
    _recordDutyCycleUsage(len);     // Append the used Airtime to the Limit

    return LHR_TX_OK;   // TX is now in flight; completion handled in update()
}

void LighthouseReckoning::_startReceive() {
    _radio->startReceive();
}


// ================================================================
// Beacon Handling
// ================================================================

void LighthouseReckoning::_sendBeacon() {
    if (_role == LHR_ROLE_HOME) {
        _sendNDAT();                // Home always advertises hops=0, anchoring the mesh
    }
    else if (_role == LHR_ROLE_NODE) {
        if (_hopsToHome != LHR_HOPS_NO_ROUTE) {
            _sendNDAT();        // We have a route — advertise it periodically
        }
        else {
            _sendRFCN();        // No route yet — actively ask neighbors instead of waiting
        }
    }
}

void LighthouseReckoning::_sendRFCN() {
    uint8_t buf[LHR_RFCN_LEN] = { LHR_START_BYTE, LHR_PKT_RFCN };
    _transmit(buf, LHR_RFCN_LEN);
    // RFCN is sent immediately (no jitter needed — it's a request, not an advertisement)
}

// _sendNDAT schedules a non-blocking jitter delay instead of calling
// delay() directly. The actual transmission happens in update() once
// _ndatJitterMs has elapsed.
void LighthouseReckoning::_sendNDAT() {
    _ndatScheduledMs = LHR_MILLIS();
    _ndatJitterMs    = _random(0, 501); // 0-500ms random delay to reduce
                                        // collision probability when multiple
                                        // neighbors respond to the same
                                        // beacon/RFCN simultaneously
    _ndatPending     = true;
}


// ================================================================
// ACK Handling
// ================================================================

void LighthouseReckoning::_sendDATARES(uint32_t receiverId, uint8_t seqNum) {
    uint8_t buf[LHR_DATA_RES_LEN];
    _buildDATARES(buf, receiverId, seqNum);
    _transmit(buf, LHR_DATA_RES_LEN);
}


// ================================================================
// Packet Forwarding
// ================================================================

void LighthouseReckoning::_forwardDataPacket() {
    if (_forwardLen < LHR_DATA_HEADER_LEN) {
        return;
    }

    // Set Sender Id (6..9) — this is OUR Id now (we're forwarding the hop),
    // distinct from _lastPacketSenderId below, which is the neighbor that
    // sent US this packet (used only for loop-avoidance, not written to the header).
    _forwardBuf[LHR_DATA_OFFSET_SENDER + 0] = (_deviceId >> 24) & 0xFF;
    _forwardBuf[LHR_DATA_OFFSET_SENDER + 1] = (_deviceId >> 16) & 0xFF;
    _forwardBuf[LHR_DATA_OFFSET_SENDER + 2] = (_deviceId >>  8) & 0xFF;
    _forwardBuf[LHR_DATA_OFFSET_SENDER + 3] = (_deviceId >>  0) & 0xFF;

    // Avoid forwarding-loops: if our best route toward Home happens to be
    // the same node that just sent us this packet, sending it straight
    // back would create an immediate loop. In that case we fall back to
    // the second-best neighbor instead — unless it's the only neighbor we
    // have, in which case there is no alternative and we send back anyway.

    // Set Receiver Id (10..13)
    if (_lastPacketSenderId != _bestNeighborId) {
      _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 0] = (_bestNeighborId >> 24) & 0xFF;
      _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 1] = (_bestNeighborId >> 16) & 0xFF;
      _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 2] = (_bestNeighborId >>  8) & 0xFF;
      _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 3] = (_bestNeighborId >>  0) & 0xFF;
    }
    else if (_lastPacketSenderId == _bestNeighborId) {
      if (_neighborCount == 1) {
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 0] = (_bestNeighborId >> 24) & 0xFF;
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 1] = (_bestNeighborId >> 16) & 0xFF;
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 2] = (_bestNeighborId >>  8) & 0xFF;
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 3] = (_bestNeighborId >>  0) & 0xFF;
      }
      else {
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 0] = (_secondBestNeighborId >> 24) & 0xFF;
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 1] = (_secondBestNeighborId >> 16) & 0xFF;
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 2] = (_secondBestNeighborId >>  8) & 0xFF;
        _forwardBuf[LHR_DATA_OFFSET_RECEIVER + 3] = (_secondBestNeighborId >>  0) & 0xFF;
      }
    }

    _pendingAckSeqNum = _forwardBuf[LHR_DATA_OFFSET_SEQ_NUM];

    // Only transmit if a valid next-hop neighbor is available.
    // LHR_FORBIDDEN_NODE_ID (0x00000000) is reserved as the invalid node Id.
    if (_bestNeighborId != LHR_FORBIDDEN_NODE_ID) {  
        _transmit(_forwardBuf, _forwardLen);
    }
    else {
        LHR_DEBUG_PRINTLN("[FWD] Not forwarding to 0x%08X ttl=%d", _bestNeighborId, _forwardBuf[LHR_DATA_OFFSET_TTL]);
    }
    
}   


// ================================================================
// Retry / confirmation state machine
// ================================================================

// Retry cycle:
// 1. Initial DATA transmission starts the ACK timer.
// 2. After _dataResendDelayMs without DATA_RES, resend the packet.
// 3. After another _dataResendDelayMs*2 timeout, send RFCN to rediscover routes.
// 4. Wait for NDAT responses, then restart the cycle.
// 5. Stop after _maxLocalRetries attempts.
lhr_update_result_t LighthouseReckoning::_confirmDataRes() {
    unsigned long now = LHR_MILLIS();
    // --- Initial send ---
    if (!_dataResCycleActive) {
        LHR_DEBUG_PRINTLN("[RETRY] Initial send");
        _localRetryCount++;
        if (_localRetryCount > _maxLocalRetries) {
            LHR_DEBUG_PRINTLN("[RETRY] Max local retries reached, dropping packet");
            _resetDataResState();
            return LHR_UPDATE_TX_DATA_FAILED;
        }
        _dataResCycleActive = true;
        _dataResStartTime = now;
        _rfcnSendTime = 0;
        _dataResend = false;
        _requestedRFCN = false;
        _forwardDataPacket();
    }

    // --- Timeout: send RFCN ---
    if (!_requestedRFCN && (now - _dataResStartTime >= _dataResendDelayMs * 2)) {
        LHR_DEBUG_PRINTLN("[RETRY] Timeout, sending RFCN");
        _resetNeighborTable();
        _sendRFCN();

        _requestedRFCN = true;
        _rfcnSendTime = now;
        _dataResend = false; // ensure the resend phase runs again on the next cycle
    }

    // --- Non-blocking RFCN Wait & Resend ---
    if (_requestedRFCN && (now - _rfcnSendTime >= _rfcnWaitDelayMs)) {
        LHR_DEBUG_PRINTLN("[RETRY] RFCN wait elapsed, resetting retry cycle");

        // _forwardDataPacket() is intentionally not called here — resetting
        // state below restarts the send/wait cycle on the next update()

        // Reset for next cycle
        _dataResCycleActive = false;
        _dataResStartTime = 0;
        _rfcnSendTime = 0;
        _dataResend = false;
        _requestedRFCN = false;
        return LHR_UPDATE_IDLE;
    }

    // --- First resend phase (before RFCN) ---
    if (!_dataResend && !_requestedRFCN && (now - _dataResStartTime >= _dataResendDelayMs)) {
        LHR_DEBUG_PRINTLN("[RETRY] Resend attempt");
        _forwardDataPacket();
        _dataResend = true;
    }
    return LHR_UPDATE_IDLE;
}


// ================================================================
// State Management
// ================================================================

// Clears DATA_RES retry state.
// Called after successful ACK reception or after giving up retries.
void LighthouseReckoning::_resetDataResState() {
    _dataResStartTime   = 0;
    _rfcnSendTime       = 0;
    _localRetryCount    = 0;
    _dataResend         = false;
    _requestedRFCN      = false;
    _waitingForDataRes  = false;
    _dataResCycleActive = false;
}


// ================================================================
// Experimental Features
// ================================================================

// Hop metadata TLV layout appended to forwarded DATA packets (test/debug only):
//   [0]     Tag        = LHR_HOP_META_TAG (0x70)
//   [1]     Value len  = LHR_HOP_META_VLEN (6)
//   [2..5]  Relay device Id (little-endian, unlike the big-endian Ids
//           used elsewhere in the header — historical inconsistency,
//           not intentional)
//   [6]     RSSI magnitude (abs value, clamped to 0-255, sign implied negative)
//   [7]     SNR (signed, clamped to int8 range)
void LighthouseReckoning::_appendHopMetadata(float rssi, float snr) {
    if (!_routingDataEnabled) return;

    if (_forwardLen + LHR_HOP_META_LEN > LORA_PHY_MAX_PACKET_SIZE) {
        LHR_DEBUG_PRINTLN("[FWD] No space for hop metadata, skipped");
        return;
    }

    float rssiAbs = -rssi;
    if (rssiAbs <   0) rssiAbs = 0;
    if (rssiAbs > 255) rssiAbs = 255;
    uint8_t rssiMag   = (uint8_t)rssiAbs;
    int8_t  snrClamped = (int8_t)(snr < -128 ? -128 : (snr > 127 ? 127 : snr));

    uint8_t* p = &_forwardBuf[_forwardLen];
    p[0] = LHR_HOP_META_TAG;
    p[1] = LHR_HOP_META_VLEN;
    p[2] = (_deviceId >>  0) & 0xFF;
    p[3] = (_deviceId >>  8) & 0xFF;
    p[4] = (_deviceId >> 16) & 0xFF;
    p[5] = (_deviceId >> 24) & 0xFF;
    p[6] = rssiMag;
    p[7] = (uint8_t)snrClamped;

    _forwardLen += LHR_HOP_META_LEN;
}