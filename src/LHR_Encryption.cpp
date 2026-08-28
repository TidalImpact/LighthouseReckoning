/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_Encryption.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.0.0
 * Platform:   ESP32 / RP2040 (Pico)
 * Description:
 *   AES-128 encryption logic for LighthouseReckoning packets.
 *   Handles key management, encryption/decryption, and nonce
 *   construction. Has no knowledge of persistent storage —
 *   see LHR_EncryptionStore for nonce counter persistence.
 *   Methods are members of LighthouseReckoning.
 *
 * License:    MIT
 **************************************************************************/

#include "LighthouseReckoning.h"


#if LHR_ENCRYPTION_SUPPORTED

// ================================================================
// Key Management
// ================================================================

lhr_err_t LighthouseReckoning::enableEncryption(bool state){
    if (!state) {
        _encryptionEnabled = false;
        return LHR_OK;
    }
    if (!_encryptionKeySet) {
        return LHR_ERR_NO_KEY_SET;
    }
    lhr_err_t err = _initNonceCounter();
    if (err != LHR_OK) {
        return err;
    }
    _encryptionEnabled = true;
    return LHR_OK;
}


lhr_err_t LighthouseReckoning::setEncryptionKey(const uint8_t* key, size_t len){
    if (key == nullptr) {
        return LHR_ERR_ARGS;
    }
    if (len != 16){
        return LHR_ERR_INVALID_KEY_LEN;
    } 
    memcpy(_encryptionKey, key, 16);
    _encryptionKeySet = true;
    return LHR_OK;
}


// ================================================================
// Nonce Counter Management
// ================================================================

lhr_err_t LighthouseReckoning::_initNonceCounter() {
    if (!_storageInit()) {
        return LHR_ERR_STORE_NOT_INIT;
    }

    _loadNonceCounter(&_encryptionNonceCounter);   // sets to 0 on first run

    _encryptionNonceReserved = _encryptionNonceCounter + LHR_NONCE_BATCH_SIZE;

    if (!_storeNonceCounter(_encryptionNonceReserved)) {
        return LHR_ERR_STORE_WRITE_FAIL;
    }

    return LHR_OK;
}

lhr_err_t LighthouseReckoning::_nextNonceCounter(uint32_t* outCounter) {
    if (_encryptionNonceCounter == 0xFFFFFFFFUL) {
        return LHR_ERR_NONCE_EXHAUSTED;
    }

    if (_encryptionNonceCounter >= _encryptionNonceReserved) {
        uint32_t reserved = _encryptionNonceCounter + LHR_NONCE_BATCH_SIZE;

        if (!_storeNonceCounter(reserved)) {
            return LHR_ERR_STORE_WRITE_FAIL;
        }

        _encryptionNonceReserved = reserved;
    }

    *outCounter = _encryptionNonceCounter;
    _encryptionNonceCounter++;
    return LHR_OK;
}

#endif // LHR_ENCRYPTION_SUPPORTED