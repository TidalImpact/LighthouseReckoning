/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LHR_Encryption.cpp
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.1.0
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
    LockGuard guard(_lock);
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
    LockGuard guard(_lock);
    if (key == nullptr) {
        return LHR_ERR_ARGS;
    }
    if (len != LHR_AES_KEY_LEN){
        return LHR_ERR_INVALID_KEY_LEN;
    } 
    memcpy(_encryptionKey, key, LHR_AES_KEY_LEN);
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
    if (_encryptionNonceCounter + LHR_NONCE_BATCH_SIZE >= LHR_NONCE_COUNTER_MAX) {
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


// ================================================================
// Nonce Builder
// ================================================================

lhr_err_t LighthouseReckoning::_buildNonce(uint8_t* outNonce, uint32_t* outWireCounter){
    uint32_t nonceCounter = 0;
    
    lhr_err_t err = _nextNonceCounter(&nonceCounter);
    if (err != LHR_OK) {
        return err;
    }
    
    // Assemble the nonce used to encrypt this packet
    outNonce[LHR_NONCE_OFFSET_DEVICEID + 0]      =  (_deviceId     >> 24) & 0xFF;
    outNonce[LHR_NONCE_OFFSET_DEVICEID + 1]      =  (_deviceId     >> 16) & 0xFF;
    outNonce[LHR_NONCE_OFFSET_DEVICEID + 2]      =  (_deviceId     >>  8) & 0xFF;
    outNonce[LHR_NONCE_OFFSET_DEVICEID + 3]      =  (_deviceId     >>  0) & 0xFF;

    outNonce[LHR_NONCE_OFFSET_NONCE_COUNTER + 0] =  (nonceCounter  >> 24) & 0xFF;
    outNonce[LHR_NONCE_OFFSET_NONCE_COUNTER + 1] =  (nonceCounter  >> 16) & 0xFF;
    outNonce[LHR_NONCE_OFFSET_NONCE_COUNTER + 2] =  (nonceCounter  >>  8) & 0xFF;
    outNonce[LHR_NONCE_OFFSET_NONCE_COUNTER + 3] =  (nonceCounter  >>  0) & 0xFF;

    // Return the full counter for later packet building
    *outWireCounter = nonceCounter;

    return LHR_OK;
}


// ================================================================
// AES-128-CCM Encryption Wrapper
// ================================================================

lhr_err_t LighthouseReckoning::_ccmEncrypt(const uint8_t* nonce,
                                            const uint8_t* aad, size_t aadLen,
                                            const uint8_t* plaintext, size_t len,
                                            uint8_t* outCiphertext, uint8_t* outTag) {
    int ret = aes128_ccm_encrypt(
        _encryptionKey, LHR_AES_KEY_LEN,
        nonce, LHR_NONCE_LEN,
        aad, (uint32_t)aadLen,
        plaintext, (uint32_t)len,
        outCiphertext, outTag, LHR_MIC_LEN
    );

    return (ret == 0) ? LHR_OK : LHR_ERR_ENCRYPT_FAIL;
}

lhr_err_t LighthouseReckoning::_ccmDecrypt(const uint8_t* nonce,
                                            const uint8_t* aad, size_t aadLen,
                                            const uint8_t* ciphertext, size_t len,
                                            const uint8_t* tag,
                                            uint8_t* outPlaintext) {
    int ret = aes128_ccm_decrypt(
        _encryptionKey, LHR_AES_KEY_LEN,
        nonce, LHR_NONCE_LEN,
        aad, (uint32_t)aadLen,
        ciphertext, (uint32_t)len,
        tag, LHR_MIC_LEN,
        outPlaintext
    );

    return (ret == 0) ? LHR_OK : LHR_ERR_DECRYPT_FAIL;
}

#endif // LHR_ENCRYPTION_SUPPORTED