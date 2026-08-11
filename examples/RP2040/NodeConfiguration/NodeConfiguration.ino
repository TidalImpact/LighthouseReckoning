// ============================================================
// Lighthouse Reckoning — Configuration example (Raspberry Pi Pico + SX1262)
// Shows all configurable LHR parameters including duty cycle.
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

constexpr unsigned long SEND_INTERVAL_MS = 10000;

// ---------------- Globals ----------------
SX1262*             radio;
LighthouseReckoning lhr;
unsigned long       lastSend = 0;


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
                     onDio1Rise,
                     RISING);
}


// ============================================================
// Setup / loop
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(3000);

    startSPILora();

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

    lhr.beginAsNode(radio, getPicoId());

    // ── Routing ──────────────────────────────────────────────
    // TTL: max hops a DATA packet may travel before being dropped.
    lhr.setTTL(6);

    // Retry delays: ms before first resend, ms to wait after RFCN.
    lhr.setRetryDelays(4000, 4000);

    // Beacon interval: how often NDAT/RFCN is broadcast (ms).
    lhr.setBeaconInterval(60000);

    // TX watchdog: force radio back to RX if stuck in TX longer than this (ms).
    lhr.setTxWatchdogTimeout(5000);

    // Max retry cycles before a packet is dropped.
    lhr.setMaxLocalRetries(3);

    // ── Multi-hop testing ────────────────────────────────────
    // Rejects neighbors below the given hop count, forcing the node to
    // route through relays at least that many hops away. Useful for
    // testing multi-hop routing without moving hardware.
    // lhr.setMinimumAcceptedHops(1);   // reject Home directly, e.g.

    // ── Duty Cycle ───────────────────────────────────────────
    // EU868 default is 1% per hour — enabled by default.
    lhr.setDutyCycleLimit(1.0f);

    // Or set a fixed ms budget per hour instead:
    // lhr.setDutyCycleLimitMs(36000);

    // Disable entirely (Default):
    // lhr.toggleDutyCycleLimit(false);

    Serial.println("Setup done.");
}

void loop() {
    lhr.update();

    if (millis() - lastSend >= SEND_INTERVAL_MS) {
        lastSend = millis();

        Serial.printf("[DUTY] Used: %lu ms / %lu ms (%.1f%%)\n",
            lhr.getDutyCycleUsedMs(),
            lhr.getDutyCycleLimitMs(),
            lhr.getDutyCycleUsage());

        uint8_t payload[16];
        for (uint8_t i = 0; i < sizeof(payload); i++) {
            payload[i] = random(0, 256);
        }

        lhr_err_t result = lhr.sendData(payload, sizeof(payload));
        Serial.printf("sendData() -> %d\n", result);
    }
}