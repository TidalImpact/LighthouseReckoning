/**************************************************************************
 * Library:    Lighthouse Reckoning
 * File:       LighthouseReckoning.h
 * Author:     Fynn Jannis Schulz
 * Co-Design:  Jan Schulz
 * Version:    1.0.0
 * Platform:   Validated on SX126x-family radios (e.g. SX1262). Other
 *             RadioLib-supported chips (SX127x/RFM9x, SX128x, etc.) are
 *             expected to work for TX/RX, but channel-activity detection
 *             (_transmit()'s CAD check) and the single-DIO1-interrupt
 *             design in handleDio1Rise() were only tested against SX126x's
 *             IRQ behavior. Untested on other families — verify CAD and
 *             IRQ wiring yourself before relying on it outside SX126x.
 * Description:
 *   Public API for the Lighthouse Reckoning LoRa mesh routing library.
 *   Handles neighbor discovery, packet routing, and retransmission
 *   automatically. The user only interacts with this class.
 *
 * Depends:    RadioLib
 * License:    MIT
 **************************************************************************/

// Formatting convention:
//   ================================================================
//   Used for top-level file sections outside classes.
//
//   ------------------------------------------------------------------
//   Used for grouping sections inside classes.
//
//   One empty line after every function.
//   Two empty lines after the last function before the next section.

// ================================================================
// Some Functions
// ================================================================
//
// void LighthouseReckoning::afunction() {
//     
// }
//
//
// ================================================================
// Some other Functions
// ================================================================

#pragma once

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include <stdint.h>
  #include <stddef.h>
  #include <string.h>
#endif

#include <RadioLib.h>

#include "LHR_TypeDef.h"
#include "LHR_Packet.h"
#include "LHR_Routing.h"
#include "LHR_Tx.h"
#include "LHR_Rx.h"
#include "LHR_Encryption.h"
#include "LHR_EncryptionStore.h"

#if LHR_ENCRYPTION_SUPPORTED
  #include "aes128_ccm_backend/aes128_ccm.h"
#endif

// ================================================================
// Version
// ================================================================

#define LHR_VERSION_MAJOR (1)
#define LHR_VERSION_MINOR (0)
#define LHR_VERSION_PATCH (0)


// Callback type for incoming data on Home node
typedef void (*LHR_DataReceivedCallback)(uint8_t* buf, size_t len);


class LighthouseReckoning {
public:

    // ------------------------------------------------------------------
    // Version
    // ------------------------------------------------------------------

    /**
     * @brief Get the library version as separate major/minor/patch integers.
     * @param major Output: major version number.
     * @param minor Output: minor version number.
     * @param patch Output: patch version number.
     * @return Always returns 0.
     */
    static int library_version(int& major, int& minor, int& patch) {
        major = LHR_VERSION_MAJOR;
        minor = LHR_VERSION_MINOR;
        patch = LHR_VERSION_PATCH;
        return 0;
    }
    
    /**
     * @brief Get the library version as a human-readable string (e.g. "1.0.0").
     * @return Pointer to a static, null-terminated version string.
     */
    static const char* getVersionString() {
        static char version[16];

        snprintf(version, sizeof(version), "%d.%d.%d",
                LHR_VERSION_MAJOR,
                LHR_VERSION_MINOR,
                LHR_VERSION_PATCH);

        return version;
    }


    // ------------------------------------------------------------------
    // Setup
    // ------------------------------------------------------------------

    /**
     * @brief Initialize this node as the Home node (hops = 0, receives all data).
     *
     * Must be called once before update() or sendData(). Resets all internal
     * state (neighbor table, retry state, buffers).
     *
     * @param radio    Pointer to any RadioLib-compatible module (SX1262, SX1276, RFM95, ...).
     *                 Must not be null.
     * @param deviceId This node's unique 32-bit Id. Must not be LHR_FORBIDDEN_NODE_ID (0x00000000).
     * @return
     * - LHR_INIT_OK on success.
     * - LHR_INIT_ERR_RADIO_NULL if @p radio is nullptr.
     * - LHR_INIT_ERR_INVALID_ARGS if @p deviceId is LHR_FORBIDDEN_NODE_ID.
     */
    lhr_init_result_t beginAsHome(PhysicalLayer* radio, uint32_t deviceId);

    /**
     * @brief Initialize this node as a Sensor/Relay node (hops = 255 until a route is found).
     *
     * Must be called once before update() or sendData(). Resets all internal
     * state and immediately schedules an RFCN broadcast to discover neighbors.
     *
     * @param radio    Pointer to any RadioLib-compatible module (SX1262, SX1276, RFM95, ...).
     *                 Must not be null.
     * @param deviceId This node's unique 32-bit Id. Must not be LHR_FORBIDDEN_NODE_ID (0x00000000).
     * @return
     * - LHR_INIT_OK on success.
     * - LHR_INIT_ERR_RADIO_NULL if @p radio is nullptr.
     * - LHR_INIT_ERR_INVALID_ARGS if @p deviceId is LHR_FORBIDDEN_NODE_ID.
     */
    lhr_init_result_t beginAsNode(PhysicalLayer* radio, uint32_t deviceId);


    // ------------------------------------------------------------------
    // Configuration  (optional, call after begin)
    // ------------------------------------------------------------------

    /**
     * @brief Set the default TTL (hop limit) for outgoing DATA packets.
     * @param ttl Hop count budget. Default: LHR_DEFAULT_TTL (8).
     */
    void setTTL(uint8_t ttl);

    /**
     * @brief Configure retry timing for the DATA/DATA_RES confirmation cycle.
     *
     * See _confirmDataRes() for the full state machine these delays drive.
     *
     * @param resendDelayMs  How long to wait after the initial send before
     *                       the first resend attempt.
     * @param rfcnWaitMs     How long to wait for neighbor responses after
     *                       sending an RFCN (triggered if the resend also times out).
     */
    void setRetryDelays(unsigned long resendDelayMs, unsigned long rfcnWaitMs);

    /**
     * @brief Set how often this node broadcasts NDAT (or RFCN, if no route is known).
     * @param intervalMs Beacon interval in milliseconds.
     *                    Default: LHR_DEFAULT_BEACON_INTERVAL_MS (60000 ms).
     * @warning Configure the beacon interval for your deployment. Short intervals increase beacon traffic, consuming more airtime and duty-cycle budget if enabled.
     */
    void setBeaconInterval(unsigned long intervalMs);

    /**
     * @brief Set the maximum time a transmit is allowed to remain in progress
     *        before being force-recovered.
     *
     * Guards against a stuck radio (e.g. missed DIO1 interrupt). See the
     * watchdog check in update().
     *
     * @param timeoutMs Timeout in milliseconds. Default: 10000.
     */
    void setTxWatchdogTimeout(unsigned long timeoutMs);

    /**
     * @brief Set how many full send/resend/RFCN cycles are attempted
     *        before a queued DATA packet is dropped (default 3).
     * @see _confirmDataRes()
     * @param maxRetries Maximum number of send/resend/RFCN cycles before giving up (default: LHR_DEFAULT_MAX_LOCAL_RETRIES).
     */
    void setMaxLocalRetries(uint8_t maxRetries);


    // ------------------------------------------------------------------
    // Runtime  (call every loop())
    // ------------------------------------------------------------------

    /**
     * @brief Drives all protocol state machines. Must be called frequently
     *        and regularly from the main loop (non-blocking).
     *
     * Priority order each call:
     *   1. In-flight TX completion/watchdog (blocks everything else)
     *   2. Incoming packet dispatch (_checkLoraData())
     *   3. DATA_RES retry/confirmation state machine (_confirmDataRes())
     *   4. Due NDAT jitter transmission
     *   5. Periodic beacon (NDAT or RFCN)
     *
     * @return An lhr_update_result_t describing what happened this call
     *         (LHR_UPDATE_IDLE if nothing happened)
     */
    lhr_update_result_t update();

    /**
     * @brief Radio DIO1 interrupt callback — call this from the pin
     *        interrupt handler (e.g. attachInterrupt) whenever the radio's
     *        DIO1 line rises.
     *
     * All actual protocol work happens later in update(), keeping this
     * safe to call from ISR context.
     */
    void handleDio1Rise();


    // ------------------------------------------------------------------
    // Sending
    // ------------------------------------------------------------------

    /**
     * @brief Queue application data for transmission toward Home using the default TTL (set via setTTL()).
     *
     * Accepted data is not sent synchronously — actual transmission begins
     * on the next update() call and is retried automatically by the
     * internal DATA_RES confirmation state machine (see _confirmDataRes()).
     *
     * @param payload  Pointer to the application payload.
     * @param len      Payload length in bytes; must be <= LHR_MAX_PAYLOAD.
     * @return LHR_OK if the packet was accepted into the send queue
     *         ("accepted", not "sent"); an lhr_err_t error code otherwise
     *         (e.g. LHR_ERR_NO_ROUTE, LHR_ERR_BUSY, LHR_ERR_TOO_LONG).
     */
    lhr_err_t sendData(uint8_t* payload, size_t len);

    /**
     * @brief Queue application data for transmission toward Home.
     *
     * Accepted data is not sent synchronously — actual transmission begins
     * on the next update() call and is retried automatically by the
     * internal DATA_RES confirmation state machine (see _confirmDataRes()).
     *
     * @param payload  Pointer to the application payload.
     * @param len      Payload length in bytes; must be <= LHR_MAX_PAYLOAD.
     * @param ttl      Time-to-live / max hop count for this packet.
     * @return LHR_OK if the packet was accepted into the send queue
     *         ("accepted", not "sent"); an lhr_err_t error code otherwise
     *         (e.g. LHR_ERR_NO_ROUTE, LHR_ERR_BUSY, LHR_ERR_TOO_LONG).
     */
    lhr_err_t sendData(uint8_t* payload, size_t len, uint8_t ttl);


    // ------------------------------------------------------------------
    // Duty Cycle
    // ------------------------------------------------------------------

    /**
     * @brief Enable or disable duty cycle limiting using the EU868 default (1%).
     * @param state true to enable at LHR_DEFAULT_DUTY_CYCLE_PERCENT, false to disable.
     */
    void toggleDutyCycleLimit(bool state);

    /**
     * @brief Set the duty cycle limit as a percentage of the time window.
     * @param percent Allowed airtime percentage (0 disables the limit).
     *                EU868 g1 sub-band default: 1.0f.
     */
    void setDutyCycleLimit(float percent);

    /**
     * @brief Set the duty cycle limit as an absolute airtime budget per window.
     * @param msPerWindow Allowed transmit time (ms) per LHR_DUTY_CYCLE_WINDOW_MS window.
     */
    void setDutyCycleLimitMs(unsigned long msPerWindow);

    /**
     * @brief Get current duty cycle usage as a percentage of the configured limit.
     * @return 0.0-100.0+ (can exceed 100 momentarily before the next transmit is blocked).
     */
    float getDutyCycleUsage();

    /**
     * @brief Get the airtime used so far in the current duty-cycle window.
     * @return Milliseconds used since the window last reset.
     */
    unsigned long getDutyCycleUsedMs();

    /**
     * @brief Get the remaining transmit budget in the current window.
     * @return Milliseconds available before the duty-cycle limit blocks transmits (0 if exhausted).
     */
    unsigned long getDutyCycleRemainingMs();

    /**
     * @brief Get the configured duty cycle transmit time budget for the current window.
     * @return Maximum allowed transmit time in milliseconds per duty cycle window.
     */
    unsigned long getDutyCycleLimitMs() const;


    // ------------------------------------------------------------------
    // Status and routing information
    // ------------------------------------------------------------------

    /**
     * @brief Get the current role of this node.
     *
     * Returns whether this node is configured as a Home node or a Relay node.
     *
     * @return The current node role.
     */
    lhr_node_role_t getRole() const;

    /**
     * @brief Get the current hop distance to the Home node.
     *
     * Returns the number of relay hops required to reach Home.
     *
     * @return
     * - 0 if this node is Home
     * - 1-254 if a route exists
     * - LHR_HOPS_NO_ROUTE if no route is currently available
     */
    uint8_t getHopsToHome() const;

    /**
     * @brief Get the number of currently known neighbors.
     *
     * @return Number of valid entries in the neighbor table.
     */
    uint8_t getNeighborCount() const;

    /**
     * @brief Get the Id of the currently selected next-hop neighbor.
     *
     * This is the neighbor used for forwarding packets towards Home.
     *
     * @return Neighbor Id or LHR_FORBIDDEN_NODE_ID if no route exists.
     */
    uint32_t getBestNeighborId() const;

    /**
     * @brief Get the Id of the second-best routing neighbor.
     *
     * Used internally as a fallback route when the best neighbor
     * becomes unavailable or is the packet source.
     *
     * @return Second-best neighbor ID. If no second-best neighbor is available,
     *         returns the best neighbor ID. Returns LHR_FORBIDDEN_NODE_ID if no
     *         valid neighbor exists.
     */
    uint32_t getSecondBestNeighborId() const;

    /**
     * @brief Check whether the library is currently busy.
     *
     * A busy node is currently processing a transmission,
     * waiting for confirmation, or executing another blocking
     * protocol state.
     *
     * @return true if the node cannot accept a new transmission.
     */
    bool isBusy() const;


    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------

    // Register callback — fired on Home node when a DATA packet arrives.
    //   buf : pointer to the internal RX buffer (valid only during callback)
    //   len : total packet length including header
    // Use buf + LHR_DATA_OFFSET_PAYLOAD to access payload.
    // Use len  - LHR_DATA_HEADER_LEN    for payload length.
    // NOTE: buf is only valid for the duration of the callback.
    //       Copy any data you need before returning.
    /**
     * @brief Register a callback invoked whenever this node (acting as
     *        Home) receives a DATA packet addressed to it.
     *
     * Only relevant when initialized via beginAsHome() — Node-role
     * instances forward DATA packets instead of delivering them locally.
     *
     * @param cb Callback receiving a pointer to the packet buffer and its length.
     *           The buffer is only valid for the duration of the callback call.
     */
    void onDataReceived(LHR_DataReceivedCallback cb);
    
    
    // ------------------------------------------------------------------
    // Encryption
    // ------------------------------------------------------------------
    
    #if LHR_ENCRYPTION_SUPPORTED

         /**
         * @brief Set the AES-128 encryption key.
         *
         * Copies the given key into internal storage. Does not enable
         * encryption by itself — call enableEncryption(true) afterwards.
         *
         * @param key Pointer to the key buffer. Must not be null.
         * @param len Length of the key in bytes. Must be exactly 16 (AES-128).
         * @return LHR_OK on success.
         *         LHR_ERR_ARGS if key is null.
         *         LHR_ERR_INVALID_KEY_LEN if len != 16.
         */
        lhr_err_t   setEncryptionKey(const uint8_t* key,  size_t len);

        /**
         * @brief Enable or disable AES-128 packet encryption.
         *
         * Disabling always succeeds. Enabling requires a key to have been
         * set beforehand via setEncryptionKey() — otherwise the call fails
         * and encryption remains disabled.
         *
         * @param state true to enable encryption, false to disable it.
         * @return LHR_OK on success.
         *         LHR_ERR_NO_KEY_SET if enabling was requested but no key
         *         has been set yet.
         */
        lhr_err_t   enableEncryption(bool state);

        /**
         * @brief Check whether packet encryption is currently enabled.
         *
         * @return true if encryption is active for outgoing/incoming packets.
         */ 
        bool        isEncryptionEnabled() {return _encryptionEnabled;}

    #endif // LHR_ENCRYPTION_SUPPORTED

    // ------------------------------------------------------------------
    // External RadioLib API Connections
    // ------------------------------------------------------------------
    /**
     * @brief Get the RadioLib status code from the last startTransmit() call.
     * @return A RADIOLIB_ERR_* code; RADIOLIB_ERR_NONE on success.
     */
    int16_t getLastRadioError() const { return _lastRadioError; }


    // ------------------------------------------------------------------
    // Debug
    // ------------------------------------------------------------------
    
    /**
     * @brief Debug-only: print the current neighbor table to the debug output.
     *
     * Compiled out entirely unless LHR_DEBUG is defined — no cost in release builds.
     */
    void printNeighborTable();


    // ------------------------------------------------------------------
    // Experimental / Testing/Debug Functions  
    // ------------------------------------------------------------------

    /**
     * @brief Testing utility: sets the minimum hop count a neighbor must
     *        advertise to be accepted into the neighbor table.
     *
     * A neighbor is only accepted if its advertised hop count is greater
     * than or equal to this value; anything lower is silently rejected —
     * its NDAT is discarded and the neighbor table is not updated.
     *
     * Since this node's own hop count becomes (best neighbor's hops + 1),
     * raising this value pushes the node at least that many hops away
     * from Home:
     *   - 0 (default): no filtering, normal behavior — any neighbor is
     *     accepted, including Home itself (hops = 0).
     *   - 1: rejects Home directly, but accepts any neighbor at hops >= 1
     *     — this node ends up at least 2 hops from Home.
     *   - 2: also rejects neighbors that are themselves direct Home
     *     neighbors (hops = 1) — this node ends up at least 3 hops from
     *     Home.
     *   - N: rejects any neighbor closer than N hops — this node ends up
     *     at least N + 1 hops from Home.
     *
     * Useful for testing multi-hop chains of a specific minimum length
     * without physically separating devices. Purely local filtering — has
     * no effect on the wire protocol or any other node's behavior.
     *
     * @param hops  minimum hop count a neighbor must advertise to be
     *              accepted. 0 disables the restriction (default, normal
     *              behavior).
     *
     * @note Has no effect on Home nodes.
     * @note Does not affect already-stored neighbors — call before the
     *       first relevant NDAT is received, or clear the neighbor table
     *       manually afterward.
     */
    void setMinimumAcceptedHops(uint8_t hops) { _minimumAcceptedHops = hops; }

    /**
     * @brief EXPERIMENTAL: enable per-hop RSSI/SNR metadata appended to forwarded
     *        packets, used for field-test route reconstruction and signal analysis.
     *
     * Not part of the stable wire format — may change or be removed in following versions.
     *
     * @param enabled true to append hop metadata during forwarding.
     */
    void setRoutingDataEnabled(bool enabled) { _routingDataEnabled = enabled; }

private:

    // ------------------------------------------------------------------
    // Radio + Identity
    // ------------------------------------------------------------------

    PhysicalLayer*  _radio              = nullptr;
    uint32_t        _deviceId           = 0;
    uint8_t         _hopsToHome         = LHR_HOPS_NO_ROUTE;
    lhr_node_role_t _role               = LHR_ROLE_NONE;
    uint8_t         _lastAdvertisedHops = LHR_HOPS_NO_ROUTE;    // Stores the hop value last advertised in NDAT packets. Used to detect routing changes that require a new advertisement.
    uint8_t         _currentSeqNum      = 0;                    // Sequence number counter for DATA packets originated by this node.
                                                                // Incremented for every new locally generated DATA packet.


    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    uint8_t       _ttl               = LHR_DEFAULT_TTL;
    unsigned long _dataResendDelayMs = LHR_DEFAULT_RESEND_DELAY_MS;
    unsigned long _rfcnWaitDelayMs   = LHR_DEFAULT_RFCN_WAIT_MS;


    // ------------------------------------------------------------------
    // Runtime State Flags
    // ------------------------------------------------------------------

    volatile bool _packetReceived    = false;
    volatile bool _working           = false;
    volatile bool _waitingForDataRes = false;

    volatile bool _txInProgress      = false;
    volatile bool _txDone            = false;


    // ------------------------------------------------------------------
    // Beacon timing
    // ------------------------------------------------------------------
    
    unsigned long _lastBeaconMs     = 0;
    unsigned long _beaconIntervalMs = LHR_DEFAULT_BEACON_INTERVAL_MS;
    volatile bool _beaconDue        = true;
    unsigned long _minTriggeredUpdateGapMs = 1000;  // Minimum spacing between event-triggered NDAT advertisements.
                                                    // Prevents excessive airtime usage when routes change rapidly.


    // ------------------------------------------------------------------
    // Tx Watchdog
    // ------------------------------------------------------------------

    unsigned long _txStartMs         = 0;       // LHR_MILLIS() when the current transmit began
    unsigned long _txMaxDurationMs   = 10000;
    // Force-recover if TX hasn't completed by this point. 10s comfortably
    // exceeds worst-case airtime at SF9-SF12 for max packet size (255B) —
    // adjust via setTxWatchdogTimeout() if using a slower SF or larger PHY size.


    // ------------------------------------------------------------------
    // DATA_RES State Machine
    // ------------------------------------------------------------------

    unsigned long  _dataResStartTime = 0;
    unsigned long  _rfcnSendTime     = 0;
    volatile bool  _dataResend       = false;
    volatile bool  _requestedRFCN    = false;
    volatile bool  _dataResCycleActive = false;
    uint8_t _localRetryCount    = 0;
    uint8_t _maxLocalRetries    = LHR_DEFAULT_MAX_LOCAL_RETRIES;  // Maximum number of DATA_RES retry cycles before dropping a packet. One cycle takes roughly 15s


    // ------------------------------------------------------------------
    // Forward buffer (holds packet being relayed until ACK received)
    // ------------------------------------------------------------------

    uint8_t _forwardBuf[LHR_DATA_HEADER_LEN + LHR_MAX_PAYLOAD];
    size_t  _forwardLen = 0;


    // ------------------------------------------------------------------
    // RX buffer (stable storage for data callback — avoids stack-pointer issue)
    // ------------------------------------------------------------------

    uint8_t _rxBuf[LORA_PHY_MAX_PACKET_SIZE];
    size_t  _rxLen = 0;


    // ------------------------------------------------------------------
    // Routing state
    // ------------------------------------------------------------------

    lhr_neighbor_t _neighbors[LHR_MAX_NEIGHBORS];
    uint8_t    _neighborCount        = 0;
    uint32_t   _bestNeighborId       = 0;
    uint32_t   _secondBestNeighborId = 0;
    uint32_t   _lastPacketSenderId   = 0;
    uint8_t _seenIndex = 0;

    struct lhr_seen_entry_t {
       uint32_t source;
       uint8_t seq;
       bool valid; 
    };
    lhr_seen_entry_t _seenPackets[LHR_SEEN_CACHE_SIZE] = {};


    // ------------------------------------------------------------------
    // User callback
    // ------------------------------------------------------------------

    LHR_DataReceivedCallback _dataReceivedCb = nullptr;


    // ------------------------------------------------------------------
    // Duty Cycle
    // ------------------------------------------------------------------

    bool          _useDutyCycleLimit     = false;
    float         _dutyCyclePercent      = 0;           // Sets the percentage of the duty cycle window that may be used for transmission. / 0 disables the limit.
    unsigned long _dutyCycleStartTime    = 0;                           
    unsigned long _dutyCycleUsedMs       = 0;


    // ------------------------------------------------------------------
    // Non-blocking NDAT jitter state
    // ------------------------------------------------------------------

    bool          _ndatPending     = false;
    unsigned long _ndatScheduledMs = 0;   // LHR_MILLIS() when _sendNDAT() was called
    unsigned long _ndatJitterMs    = 0;   // the randomly chosen delay
    unsigned long _lastTriggeredNdatMs = 0;


    // ------------------------------------------------------------------
    // ACK sequence tracking
    // ------------------------------------------------------------------

    uint8_t _pendingAckSeqNum = 0;      // Store the sequence number expected in DATA_RES.


    // ------------------------------------------------------------------
    // Internal PRNG (avoids dependency on external random library)
    // ------------------------------------------------------------------

    uint32_t _randState = 0;


    // ------------------------------------------------------------------
    // Encryption
    // ------------------------------------------------------------------
    
    #if LHR_ENCRYPTION_SUPPORTED

        bool     _encryptionEnabled = false;
        bool     _encryptionKeySet  = false;
        uint8_t  _encryptionKey[16];
        uint32_t _encryptionNonceCounter;
        uint32_t _encryptionNonceReserved;

    #endif // LHR_ENCRYPTION_SUPPORTED



    // ------------------------------------------------------------------
    // Internal — lifecycle
    // ------------------------------------------------------------------

    void _reset();


    // ------------------------------------------------------------------
    // Internal — Rx
    // ------------------------------------------------------------------

    lhr_update_result_t  _checkLoraData();
    void _handleDATA   (uint8_t* buf, size_t len, float rssi, float snr);
    void _handleNDAT   (uint8_t* buf, size_t len, float rssi);
    void _handleRFCN   (uint8_t* buf, size_t len);
    bool _handleDATARES(uint8_t* buf, size_t len);

    bool _isDuplicateData(uint32_t source, uint8_t seq);


    // ------------------------------------------------------------------
    // Internal — Data Forwarding and Confirmation
    // ------------------------------------------------------------------

    /**
     * @brief Retransmits the currently buffered packet (_forwardBuf) toward
     *        Home, updating the sender Id to this node and choosing the
     *        next-hop receiver.
     *
     * Loop avoidance: if our best neighbor is the same node that sent us
     * this packet, we route to the second-best neighbor instead (unless
     * it's our only neighbor, in which case there's no alternative).
     */
    void _forwardDataPacket();

    /**
     * @brief Non-blocking DATA_RES confirmation/retry state machine.
     *        Called every update() while _waitingForDataRes is true.
     *
     * Cycle (relative to _dataResStartTime, reset to 0 at cycle start):
     *   t=0                        : initial forward transmit, _localRetryCount++
     *   t=_dataResendDelayMs       : first resend attempt (if still unconfirmed)
     *   t=_dataResendDelayMs*2     : escalate — reset neighbor table, send RFCN
     *   t=(RFCN send)+_rfcnWaitMs  : RFCN wait elapsed, cycle resets to t=0
     *                                (next update() call restarts the cycle)
     *
     * Ends when either _handleDATARES() calls _resetDataResState() (success)
     * or _localRetryCount reaches _maxLocalRetries (packet dropped).
     */
    lhr_update_result_t  _confirmDataRes   ();
    void                 _resetDataResState();


    // ------------------------------------------------------------------
    // Internal — Tx
    // ------------------------------------------------------------------

    /**
     * @brief Attempts to start an asynchronous radio transmission,
     *        subject to duty-cycle budget and channel-activity checks.
     *
     * On success, the transmission is in-flight — completion is handled
     * later in update() via the _txInProgress/_txDone flags, not here.
     *
     * @param buf Packet bytes to transmit.
     * @param len Packet length.
     * @return LHR_TX_OK if transmission started; otherwise the reason it didn't
     *         (channel busy, over duty-cycle budget, or radio error).
     */
    lhr_tx_result_t _transmit           (const uint8_t* buf, size_t len);
    void            _startReceive       ();
    void            _sendDATARES        (uint32_t receiverId, uint8_t seqNum);
    

    // ------------------------------------------------------------------
    // Internal — Beacon / Route Advertisement
    // ------------------------------------------------------------------

    void _sendNDAT        ();
    void _sendRFCN        ();
    void _sendBeacon      ();
    void _sendNdatIfNeeded();
    

    // ------------------------------------------------------------------
    // Internal — Routing
    // ------------------------------------------------------------------

    int       _findNeighbor       (uint32_t Id);
    lhr_err_t _updateNeighbor     (uint32_t Id, uint8_t hops, float rssi);
    int       _findBestNeighbor   ();
    void      _resetNeighborTable ();
    void      _pruneStaleNeighbors();


    // ------------------------------------------------------------------
    // Internal — Packet builders and Storage
    // ------------------------------------------------------------------

    void      _storeForwardPacket(uint8_t* buf, size_t len);
    void      _buildNDAT         (uint8_t* buf);
    /**
     * @brief Fills in the fixed DATA packet header fields (magic, type,
     *        source/sender/receiver Ids, sequence number, TTL) at the
     *        start of buf. Does not write the payload.
     *
     * Also assigns _pendingAckSeqNum to the sequence number used, so the
     * subsequent DATA_RES can be matched against it (see _handleDATARES()).
     *
     * @param buf Destination buffer, must be at least LHR_DATA_HEADER_LEN bytes.
     * @param ttl TTL value to write into the header.
     */
    void      _buildDataHeader   (uint8_t* buf, uint8_t ttl);
    void      _buildDATARES      (uint8_t* buf, uint32_t receiverId, uint8_t seqNum);


    // ------------------------------------------------------------------
    // Internal — PRNG
    // ------------------------------------------------------------------

    // Lightweight XORshift generator used for jitter/random delays.
    // Avoids dependency on platform-specific random functions.
    uint32_t  _random(uint32_t min, uint32_t max);


    // ------------------------------------------------------------------
    // Internal — Duty Cycle
    // ------------------------------------------------------------------

    // Duty cycle window
    void _updateDutyCycleWindow();

    // Duty cycle calculations
    unsigned long _dutyCycleLimitMs() const;
    unsigned long _dutyCycleRemainingMs() const;

    // Duty cycle accounting
    void _recordDutyCycleUsage(size_t len);


    // ------------------------------------------------------------------
    // Encryption
    // ------------------------------------------------------------------
    
    #if LHR_ENCRYPTION_SUPPORTED

        lhr_err_t _buildEncryptedDataPacket(uint8_t* buf, uint8_t ttl, const uint8_t* payload, size_t len);
        lhr_err_t _buildEncryptedRFCN(uint8_t* buf);
        lhr_err_t _buildEncryptedNDAT(uint8_t* buf);
        lhr_err_t _buildEncryptedDATARES(uint8_t* buf, uint32_t receiverId, uint8_t seqNum);

        lhr_err_t _verifyAndDecryptDataPacket(uint8_t* buf, size_t len, uint8_t* outPacket, size_t* outPacketLen);
        lhr_err_t _verifyAndDecryptNDAT(uint8_t* buf, size_t len, uint32_t* outSenderId, uint8_t* outHops);
        lhr_err_t _verifyRFCN(uint8_t* buf, size_t len);
        lhr_err_t _verifyAndDecryptDATARES(uint8_t* buf, size_t len, uint8_t* outSeqNum);

        /**
         * @brief Encrypts a single packet payload and produces its MIC.
         *
         * Thin wrapper around aes128_ccm_encrypt() that fixes the key, nonce
         * length, and tag length to this device's configuration.
         *
         * @param[in]  nonce         Full nonce, LHR_NONCE_LEN bytes.
         * @param[in]  aad           Additional authenticated data (may be nullptr if aadLen == 0).
         * @param[in]  aadLen        Length of aad.
         * @param[in]  plaintext     Data to encrypt.
         * @param[in]  len           Length of plaintext (and outCiphertext).
         * @param[out] outCiphertext Buffer for the ciphertext, must be >= len bytes.
         * @param[out] outTag        Buffer for the MIC, must be LHR_MIC_LEN bytes.
         *
         * @return LHR_OK on success, LHR_ERR_ENCRYPT_FAIL on failure.
         */
        lhr_err_t _ccmEncrypt(const uint8_t* nonce,
                      const uint8_t* aad, size_t aadLen,
                      const uint8_t* plaintext, size_t len,
                      uint8_t* outCiphertext, uint8_t* outTag);
        
        /**
         * @brief Decrypts a single packet payload and verifies its MIC.
         *
         * Thin wrapper around aes128_ccm_decrypt() that fixes the key, nonce
         * length, and tag length to this device's configuration.
         *
         * @param[in]  nonce        Full nonce, LHR_NONCE_LEN bytes.
         * @param[in]  aad          Additional authenticated data (may be nullptr if aadLen == 0).
         * @param[in]  aadLen       Length of aad.
         * @param[in]  ciphertext   Data to decrypt.
         * @param[in]  len          Length of ciphertext (and outPlaintext).
         * @param[in]  tag          MIC to verify against, LHR_MIC_LEN bytes.
         * @param[out] outPlaintext Buffer for the plaintext, must be >= len bytes.
         *                          Zeroed by the underlying library on auth failure.
         *
         * @return LHR_OK if the MIC is valid, LHR_ERR_DECRYPT_FAIL otherwise.
         */
        lhr_err_t _ccmDecrypt(const uint8_t* nonce,
                            const uint8_t* aad, size_t aadLen,
                            const uint8_t* ciphertext, size_t len,
                            const uint8_t* tag,
                            uint8_t* outPlaintext);
                            
        /**
         * @brief Builds the full CCM nonce for the next outgoing message.
         *
         * @warning outWireCounter holds the FULL 4-byte counter. Only the lower
         *          3 bytes are transmitted on the wire; masking happens at
         *          packet build time, not here.
         *
         * @param[out] outNonce       Buffer for the full nonce (LHR_NONCE_LEN bytes).
         * @param[out] outWireCounter Full 4-byte counter used for this message.
         *
         * @return LHR_OK, LHR_ERR_NONCE_EXHAUSTED, or LHR_ERR_STORE_WRITE_FAIL.
         */
        lhr_err_t _buildNonce(uint8_t* outNonce, uint32_t* outWireCounter);


        /**
         * @brief Initialise the RAM nonce counter from persistent storage and
         *        reserve the next batch (+100) in the store.
         *
         * Must be called before the first _nextNonceCounter() call, typically
         * from enableEncryption(true).
         *
         * @return LHR_OK on success.
         *         LHR_ERR_STORE_NOT_INIT if the storage backend could not be opened.
         *         LHR_ERR_STORE_WRITE_FAIL if the initial reservation write failed.
         */
        lhr_err_t _initNonceCounter();


        /**
         * @brief Get the next nonce counter value and advance the RAM counter.
         *
         * Transparently persists a new reservation batch (+100) to storage
         * whenever the RAM counter reaches the currently reserved boundary.
         *
         * @param outCounter Output: the counter value to use for this packet.
         * @return LHR_OK on success.
         *         LHR_ERR_NONCE_EXHAUSTED if the counter would overflow.
         *         LHR_ERR_STORE_WRITE_FAIL if a required batch write failed.
         */
        lhr_err_t _nextNonceCounter(uint32_t* outCounter);

        /**
         * @brief Initialise the persistent storage backend for the nonce counter.
         * @return true on success, false if storage could not be opened.
         */
        bool _storageInit();

        /**
         * @brief Load the 32-bit nonce counter from permanent storage.
         * @param counter Output: loaded value (set to 0 on first run).
         * @return true if a stored value was found, false on first run.
         */
        bool _loadNonceCounter(uint32_t* counter);

        /**
         * @brief Store the 32-bit nonce counter permanently.
         * @param counter Value to persist.
         * @return true on success.
         */
        bool _storeNonceCounter(uint32_t counter);

        /**
         * @brief Set the nonce counter to a specific value and store it.
         * @param counter New value.
         * @return true on success.
         */
        bool _setNonceCounter(uint32_t counter);

        /**
         * @brief Reset the nonce counter to 0 and store it.
         * @return true on success.
         */
        bool _resetNonceCounter();

    #endif // LHR_ENCRYPTION_SUPPORTED


    // ------------------------------------------------------------------
    // External RadioLib API Connections
    // ------------------------------------------------------------------

    int16_t _lastRadioError = RADIOLIB_ERR_NONE;


    // ------------------------------------------------------------------
    // Internal — Debug / Experimental
    // ------------------------------------------------------------------

    uint8_t _minimumAcceptedHops = 0;
    
    // Routing metadata used for debugging and testing only.
    // Not intended for normal application use.
    void _appendHopMetadata(float rssi, float snr);
    bool _routingDataEnabled = false;
};
