// ============================================================
// Lighthouse Reckoning — Home node example (ESP32 + SX1262)
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

// ---------------- Globals ----------------
SX1262*             radio;
LighthouseReckoning lhr;


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
    lhr.handleDio1Rise();
}

void setupLoRaReceiveInterrupt() {
    pinMode(PIN_DIO1, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_DIO1),
                     onDio1Rise,     // ISR callback for TX-done / packet reception
                     RISING);
}


// ============================================================
// LHR callback — fires whenever a DATA packet reaches Home
// ============================================================

void onDataReceived(uint8_t* buf, size_t len) {
    uint32_t sourceId =
        ((uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 0] << 24) |
        ((uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 1] << 16) |
        ((uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 2] <<  8) |
         (uint32_t)buf[LHR_DATA_OFFSET_SOURCE + 3];

    uint8_t* payload    = buf + LHR_DATA_OFFSET_PAYLOAD;
    size_t   payloadLen = len - LHR_DATA_HEADER_LEN;

    Serial.printf("\n[HOME] Packet from 0x%08X | TTL: %d | %d bytes\n",
        sourceId, buf[LHR_DATA_OFFSET_TTL], payloadLen);

    Serial.print("[HOME] Payload: ");
    for (size_t i = 0; i < payloadLen; i++) {
        Serial.printf("%02X ", payload[i]);
    }
    Serial.println();
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
        Serial.println("Fehler beim Initialisieren! Pruefe Pins und Stromversorgung.");
        while (true) { delay(1000); }
    }

    setupLoRaReceiveInterrupt();

    lhr.beginAsHome(radio, getDeviceId());

    lhr.onDataReceived(onDataReceived);

    Serial.println("Setup done.");
}

void loop() {
    lhr.update();
}