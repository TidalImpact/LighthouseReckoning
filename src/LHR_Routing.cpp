/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_Routing.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Packet header construction, neighbor table management, best/second-
 *   best route selection, and routing advertisement (NDAT) triggering.
 *
 * License:    MIT
 **************************************************************************/

#include "LighthouseReckoning.h"


// ================================================================
// Packet Construction — Unencrypted
// ================================================================

void LighthouseReckoning::_buildDataHeader(uint8_t* buf, uint8_t ttl) {
    buf[LHR_DATA_OFFSET_MAGIC]          = LHR_START_BYTE; 
    buf[LHR_DATA_OFFSET_TYPE]           = LHR_PKT_DATA;

    buf[LHR_DATA_OFFSET_SOURCE + 0]     = (_deviceId >> 24) & 0xFF;
    buf[LHR_DATA_OFFSET_SOURCE + 1]     = (_deviceId >> 16) & 0xFF;
    buf[LHR_DATA_OFFSET_SOURCE + 2]     = (_deviceId >> 8 ) & 0xFF;
    buf[LHR_DATA_OFFSET_SOURCE + 3]     = (_deviceId >> 0 ) & 0xFF;

    buf[LHR_DATA_OFFSET_SENDER + 0]     = (_deviceId >> 24) & 0xFF;
    buf[LHR_DATA_OFFSET_SENDER + 1]     = (_deviceId >> 16) & 0xFF;
    buf[LHR_DATA_OFFSET_SENDER + 2]     = (_deviceId >> 8 ) & 0xFF;
    buf[LHR_DATA_OFFSET_SENDER + 3]     = (_deviceId >> 0 ) & 0xFF;

    buf[LHR_DATA_OFFSET_RECEIVER + 0]   = (_bestNeighborId >> 24) & 0xFF;
    buf[LHR_DATA_OFFSET_RECEIVER + 1]   = (_bestNeighborId >> 16) & 0xFF;
    buf[LHR_DATA_OFFSET_RECEIVER + 2]   = (_bestNeighborId >> 8 ) & 0xFF;
    buf[LHR_DATA_OFFSET_RECEIVER + 3]   = (_bestNeighborId >> 0 ) & 0xFF;

    _pendingAckSeqNum = _currentSeqNum;
    buf[LHR_DATA_OFFSET_SEQ_NUM]        = _currentSeqNum++;     // _pendingAckSeqNum records which seq number we expect the DATA_RES
                                                                // to confirm. Post-increment ensures the packet gets the current
                                                                // value while _currentSeqNum is already advanced for the next call.
    buf[LHR_DATA_OFFSET_TTL]            = ttl;
}

void LighthouseReckoning::_buildNDAT(uint8_t* buf) {
    buf[LHR_NDAT_OFFSET_MAGIC]      = LHR_START_BYTE;
    buf[LHR_NDAT_OFFSET_TYPE]       = LHR_PKT_NDAT;
    buf[LHR_NDAT_OFFSET_SENDER + 0] = (_deviceId >> 24) & 0xFF;
    buf[LHR_NDAT_OFFSET_SENDER + 1] = (_deviceId >> 16) & 0xFF;
    buf[LHR_NDAT_OFFSET_SENDER + 2] = (_deviceId >>  8) & 0xFF;
    buf[LHR_NDAT_OFFSET_SENDER + 3] = (_deviceId >>  0) & 0xFF;
    buf[LHR_NDAT_OFFSET_HOPS]       = _hopsToHome;
}

void LighthouseReckoning::_buildDATARES(uint8_t* buf, uint32_t receiverId, uint8_t seqNum) {
    buf[LHR_DATARES_OFFSET_MAGIC] = LHR_START_BYTE;
    buf[LHR_DATARES_OFFSET_TYPE] = LHR_PKT_DATA_RES;

    buf[LHR_DATARES_OFFSET_SENDER + 0]      = (_deviceId  >> 24) & 0xFF;
    buf[LHR_DATARES_OFFSET_SENDER + 1]      = (_deviceId  >> 16) & 0xFF;
    buf[LHR_DATARES_OFFSET_SENDER + 2]      = (_deviceId  >>  8) & 0xFF;
    buf[LHR_DATARES_OFFSET_SENDER + 3]      = (_deviceId  >>  0) & 0xFF;

    buf[LHR_DATARES_OFFSET_RECEIVER + 0]    = (receiverId >> 24) & 0xFF;
    buf[LHR_DATARES_OFFSET_RECEIVER + 1]    = (receiverId >> 16) & 0xFF;
    buf[LHR_DATARES_OFFSET_RECEIVER + 2]    = (receiverId >>  8) & 0xFF;
    buf[LHR_DATARES_OFFSET_RECEIVER + 3]    = (receiverId >>  0) & 0xFF;

    buf[LHR_DATARES_OFFSET_SEQ_NUM] = seqNum;
}


// ================================================================
// Packet Construction — Encrypted
// ================================================================

#if LHR_ENCRYPTION_SUPPORTED

lhr_err_t LighthouseReckoning::_buildEncryptedDataPacket(uint8_t* buf, uint32_t receiverId, uint32_t sourceId, uint8_t ttl, uint8_t seqNum, const uint8_t* payload, size_t len) {
    if (len > LHR_MAX_PAYLOAD_ENC) {
        return LHR_ERR_TOO_LONG;
    }

    uint8_t aad[10];
    aad[0] = LHR_START_BYTE;
    aad[1] = LHR_PKT_DATA;

    aad[2] = (_deviceId >> 24) & 0xFF;
    aad[3] = (_deviceId >> 16) & 0xFF;
    aad[4] = (_deviceId >>  8) & 0xFF;
    aad[5] = (_deviceId >>  0) & 0xFF;

    aad[6] = (receiverId  >> 24) & 0xFF;
    aad[7] = (receiverId  >> 16) & 0xFF;
    aad[8] = (receiverId  >>  8) & 0xFF;
    aad[9] = (receiverId  >>  0) & 0xFF;


    uint8_t plaintext[LHR_DATA_ENC_HEADER_FIELDS_LEN + LHR_MAX_PAYLOAD_ENC];
    // Packet Sourc Id
    plaintext[0] = (sourceId >> 24) & 0xFF;
    plaintext[1] = (sourceId >> 16) & 0xFF;
    plaintext[2] = (sourceId >>  8) & 0xFF;
    plaintext[3] = (sourceId >>  0) & 0xFF;

    _pendingAckSeqNum = seqNum;
    plaintext[4] = seqNum;

    plaintext[5] = ttl;

    memcpy(&plaintext[LHR_DATA_ENC_HEADER_FIELDS_LEN], payload, len);
    size_t plainLen = LHR_DATA_ENC_HEADER_FIELDS_LEN  + len;

    uint8_t nonce[LHR_NONCE_LEN];
    uint32_t wireCounter = 0;
    lhr_err_t err = _buildNonce(nonce, &wireCounter);
    if (err != LHR_OK) {
        return err;
    }

    uint8_t temp[LHR_DATA_ENC_HEADER_FIELDS_LEN + LHR_MAX_PAYLOAD_ENC];
    uint8_t tag[LHR_MIC_LEN];
    err = _ccmEncrypt(nonce, aad, sizeof(aad), plaintext, plainLen, &temp[0], tag);
    if (err != LHR_OK) {
        return err;
    }

    buf[LHR_DATA_ENC_OFFSET_MAGIC]          = LHR_START_BYTE;
    buf[LHR_DATA_ENC_OFFSET_TYPE]           = LHR_PKT_DATA;

    memcpy(&buf[LHR_DATA_ENC_OFFSET_SOURCE], &temp[0], 4);

    memcpy(&buf[LHR_DATA_ENC_OFFSET_SENDER], &aad[2], 4);

    memcpy(&buf[LHR_DATA_ENC_OFFSET_RECEIVER], &aad[6], 4);

    memcpy(&buf[LHR_DATA_ENC_OFFSET_SEQ_NUM], &temp[4], 1);
    memcpy(&buf[LHR_DATA_ENC_OFFSET_TTL], &temp[5], 1);

    memcpy(&buf[LHR_DATA_ENC_OFFSET_PAYLOAD], &temp[LHR_DATA_ENC_HEADER_FIELDS_LEN], len);


    uint32_t maskedCounter = wireCounter & 0x00FFFFFFUL;
    buf[LHR_DATA_ENC_OFFSET_WIRECOUNTER + 0] = (maskedCounter >> 16) & 0xFF;
    buf[LHR_DATA_ENC_OFFSET_WIRECOUNTER + 1] = (maskedCounter >>  8) & 0xFF;
    buf[LHR_DATA_ENC_OFFSET_WIRECOUNTER + 2] = (maskedCounter >>  0) & 0xFF;

    memcpy(&buf[LHR_DATA_ENC_OFFSET_MIC], tag, LHR_MIC_LEN);

    return LHR_OK;
}

lhr_err_t LighthouseReckoning::_buildEncryptedNDAT(uint8_t* buf) {
    uint8_t aad[6];
    aad[0] = LHR_START_BYTE;
    aad[1] = LHR_PKT_NDAT;
    aad[2] = (_deviceId >> 24) & 0xFF;
    aad[3] = (_deviceId >> 16) & 0xFF;
    aad[4] = (_deviceId >>  8) & 0xFF;
    aad[5] = (_deviceId >>  0) & 0xFF;

    uint8_t plaintext = _hopsToHome;

    uint8_t nonce[LHR_NONCE_LEN];
    uint32_t wireCounter = 0;
    lhr_err_t err = _buildNonce(nonce, &wireCounter);
    if (err != LHR_OK) {
        return err;
    }

    uint8_t tag[LHR_MIC_LEN];
    err = _ccmEncrypt(nonce, aad, sizeof(aad), &plaintext, 1, &buf[LHR_NDAT_ENC_OFFSET_HOPS], tag);
    if (err != LHR_OK) {
        return err;
    }

    buf[LHR_NDAT_ENC_OFFSET_MAGIC] = LHR_START_BYTE;
    buf[LHR_NDAT_ENC_OFFSET_TYPE]  = LHR_PKT_NDAT;
    memcpy(&buf[LHR_NDAT_ENC_OFFSET_SENDER], &aad[2], 4);

    uint32_t maskedCounter = wireCounter & 0x00FFFFFFUL;
    buf[LHR_NDAT_ENC_OFFSET_WIRECOUNTER + 0] = (maskedCounter >> 16) & 0xFF;
    buf[LHR_NDAT_ENC_OFFSET_WIRECOUNTER + 1] = (maskedCounter >>  8) & 0xFF;
    buf[LHR_NDAT_ENC_OFFSET_WIRECOUNTER + 2] = (maskedCounter >>  0) & 0xFF;

    memcpy(&buf[LHR_NDAT_ENC_OFFSET_MIC], tag, LHR_MIC_LEN);

    return LHR_OK;
}

lhr_err_t LighthouseReckoning::_buildEncryptedRFCN(uint8_t* buf) {
    uint8_t aad[6];
    aad[0] = LHR_START_BYTE;
    aad[1] = LHR_PKT_RFCN;
    aad[2] = (_deviceId >> 24) & 0xFF;
    aad[3] = (_deviceId >> 16) & 0xFF;
    aad[4] = (_deviceId >>  8) & 0xFF;
    aad[5] = (_deviceId >>  0) & 0xFF;

    uint8_t nonce[LHR_NONCE_LEN];
    uint32_t wireCounter = 0;
    lhr_err_t err = _buildNonce(nonce, &wireCounter);
    if (err != LHR_OK) {
        return err;
    }

    uint8_t tag[LHR_MIC_LEN];
    err = _ccmEncrypt(nonce, aad, sizeof(aad), nullptr, 0, nullptr, tag);
    if (err != LHR_OK) {
        return err;
    }

    buf[LHR_RFCN_ENC_OFFSET_MAGIC] = LHR_START_BYTE;
    buf[LHR_RFCN_ENC_OFFSET_TYPE]  = LHR_PKT_RFCN;
    memcpy(&buf[LHR_RFCN_ENC_OFFSET_SENDER], &aad[2], 4);

    uint32_t maskedCounter = wireCounter & 0x00FFFFFFUL;
    buf[LHR_RFCN_ENC_OFFSET_WIRECOUNTER + 0] = (maskedCounter >> 16) & 0xFF;
    buf[LHR_RFCN_ENC_OFFSET_WIRECOUNTER + 1] = (maskedCounter >>  8) & 0xFF;
    buf[LHR_RFCN_ENC_OFFSET_WIRECOUNTER + 2] = (maskedCounter >>  0) & 0xFF;

    memcpy(&buf[LHR_RFCN_ENC_OFFSET_MIC], tag, LHR_MIC_LEN);

    return LHR_OK;
}

lhr_err_t LighthouseReckoning::_buildEncryptedDATARES(uint8_t* buf, uint32_t receiverId, uint8_t seqNum) {
    uint8_t aad[10];
    aad[0] = LHR_START_BYTE;
    aad[1] = LHR_PKT_DATA_RES;
    aad[2] = (_deviceId  >> 24) & 0xFF;
    aad[3] = (_deviceId  >> 16) & 0xFF;
    aad[4] = (_deviceId  >>  8) & 0xFF;
    aad[5] = (_deviceId  >>  0) & 0xFF;
    aad[6] = (receiverId >> 24) & 0xFF;
    aad[7] = (receiverId >> 16) & 0xFF;
    aad[8] = (receiverId >>  8) & 0xFF;
    aad[9] = (receiverId >>  0) & 0xFF;

    uint8_t plaintext = seqNum;

    uint8_t nonce[LHR_NONCE_LEN];
    uint32_t wireCounter = 0;
    lhr_err_t err = _buildNonce(nonce, &wireCounter);
    if (err != LHR_OK) {
        return err;
    }

    uint8_t tag[LHR_MIC_LEN];
    err = _ccmEncrypt(nonce, aad, sizeof(aad), &plaintext, 1, &buf[LHR_DATARES_ENC_OFFSET_SEQ_NUM], tag);
    if (err != LHR_OK) {
        return err;
    }

    buf[LHR_DATARES_ENC_OFFSET_MAGIC] = LHR_START_BYTE;
    buf[LHR_DATARES_ENC_OFFSET_TYPE]  = LHR_PKT_DATA_RES;
    memcpy(&buf[LHR_DATARES_ENC_OFFSET_SENDER],   &aad[2], 4);
    memcpy(&buf[LHR_DATARES_ENC_OFFSET_RECEIVER], &aad[6], 4);

    uint32_t maskedCounter = wireCounter & 0x00FFFFFFUL;
    buf[LHR_DATARES_ENC_OFFSET_WIRECOUNTER + 0] = (maskedCounter >> 16) & 0xFF;
    buf[LHR_DATARES_ENC_OFFSET_WIRECOUNTER + 1] = (maskedCounter >>  8) & 0xFF;
    buf[LHR_DATARES_ENC_OFFSET_WIRECOUNTER + 2] = (maskedCounter >>  0) & 0xFF;

    memcpy(&buf[LHR_DATARES_ENC_OFFSET_MIC], tag, LHR_MIC_LEN);

    return LHR_OK;
}

#endif // LHR_ENCRYPTION_SUPPORTED

// ================================================================
// Forward / Retry Buffer Management
// ================================================================

void LighthouseReckoning::_storeForwardPacket(uint8_t* buf, size_t len) {
    // buf typically lives on the caller's stack (e.g. sendData()'s local
    // array) and would go out of scope before resends/retries happen.
    // Copying into the persistent _forwardBuf member keeps the packet
    // alive across the full retry state machine in _confirmDataRes().
    memcpy(_forwardBuf, buf, len);
    _forwardLen = len;
}


// ================================================================
// Neighbor Table Management
// ================================================================

int LighthouseReckoning::_findNeighbor(uint32_t Id) {
    for (int i = 0; i < _neighborCount; i++) {
        if (_neighbors[i].Id == Id) {
            return i;
        }
    }
    return -1;
}

lhr_err_t LighthouseReckoning::_updateNeighbor(uint32_t Id, uint8_t hops, float rssi) {
    if (hops < _minimumAcceptedHops) {
        LHR_DEBUG_PRINTLN("[ROUTE] minimumAcceptedHops: rejecting 0x%08X (hops=%d < min=%d)", Id, hops, _minimumAcceptedHops);
        return LHR_OK;
    }
    int index = _findNeighbor(Id);

    if (index >= 0) {
        _neighbors[index].hops_to_home = hops;
        _neighbors[index].rssi         = rssi;
        _neighbors[index].last_seen_ms = LHR_MILLIS();
        LHR_DEBUG_PRINTLN("[ROUTE] Updated neighbor 0x%08X hops=%d rssi=%.1f", Id, hops, rssi);
    }
    else if (_neighborCount >= LHR_MAX_NEIGHBORS) {
        // Table full — find the current worst entry
        int worstIdx = 0;
        for (int i = 1; i < _neighborCount; i++) {
            bool worse = (_neighbors[i].hops_to_home > _neighbors[worstIdx].hops_to_home) ||
                         (_neighbors[i].hops_to_home == _neighbors[worstIdx].hops_to_home &&
                          _neighbors[i].rssi < _neighbors[worstIdx].rssi);
            if (worse) worstIdx = i;
        }

        bool newIsBetter = (hops < _neighbors[worstIdx].hops_to_home) ||
                           (hops == _neighbors[worstIdx].hops_to_home &&
                            rssi > _neighbors[worstIdx].rssi);

        if (!newIsBetter) {
            LHR_DEBUG_PRINTLN("[ROUTE] Table full, 0x%08X not better than worst, dropping", Id);
            return LHR_ERR_FULL_NEIGHBORS;
        }

        LHR_DEBUG_PRINTLN("[ROUTE] Evicting 0x%08X for 0x%08X", _neighbors[worstIdx].Id, Id);
        _neighbors[worstIdx].Id           = Id;
        _neighbors[worstIdx].hops_to_home = hops;
        _neighbors[worstIdx].rssi         = rssi;
        _neighbors[worstIdx].last_seen_ms = LHR_MILLIS();
    }
    else {
        _neighbors[_neighborCount].Id           = Id;
        _neighbors[_neighborCount].hops_to_home = hops;
        _neighbors[_neighborCount].rssi         = rssi;
        _neighbors[_neighborCount].last_seen_ms = LHR_MILLIS();
        LHR_DEBUG_PRINTLN("[ROUTE] New neighbor added 0x%08X hops=%d rssi=%.1f", Id, hops, rssi);
        _neighborCount++;
    }

    _findBestNeighbor();
    _sendNdatIfNeeded();
    return LHR_OK;
}

void LighthouseReckoning::_pruneStaleNeighbors() {
    unsigned long now = LHR_MILLIS();
    unsigned long timeout = (unsigned long)_beaconIntervalMs * LHR_NEIGHBOR_TIMEOUT_BEACONS;
    bool removed = false;

    for (int i = 0; i < _neighborCount; i++) {
        if (now - _neighbors[i].last_seen_ms >= timeout) {
            LHR_DEBUG_PRINTLN("[ROUTE] Neighbor 0x%08X timed out, removing", _neighbors[i].Id);
            _neighbors[i] = _neighbors[_neighborCount - 1];  // swap-remove
            _neighborCount--;
            i--;                        // Re-check this index because the last entry was moved into this position.
            removed = true;
        }
    }

    if (removed) {
        _findBestNeighbor();
        _sendNdatIfNeeded();
    }
}

void LighthouseReckoning::_resetNeighborTable() {
    // Clearing fields here is defensive only — _neighborCount = 0 below
    // already makes all entries logically invalid/unreachable. Helps
    // avoid confusion when inspecting raw memory during debugging.
    for(int i = 0; i < _neighborCount; i++) {
        _neighbors[i].Id             = 0;
        _neighbors[i].hops_to_home   = LHR_HOPS_NO_ROUTE;
        _neighbors[i].rssi           = LHR_RSSI_UNKNOWN;
    }
    _neighborCount = 0;
    _hopsToHome = LHR_HOPS_NO_ROUTE;
    _bestNeighborId = 0;
    _secondBestNeighborId = 0;
}


// ================================================================
// Routing Selection
// ================================================================

int LighthouseReckoning::_findBestNeighbor() {

    if (_neighborCount == 0) {
        _bestNeighborId       = 0;
        _secondBestNeighborId = 0;
        _hopsToHome           = LHR_HOPS_NO_ROUTE;
        return -1;
    }

    int bestIdx   = -1;
    int secondIdx = -1;

    auto isBetter = [this](int a, int b) -> bool {
        if (_neighbors[a].hops_to_home != _neighbors[b].hops_to_home) {
            return _neighbors[a].hops_to_home < _neighbors[b].hops_to_home;
        }

        return _neighbors[a].rssi > _neighbors[b].rssi;
    };

    // Single-pass selection of best and second-best neighbor (by hop count,
    // then RSSI as tiebreaker). Whenever a new best is found, the previous
    // best is "demoted" to second-best candidate (but only if it's actually
    // better than the current second-best) — this avoids a second loop over
    // the neighbor table just to find the runner-up.
    for (int i = 0; i < _neighborCount; i++) {

        if (_neighbors[i].hops_to_home >= LHR_HOPS_NO_ROUTE) {
            continue;
        }

        if (bestIdx == -1) {
            bestIdx = i;
        }
        else if (isBetter(i, bestIdx)) {
            if (secondIdx == -1 || isBetter(bestIdx, secondIdx)) {
                secondIdx = bestIdx;
            }

            bestIdx = i;
        }
        else if (secondIdx == -1 || isBetter(i, secondIdx)) {
            secondIdx = i;
        }
    }

    if (bestIdx == -1) {
        _bestNeighborId       = 0;
        _secondBestNeighborId = 0;
        _hopsToHome           = LHR_HOPS_NO_ROUTE;
        return -1;
    }

    _bestNeighborId       = _neighbors[bestIdx].Id;
    _hopsToHome           = _neighbors[bestIdx].hops_to_home + 1;       // _hopsToHome = min(_neighbors[bestIdx].hops_to_home + 1, (int)LHR_HOPS_NO_ROUTE - 1); maybe later

    // Falls back to bestNeighborId when there's no distinct second-best
    // (e.g. only one neighbor known). This mirrors the special-case
    // handling in _forwardDataPacket()'s loop-avoidance logic.
    _secondBestNeighborId = (secondIdx != -1)
                            ? _neighbors[secondIdx].Id
                            : _bestNeighborId;

    LHR_DEBUG_PRINTLN("[ROUTE] Best: 0x%08X hops=%d rssi=%.1f", 
        _bestNeighborId, _hopsToHome, _neighbors[bestIdx].rssi);
    LHR_DEBUG_PRINTLN("[ROUTE] Second best: 0x%08X hops=%d rssi=%.1f", 
        _secondBestNeighborId, 
        secondIdx != -1 ? _neighbors[secondIdx].hops_to_home + 1 : _hopsToHome,
        secondIdx != -1 ? _neighbors[secondIdx].rssi : _neighbors[bestIdx].rssi);


    return bestIdx;
}


// ================================================================
// Debug Utilities
// ================================================================

void LighthouseReckoning::printNeighborTable() {
#ifdef LHR_DEBUG
    // Debug-only utility for manual inspection during development.
    // Compiled out entirely unless LHR_DEBUG is defined — no runtime cost in release builds.
    int best = _findBestNeighbor();

    LHR_DEBUG_PRINTLN("---- Neighbor Table ----");
    for (int i = 0; i < _neighborCount; i++) {
        LHR_DEBUG_PRINTLN("%d: ID=0x%08X | Hops=%d | RSSI=%.1f%s",
            i, _neighbors[i].Id, _neighbors[i].hops_to_home, _neighbors[i].rssi,
            (i == best) ? "  <-- BEST" : "");
    }
    LHR_DEBUG_PRINTLN("My hops to home = %d", _hopsToHome);
    LHR_DEBUG_PRINTLN("Best: 0x%08X  Second best: 0x%08X", _bestNeighborId, _secondBestNeighborId);
    LHR_DEBUG_PRINTLN("------------------------");
#endif
}


// ================================================================
// Routing Advertisements
// ================================================================

void LighthouseReckoning::_sendNdatIfNeeded() {
    if (_role != LHR_ROLE_NODE) return;                                             // Home nodes always have a fixed hopsToHome value of 0 and can´t change
    if (_hopsToHome == _lastAdvertisedHops) return;                                 // Only advertise when the route changed.
    if (LHR_MILLIS() - _lastTriggeredNdatMs < _minTriggeredUpdateGapMs) return;     // Limit triggered NDAT updates to avoid unnecessary airtime usage.
    if (_neighborCount == 0) return;                                                // No neighbors means no useful route information to advertise.
    if (_waitingForDataRes) return;                                                 // Do not interfere with an active DATA retry cycle.

    _sendNDAT();
}
