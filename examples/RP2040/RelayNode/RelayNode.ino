// ============================================================
// Lighthouse Reckoning — Relay node example (Raspberry Pi Pico + SX1262)
// This node forwards DATA packets toward Home automatically.
// No application payload is sent — the node acts as a pure mesh relay.
// ============================================================

//#define LHR_DEBUG
#include <RadioLib.h>
#include <LighthouseReckoning.h>
#include <SPI.h>
#include <pico/stdlib.h>
#include <stdio.h>

// ---------------- Pins ----------------
#define LORA_MISO_PIN   16
#define LORA_MOSI_PIN   19
#define LORA_SCK_PIN    18
#define PIN_CS          17
#define PIN_DIO1        20
#define PIN_RST         21
#define PIN_BUSY        22

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

// ---------------- Globals ----------------
SX1262*             radio;
LighthouseReckoning lhr;

// ============================================================
// Setup helpers
// ============================================================

void startSPILora() {
    SPI.setSCK(LORA_SCK_PIN);
    SPI.setTX(LORA_MOSI_PIN);
    SPI.setRX(LORA_MISO_PIN);
    SPI.begin();
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

uint32_t getPicoId() {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    return ((uint32_t)id.id[4] << 24) |
           ((uint32_t)id.id[5] << 16) |
           ((uint32_t)id.id[6] << 8)  |
            (uint32_t)id.id[7];
}

// ── ISR ──────────────────────────────────────────────────────
void onDio1Rise() {
    lhr.handleDio1Rise();
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

    lhr.beginAsNode(radio, getPicoId());
}

void loop() {
    // Relay nodes require no application logic.
    // Forwarding happens automatically inside update():
    //   - Incoming DATA packets are relayed to the best known next-hop neighbor.
    //   - NDAT beacons are sent periodically to advertise the route to Home.
    //   - RFCN requests are sent when no route is known yet.
    lhr.update();
}