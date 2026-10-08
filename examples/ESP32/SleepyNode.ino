// ============================================================
// Lighthouse Reckoning — Sleepy Node example (ESP32 + SX1262)
// Demonstrates getNextUpdateInMs(): waits only as long as
// necessary between update() calls instead of polling in a
// tight loop. A DIO1 interrupt cuts the wait short at any time,
// so incoming packets are never missed.
// ============================================================

//#define LHR_DEBUG
#include <RadioLib.h>
#include <LighthouseReckoning.h>
#include <SPI.h>


// ---------------- Pins ----------------
#define LORA_MISO_PIN   16
#define LORA_MOSI_PIN   8
#define LORA_SCK_PIN    18
#define PIN_CS          17
#define PIN_DIO1        13
#define PIN_RST         11
#define PIN_BUSY        12

// ---------------- LoRa radio settings ----------------
constexpr float    LORA_FREQ     = 868.0;
constexpr float    LORA_BW       = 125.0;
constexpr uint8_t  LORA_SF       = 9;
constexpr uint8_t  LORA_CR       = 8;
constexpr uint8_t  LORA_SYNC     = 0xA4;
constexpr int8_t   LORA_POWER    = 22;
constexpr uint16_t LORA_PREAMBLE = 16;
constexpr bool     LORA_CRC      = true;
constexpr bool     LORA_LDRO     = false;

constexpr unsigned long SEND_INTERVAL_MS = 10000;

// ---------------- Globals ----------------
SX1262*             radio;
LighthouseReckoning lhr;
unsigned long       lastSend = 0;

// Set by the DIO1 ISR whenever RadioLib signals an event (RX done,
// TX done, ...). The main loop checks this to cut a wait short.
volatile bool dio1Triggered = false;


// ============================================================
// Setup helpers
// ============================================================

void startSPILora() {
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
}

void configureLoraRadio() {
    radio->setFrequency(LORA_FREQ);
    radio->setBandwidth(LORA_BW);
    radio->setSpreadingFactor(LORA_SF);
    radio->setCodingRate(LORA_CR);
    radio->setSyncWord(LORA_SYNC);
    radio->setOutputPower(LORA_POWER);
    radio->setPreambleLength(LORA_PREAMBLE);
    radio->setCRC(LORA_CRC);
    Serial.println("LoRa configured.");
}

uint32_t getDeviceId() {
    uint64_t mac = ESP.getEfuseMac();
    return (uint32_t)(mac >> 32) ^ (uint32_t)(mac & 0xFFFFFFFF);
}

// ── ISR ──────────────────────────────────────────────────────
void IRAM_ATTR onDio1Rise() {
    lhr.handleDio1Rise();   // hand the event to LHR (safe from ISR context)
    dio1Triggered = true;   // tell loop() to stop waiting and call update() now
}

void setupLoRaReceiveInterrupt() {
    pinMode(PIN_DIO1, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_DIO1),
                     onDio1Rise,     // ISR callback for TX-done / packet reception
                     RISING);
}


// ============================================================
// Setup / loop
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(3000);

    startSPILora();

    // Create radio module
    radio = new SX1262(new Module(PIN_CS, PIN_DIO1, PIN_RST, PIN_BUSY));

    int state = radio->begin(LORA_FREQ);
    configureLoraRadio();

    Serial.print("Init: ");
    Serial.println(state);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println("Initialization failed! Check the wiring and power supply.");
        while (true) { delay(1000); }
    }

    setupLoRaReceiveInterrupt();

    // Encryption example
    const uint8_t encryptionKey[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    };

    lhr.setEncryptionKey(encryptionKey, sizeof(encryptionKey));

    // Enable encryption by uncommenting the following lines:
    // lhr_err_t encErr = lhr.enableEncryption(true);
    // Serial.printf("enableEncryption() -> %d\n", encErr);

    lhr.beginAsNode(radio, getDeviceId());

    Serial.println("Setup done.");
}

void loop() {
    unsigned long now = millis();

    // 1. How long until LHR's own state machines (beacon, retries, ...)
    //    need update() called again?
    unsigned long lhrWaitMs = lhr.getNextUpdateInMs();

    // 2. How long until this sketch's own send interval is due?
    unsigned long sinceLastSend = now - lastSend;
    unsigned long sendWaitMs = (sinceLastSend >= SEND_INTERVAL_MS)
                                   ? 0
                                   : (SEND_INTERVAL_MS - sinceLastSend);

    // 3. The earlier of the two deadlines decides how long we may wait.
    unsigned long waitMs = min(lhrWaitMs, sendWaitMs);

    // 4. Wait, but let a DIO1 interrupt cut the wait short at any time —
    //    otherwise an incoming packet would sit unhandled until the
    //    wait elapses on its own.
    if (waitMs > 0) {
        unsigned long waitStart = millis();
        while ((unsigned long)(millis() - waitStart) < waitMs) {
            if (dio1Triggered) {
                dio1Triggered = false;
                break;
            }
            delay(1);
        }
    }

    // 5. Something is due now: LHR's timer, the send timer, or a
    //    DIO1 event. Let LHR process it.
    lhr.update();

    // 6. Send our own data if its interval has elapsed.
    now = millis();
    if ((unsigned long)(now - lastSend) >= SEND_INTERVAL_MS) {
        lastSend = now;

        // Random test payload
        uint8_t payload[16];
        for (uint8_t i = 0; i < sizeof(payload); i++) {
            payload[i] = random(0, 256);
        }

        lhr_err_t result = lhr.sendData(payload, sizeof(payload));
        Serial.printf("sendData() -> %d\n", result);
    }
}