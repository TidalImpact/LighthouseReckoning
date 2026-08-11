/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_Rx.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Platform:   Any (Arduino / RP2040 / ESP32)
 * Description:
 *   Main RX dispatcher and packet handlers for DATA, NDAT, RFCN, and
 *   DATA_RES packet types, including duplicate detection.
 *
 * License:    MIT
 **************************************************************************/

#include "LighthouseReckoning.h"


// ================================================================
// Main RX dispatcher
// ================================================================

lhr_update_result_t LighthouseReckoning::_checkLoraData() {
    if (!_packetReceived) {
        _startReceive();
        return LHR_UPDATE_IDLE;
    }

    _packetReceived = false;

    uint8_t buf[LORA_PHY_MAX_PACKET_SIZE];
    size_t  len   = _radio->getPacketLength();

    if (len < LHR_MIN_VALID_PACKET_SIZE || len > sizeof(buf)) {
        _startReceive();
        return LHR_UPDATE_IDLE;
    }

    int     state = _radio->readData(buf, len);

    if (state != RADIOLIB_ERR_NONE) {
        LHR_DEBUG_PRINTLN("[RX] Radio read error: %d", state);
        _startReceive();
        return LHR_UPDATE_RX_ERROR;
    }

    LHR_DEBUG_PRINTLN("[RX] Packet received, len=%d", len);

    if (buf[0] != LHR_START_BYTE) {
        LHR_DEBUG_PRINTLN("[RX] Invalid start byte: 0x%02X", buf[0]);
        _startReceive();
        return LHR_UPDATE_IDLE;
    }

    uint8_t type = buf[1];

    switch (type) {

        case LHR_PKT_DATA: {
            if (len < LHR_DATA_HEADER_LEN) {
                LHR_DEBUG_PRINTLN("[RX] DATA too short: %d", len);
                break;
            }
            if (_waitingForDataRes) {
                LHR_DEBUG_PRINTLN("[RX] DATA received but dropped, waiting for DATA_RES");
                break;
            }
            float rssi = _radio->getRSSI();
            float snr  = _radio->getSNR();
            _handleDATA(buf, len, rssi, snr);
            //_startReceive();  // I guess that needs to be removed as it could interfere with the SendDataRes  which is called from _handleDATA() above
            return LHR_UPDATE_RX_DATA;
        }

        case LHR_PKT_NDAT: {
            if (len != LHR_NDAT_LEN) {
                LHR_DEBUG_PRINTLN("[RX] NDAT wrong length: %d", len);
                break;
            }
            if (_role == LHR_ROLE_HOME) {
                break;   // Home does not maintain a neighbor table
            }
            float rssi = _radio->getRSSI();
            _handleNDAT(buf, len, rssi);
            _startReceive();
            return LHR_UPDATE_RX_NDAT;
        }

        case LHR_PKT_RFCN: {
            if (len != LHR_RFCN_LEN) {
                LHR_DEBUG_PRINTLN("[RX] RFCN wrong length: %d", len);
                break;
            }
            _handleRFCN();
            _startReceive();
            return LHR_UPDATE_RX_RFCN;
        }

        case LHR_PKT_DATA_RES: {
            if (len != LHR_DATA_RES_LEN) {
                LHR_DEBUG_PRINTLN("[RX] DATA_RES wrong length: %d", len);
                break;
            }
            if (_waitingForDataRes) {
                bool confirmed = _handleDATARES(buf, len);
                _startReceive();
                return confirmed ? LHR_UPDATE_TX_DATA_CONFIRMED : LHR_UPDATE_RX_DATARES;
            }
            break;
        }

        default: {
            LHR_DEBUG_PRINTLN("[RX] Unknown packet type: 0x%02X", type);
            break;
        }
    }

    _startReceive();
    return LHR_UPDATE_IDLE;
}


// ================================================================
// RX Helper Functions
// ================================================================

bool LighthouseReckoning::_isDuplicateData(uint32_t source, uint8_t seq) {
    // Duplicate detection uses a fixed-size ring buffer (LHR_SEEN_CACHE_SIZE
    // entries). Only the most recently seen (source, seq) pairs are
    // remembered — older entries are silently overwritten. Sufficient to
    // catch retransmissions within a normal retry window, not a guarantee
    // against long-term duplicate delivery.
    for (auto& e : _seenPackets) {
        if (e.valid && e.source == source && e.seq == seq) return true;
    }
    _seenPackets[_seenIndex] = { source, seq, true };
    _seenIndex = (_seenIndex + 1) % LHR_SEEN_CACHE_SIZE;
    return false;
}


// ================================================================
// Packet Handlers
// ================================================================

void LighthouseReckoning::_handleNDAT(uint8_t* buf, size_t len, float rssi) {
    uint32_t senderId =
        ((uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 0] << 24) |
        ((uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 1] << 16) |
        ((uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 2] <<  8) |
         (uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 3];

    uint8_t hops = buf[LHR_NDAT_OFFSET_HOPS];

    LHR_DEBUG_PRINTLN("[NDAT] From 0x%08X hops=%d rssi=%.1f", senderId, hops, rssi);

    _updateNeighbor(senderId, hops, rssi);
}


void LighthouseReckoning::_handleRFCN() {
    if (_ndatPending) {
        LHR_DEBUG_PRINTLN("[RFCN] Response already pending, ignoring duplicate");
        return;
    }
    LHR_DEBUG_PRINTLN("[RFCN] Received — scheduling NDAT response");
    _sendNDAT();   // non-blocking: schedules jitter, actual TX happens in update()
}


void LighthouseReckoning::_handleDATA(uint8_t* buf, size_t len, float rssi, float snr) {
    // Defensive length check (caller already validated, kept for safety)
    if (len < LHR_DATA_HEADER_LEN) {
        return;
    }

    uint32_t receiverId =
        ((uint32_t)buf[LHR_DATA_OFFSET_RECEIVER + 0] << 24) |
        ((uint32_t)buf[LHR_DATA_OFFSET_RECEIVER + 1] << 16) |
        ((uint32_t)buf[LHR_DATA_OFFSET_RECEIVER + 2] <<  8) |
         (uint32_t)buf[LHR_DATA_OFFSET_RECEIVER + 3];

    if (receiverId == _deviceId) {
        LHR_DEBUG_PRINTLN("[DATA] Packet is for us (receiver=0x%08X)", receiverId);
    }
    else {
        LHR_DEBUG_PRINTLN("[DATA] Not for us (receiver=0x%08X, we are 0x%08X), dropping", receiverId, _deviceId);
        _startReceive();
        return;
    }

    uint32_t senderId =
        ((uint32_t)buf[LHR_DATA_OFFSET_SENDER + 0] << 24) |
        ((uint32_t)buf[LHR_DATA_OFFSET_SENDER + 1] << 16) |
        ((uint32_t)buf[LHR_DATA_OFFSET_SENDER + 2] <<  8) |
         (uint32_t)buf[LHR_DATA_OFFSET_SENDER + 3];

    uint8_t seqNum = buf[LHR_DATA_OFFSET_SEQ_NUM];
    
    LHR_DEBUG_PRINTLN("[DATA] From 0x%08X to 0x%08X ttl=%d", senderId, receiverId, buf[LHR_DATA_OFFSET_TTL]);

    _lastPacketSenderId = senderId;

    // ACK unconditionally, even before checking for duplicates: if our
    // previous DATA_RES for this seq was lost in transit, the sender will
    // retransmit the same packet. ACKing again here lets that retry
    // resolve even though we drop the (duplicate) payload below.
    _sendDATARES(senderId, seqNum);   // ACK the hop
    
    uint32_t sourceId =
        ((uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 0] << 24) |
        ((uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 1] << 16) |
        ((uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 2] <<  8) |
         (uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 3];

    if (_isDuplicateData(sourceId, seqNum)) {
        LHR_DEBUG_PRINTLN("[DATA] Duplicate (src=0x%08X seq=%d), dropping", sourceId, seqNum);
        return;
    }

    if (_role == LHR_ROLE_HOME) {
        if (_dataReceivedCb != nullptr) {
            // Copy into stable class buffer so the callback pointer stays valid
            // regardless of how long the callback takes.
            memcpy(_rxBuf, buf, len);
            _rxLen = len;
            _dataReceivedCb(_rxBuf, _rxLen);
        }
        else {
            LHR_DEBUG_PRINTLN("[DATA] Received but no callback registered, dropping");
        }
    }
    else if (_role == LHR_ROLE_NODE) {
        if (buf[LHR_DATA_OFFSET_TTL] > 0) {
            buf[LHR_DATA_OFFSET_TTL]--;
        } 
        else {

            LHR_DEBUG_PRINTLN("[FWD] TTL expired, dropping packet");
            return;
        }
        memcpy(_forwardBuf, buf, len);
        _forwardLen        = len;
        if (_routingDataEnabled) {
            _appendHopMetadata(rssi, snr);    // Test/debug instrumentation only — see comment on _appendHopMetadata()
        }
        
        _waitingForDataRes = true;
    }
}


bool LighthouseReckoning::_handleDATARES(uint8_t* buf, size_t len) {
    // Defensive length check (caller already validated, kept for safety)
    if (len != LHR_DATA_RES_LEN) {
        return false;
    } 

    uint32_t receiverId =
        ((uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 0] << 24) |
        ((uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 1] << 16) |
        ((uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 2] <<  8) |
         (uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 3];

    if (receiverId == _deviceId) {
        uint8_t seqNum = buf[LHR_DATARES_OFFSET_SEQ_NUM];
        if (_pendingAckSeqNum == seqNum) {
#ifdef LHR_DEBUG
            // Only needed for the debug log line below — avoids an
            // unused-variable warning in release builds.
            uint32_t senderId =     
                ((uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 0] << 24) |
                ((uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 1] << 16) |
                ((uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 2] <<  8) |
                (uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 3];

            LHR_DEBUG_PRINTLN("[DATARES] Received from 0x%08X", senderId);
#endif

            _resetDataResState();
            return true;
        }
        else {
            LHR_DEBUG_PRINTLN("[DATARES] for us but wrong seq (expected=%d, got=%d)", _pendingAckSeqNum, seqNum);
            return false;
        }
    }
    else {
        LHR_DEBUG_PRINTLN("[DATARES] Not for us, dropping");
        return false;
    }
}
