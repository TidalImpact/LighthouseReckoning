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

    size_t minValidLen = LHR_MIN_VALID_PACKET_SIZE;
    #if LHR_ENCRYPTION_SUPPORTED
        if (_encryptionEnabled) {
            minValidLen = LHR_MIN_VALID_PACKET_SIZE_ENC;
        }
    #endif // LHR_ENCRYPTION_SUPPORTED
    if (len < minValidLen || len > sizeof(buf)) {
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
            size_t minDataLen = LHR_DATA_HEADER_LEN;
            #if LHR_ENCRYPTION_SUPPORTED
                if (_encryptionEnabled) {
                    minDataLen = LHR_DATA_ENC_OFFSET_PAYLOAD;
                }
            #endif // LHR_ENCRYPTION_SUPPORTED
            if (len < minDataLen) {
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
            size_t expectedNdatLen = LHR_NDAT_LEN;
            #if LHR_ENCRYPTION_SUPPORTED
                if (_encryptionEnabled) {
                    expectedNdatLen = LHR_NDAT_ENC_LEN;
                }
            #endif // LHR_ENCRYPTION_SUPPORTED
            if (len != expectedNdatLen) {
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
            size_t expectedRfcnLen = LHR_RFCN_LEN;
            #if LHR_ENCRYPTION_SUPPORTED
                if (_encryptionEnabled) {
                    expectedRfcnLen = LHR_RFCN_ENC_LEN;
                }
            #endif // LHR_ENCRYPTION_SUPPORTED
            if (len != expectedRfcnLen) {
                LHR_DEBUG_PRINTLN("[RX] RFCN wrong length: %d", len);
                break;
            }
            _handleRFCN(buf, len);
            _startReceive();
            return LHR_UPDATE_RX_RFCN;
        }

        case LHR_PKT_DATA_RES: {
            size_t expectedDataResLen = LHR_DATA_RES_LEN;
            #if LHR_ENCRYPTION_SUPPORTED
                if (_encryptionEnabled) {
                    expectedDataResLen = LHR_DATARES_ENC_LEN;
                }
            #endif // LHR_ENCRYPTION_SUPPORTED
            if (len != expectedDataResLen) {
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
// Packet Handlers — Unencrypted
// ================================================================

void LighthouseReckoning::_handleNDAT(uint8_t* buf, size_t len, float rssi) {
    uint32_t senderId;
    uint8_t  hops;
#if LHR_ENCRYPTION_SUPPORTED
    if (_encryptionEnabled) {
        lhr_err_t err = _verifyAndDecryptNDAT(buf, len, &senderId, &hops);
        if (err != LHR_OK) {
            LHR_DEBUG_PRINTLN("[NDAT] Decrypt/verify failed, err=%d", err);
            return;
        }
    } else
#endif // LHR_ENCRYPTION_SUPPORTED
    {
        senderId =
        ((uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 0] << 24) |
        ((uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 1] << 16) |
        ((uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 2] <<  8) |
         (uint32_t)buf[LHR_NDAT_OFFSET_SENDER + 3];

        hops = buf[LHR_NDAT_OFFSET_HOPS];
    }
    

    LHR_DEBUG_PRINTLN("[NDAT] From 0x%08X hops=%d rssi=%.1f", senderId, hops, rssi);

    _updateNeighbor(senderId, hops, rssi);
}


void LighthouseReckoning::_handleRFCN(uint8_t* buf, size_t len) {
#if LHR_ENCRYPTION_SUPPORTED
    if (_encryptionEnabled) {
        lhr_err_t err = _verifyRFCN(buf, len);
        if (err != LHR_OK) {
            LHR_DEBUG_PRINTLN("[RFCN] Decrypt/verify failed, err=%d", err);
            return;
        }
    }
#endif // LHR_ENCRYPTION_SUPPORTED
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
    uint32_t receiverId;
    uint8_t  seqNum;

#if LHR_ENCRYPTION_SUPPORTED
    if (_encryptionEnabled) {
        // Defensive length check (caller already validated, kept for safety)
        if (len != LHR_DATARES_ENC_LEN) {
            return false;
        }

        // RECEIVER is AAD (plaintext) even when encrypted — readable directly,
        // no need to decrypt first just to know whether this packet is for us.
        receiverId =
            ((uint32_t)buf[LHR_DATARES_ENC_OFFSET_RECEIVER + 0] << 24) |
            ((uint32_t)buf[LHR_DATARES_ENC_OFFSET_RECEIVER + 1] << 16) |
            ((uint32_t)buf[LHR_DATARES_ENC_OFFSET_RECEIVER + 2] <<  8) |
             (uint32_t)buf[LHR_DATARES_ENC_OFFSET_RECEIVER + 3];

        if (receiverId != _deviceId) {
            LHR_DEBUG_PRINTLN("[DATARES] Not for us, dropping");
            return false;
        }

        lhr_err_t err = _verifyAndDecryptDATARES(buf, len, &seqNum);
        if (err != LHR_OK) {
            LHR_DEBUG_PRINTLN("[DATARES] Decrypt/verify failed, err=%d", err);
            return false;
        }
    } else
#endif // LHR_ENCRYPTION_SUPPORTED
    {
        // Defensive length check (caller already validated, kept for safety)
        if (len != LHR_DATA_RES_LEN) {
            return false;
        }

        receiverId =
            ((uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 0] << 24) |
            ((uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 1] << 16) |
            ((uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 2] <<  8) |
             (uint32_t)buf[LHR_DATARES_OFFSET_RECEIVER + 3];

        if (receiverId != _deviceId) {
            LHR_DEBUG_PRINTLN("[DATARES] Not for us, dropping");
            return false;
        }

        seqNum = buf[LHR_DATARES_OFFSET_SEQ_NUM];
    }

    if (_pendingAckSeqNum != seqNum) {
        LHR_DEBUG_PRINTLN("[DATARES] for us but wrong seq (expected=%d, got=%d)", _pendingAckSeqNum, seqNum);
        return false;
    }

#ifdef LHR_DEBUG
    // Only needed for the debug log line below — avoids an
    // unused-variable warning in release builds.
    uint32_t senderId;
    #if LHR_ENCRYPTION_SUPPORTED
    if (_encryptionEnabled) {
        senderId =
            ((uint32_t)buf[LHR_DATARES_ENC_OFFSET_SENDER + 0] << 24) |
            ((uint32_t)buf[LHR_DATARES_ENC_OFFSET_SENDER + 1] << 16) |
            ((uint32_t)buf[LHR_DATARES_ENC_OFFSET_SENDER + 2] <<  8) |
             (uint32_t)buf[LHR_DATARES_ENC_OFFSET_SENDER + 3];
    } else
    #endif // LHR_ENCRYPTION_SUPPORTED
    {
        senderId =
            ((uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 0] << 24) |
            ((uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 1] << 16) |
            ((uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 2] <<  8) |
             (uint32_t)buf[LHR_DATARES_OFFSET_SENDER + 3];
    }

    LHR_DEBUG_PRINTLN("[DATARES] Received from 0x%08X", senderId);
#endif

    _resetDataResState();
    return true;
}

// ================================================================
// Packet Handlers — Encrypted
// ================================================================

#if LHR_ENCRYPTION_SUPPORTED

lhr_err_t LighthouseReckoning::_verifyAndDecryptDataPacket(uint8_t* buf, size_t len, uint8_t* outPacket, size_t* outPacketLen) {
    if (len < LHR_DATA_HEADER_ENC_LEN || len > LORA_PHY_MAX_PACKET_SIZE) {
        return LHR_ERR_ARGS;
    }

    uint8_t aad[10];
    aad[0] = buf[LHR_DATA_ENC_OFFSET_MAGIC];
    aad[1] = buf[LHR_DATA_ENC_OFFSET_TYPE];

    memcpy(&aad[2], &buf[LHR_DATA_ENC_OFFSET_SENDER],   4);

    memcpy(&aad[6], &buf[LHR_DATA_ENC_OFFSET_RECEIVER], 4);


    size_t payloadLen = len - LHR_DATA_HEADER_ENC_LEN;

    // Ciphertext is the contiguous block: source(4) + seq(1) + ttl(1) + payload(N),
    // matching the plaintext layout built in _buildEncryptedDataPacket().
    uint8_t temp[LHR_DATA_ENC_HEADER_FIELDS_LEN + LHR_MAX_PAYLOAD_ENC];
    memcpy(&temp[0], &buf[LHR_DATA_ENC_OFFSET_SOURCE], 4);

    memcpy(&temp[4], &buf[LHR_DATA_ENC_OFFSET_SEQ_NUM], 1);
    memcpy(&temp[5], &buf[LHR_DATA_ENC_OFFSET_TTL], 1);

    memcpy(&temp[LHR_DATA_ENC_HEADER_FIELDS_LEN], &buf[LHR_DATA_ENC_OFFSET_PAYLOAD], payloadLen);

    size_t cipherLen = LHR_DATA_ENC_HEADER_FIELDS_LEN + payloadLen;

    uint8_t nonce[LHR_NONCE_LEN];
    memcpy(&nonce[0], &buf[LHR_DATA_ENC_OFFSET_SENDER], 4);
    memcpy(&nonce[5], &buf[LHR_DATA_ENC_OFFSET_WIRECOUNTER], 3);

    uint8_t plaintext[LHR_DATA_ENC_HEADER_FIELDS_LEN + LHR_MAX_PAYLOAD_ENC];
    lhr_err_t err = LHR_ERR_AUTH_FAIL;

    // nonce[4] (the counter's upper byte) is not transmitted on the wire to save
    // bandwidth; it is brute-forced below since it changes rarely (only on
    // wire-counter rollover), while nonce[5..7] carry the transmitted lower
    // 3 bytes of the counter.
    for (uint8_t upperByte = 0; upperByte < LHR_NONCE_UPPER_BYTE_MAX_ATTEMPTS; upperByte++) {
        nonce[4] = upperByte;

        err = _ccmDecrypt(nonce, aad, sizeof(aad), &temp[0], cipherLen, &buf[LHR_DATA_ENC_OFFSET_MIC], plaintext);
        if (err == LHR_OK) {
            break;
        }
    }

    if (err != LHR_OK) {
        return err;
    }

    // Reassemble the full cleartext packet: header fields (MAGIC/TYPE from
    // the wire, SENDER/RECEIVER from AAD) plus the decrypted SOURCE/SEQ/TTL/PAYLOAD.
    outPacket[LHR_DATA_OFFSET_MAGIC] = aad[0];
    outPacket[LHR_DATA_OFFSET_TYPE]  = aad[1];

    memcpy(&outPacket[LHR_DATA_OFFSET_SOURCE], &plaintext[0], 4);

    memcpy(&outPacket[LHR_DATA_OFFSET_SENDER],   &aad[2], 4);

    memcpy(&outPacket[LHR_DATA_OFFSET_RECEIVER], &aad[6], 4);

    memcpy(&outPacket[LHR_DATA_OFFSET_SEQ_NUM], &plaintext[4], 1);
    memcpy(&outPacket[LHR_DATA_OFFSET_TTL],     &plaintext[5], 1);

    memcpy(&outPacket[LHR_DATA_OFFSET_PAYLOAD], &plaintext[LHR_DATA_ENC_HEADER_FIELDS_LEN], payloadLen);

    *outPacketLen = LHR_DATA_OFFSET_PAYLOAD + payloadLen;

    return LHR_OK;
}

lhr_err_t LighthouseReckoning::_verifyAndDecryptNDAT(uint8_t* buf, size_t len, uint32_t* outSenderId, uint8_t* outHops) {
    if (len != LHR_NDAT_ENC_LEN) {
        return LHR_ERR_ARGS;
    }

    uint8_t aad[6];
    aad[0] = buf[LHR_NDAT_ENC_OFFSET_MAGIC];
    aad[1] = buf[LHR_NDAT_ENC_OFFSET_TYPE];
    memcpy(&aad[2], &buf[LHR_NDAT_ENC_OFFSET_SENDER], 4);

    uint8_t nonce[LHR_NONCE_LEN];
    memcpy(&nonce[0], &buf[LHR_NDAT_ENC_OFFSET_SENDER], 4);
    memcpy(&nonce[5], &buf[LHR_NDAT_ENC_OFFSET_WIRECOUNTER], 3);

    uint8_t hopsPlain;
    lhr_err_t lastErr = LHR_ERR_AUTH_FAIL;

    // nonce[4] (the counter's upper byte) is not transmitted on the wire to save
    // bandwidth; it is brute-forced below since it changes rarely (only on
    // wire-counter rollover), while nonce[5..7] carry the transmitted lower
    // 3 bytes of the counter.
    for (uint8_t upperByte = 0; upperByte < LHR_NONCE_UPPER_BYTE_MAX_ATTEMPTS; upperByte++) {
        nonce[4] = upperByte;

        lastErr = _ccmDecrypt(nonce, aad, sizeof(aad),
                               &buf[LHR_NDAT_ENC_OFFSET_HOPS], 1,
                               &buf[LHR_NDAT_ENC_OFFSET_MIC], &hopsPlain);
        if (lastErr == LHR_OK) {
            *outSenderId = ((uint32_t)aad[2] << 24) | ((uint32_t)aad[3] << 16) |
                           ((uint32_t)aad[4] <<  8) |  (uint32_t)aad[5];
            *outHops = hopsPlain;
            return LHR_OK;
        }
    }

    return lastErr;
}

lhr_err_t LighthouseReckoning::_verifyRFCN(uint8_t* buf, size_t len) {
    if (len != LHR_RFCN_ENC_LEN) {
        return LHR_ERR_ARGS;
    }

    uint8_t aad[6];
    aad[0] = buf[LHR_RFCN_ENC_OFFSET_MAGIC];
    aad[1] = buf[LHR_RFCN_ENC_OFFSET_TYPE];
    memcpy(&aad[2], &buf[LHR_RFCN_ENC_OFFSET_SENDER], 4);

    uint8_t nonce[LHR_NONCE_LEN];
    memcpy(&nonce[0], &buf[LHR_RFCN_ENC_OFFSET_SENDER], 4);
    memcpy(&nonce[5], &buf[LHR_RFCN_ENC_OFFSET_WIRECOUNTER], 3);

    lhr_err_t lastErr = LHR_ERR_AUTH_FAIL;

    // nonce[4] (the counter's upper byte) is not transmitted on the wire to save
    // bandwidth; it is brute-forced below since it changes rarely (only on
    // wire-counter rollover), while nonce[5..7] carry the transmitted lower
    // 3 bytes of the counter.
    for (uint8_t upperByte = 0; upperByte < LHR_NONCE_UPPER_BYTE_MAX_ATTEMPTS; upperByte++) {
        nonce[4] = upperByte;

        // RFCN has no encrypted payload, only AAD authenticated against the MIC.
        lastErr = _ccmDecrypt(nonce, aad, sizeof(aad),
                               nullptr, 0,
                               &buf[LHR_RFCN_ENC_OFFSET_MIC], nullptr);
        if (lastErr == LHR_OK) {
            return LHR_OK;
        }
    }

    return lastErr;
}

lhr_err_t LighthouseReckoning::_verifyAndDecryptDATARES(uint8_t* buf, size_t len, uint8_t* outSeqNum) {
    if (len != LHR_DATARES_ENC_LEN) {
        return LHR_ERR_ARGS;
    }

    uint8_t aad[10];
    aad[0] = buf[LHR_DATARES_ENC_OFFSET_MAGIC];
    aad[1] = buf[LHR_DATARES_ENC_OFFSET_TYPE];
    memcpy(&aad[2], &buf[LHR_DATARES_ENC_OFFSET_SENDER],   4);
    memcpy(&aad[6], &buf[LHR_DATARES_ENC_OFFSET_RECEIVER], 4);

    uint8_t nonce[LHR_NONCE_LEN];
    memcpy(&nonce[0], &buf[LHR_DATARES_ENC_OFFSET_SENDER], 4);
    memcpy(&nonce[5], &buf[LHR_DATARES_ENC_OFFSET_WIRECOUNTER], 3);

    uint8_t seqNumPlain;
    lhr_err_t lastErr = LHR_ERR_AUTH_FAIL;

    // nonce[4] (the counter's upper byte) is not transmitted on the wire to save
    // bandwidth; it is brute-forced below since it changes rarely (only on
    // wire-counter rollover), while nonce[5..7] carry the transmitted lower
    // 3 bytes of the counter.
    for (uint8_t upperByte = 0; upperByte < LHR_NONCE_UPPER_BYTE_MAX_ATTEMPTS; upperByte++) {
        nonce[4] = upperByte;

        lastErr = _ccmDecrypt(nonce, aad, sizeof(aad),
                               &buf[LHR_DATARES_ENC_OFFSET_SEQ_NUM], 1,
                               &buf[LHR_DATARES_ENC_OFFSET_MIC], &seqNumPlain);
        if (lastErr == LHR_OK) {
            *outSeqNum     = seqNumPlain;
            return LHR_OK;
        }
    }

    return lastErr;
}

#endif // LHR_ENCRYPTION_SUPPORTED