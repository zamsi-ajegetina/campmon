/**
 * Field Node Simulation (Wokwi)
 * Hardware: Arduino Uno + nRF24L01 + DHT22 + LDR + LED
 *
 * Wokwi project URL (after uploading diagram.json):
 *   https://wokwi.com/projects/new/arduino-uno
 *
 * Behaviour:
 *   - Reads DHT22 every SEND_INTERVAL ms
 *   - Reads LDR from A0
 *   - Packs into SensorPacket struct and transmits via nRF24L01
 *   - Listens for LED command from gateway after each TX
 */

#include <SPI.h>
#include <RF24.h>
#include <DHT.h>

// ── Pin definitions ──────────────────────────────────────────────
#define DHT_PIN     7
#define DHT_TYPE    DHT22
#define LDR_PIN     A0
#define LED_PIN     4
#define RF24_CE     9
#define RF24_CSN    10

// ── Node configuration ───────────────────────────────────────────
#define NODE_ID     1          // Change per node: 1, 2, or 3
#define SEND_INTERVAL 30000    // ms between sensor reads (30s)

// ── nRF24L01 addresses ───────────────────────────────────────────
// Gateway listens on pipe 0; each node gets a unique TX address
// matching the gateway's pipe for that node.
const uint64_t TX_ADDRESS = 0xF0F0F0F0A1LL + NODE_ID; // node → gateway
const uint64_t RX_ADDRESS = 0xF0F0F0F000LL + NODE_ID; // gateway → node

// ── Packet struct (must match gateway) ──────────────────────────
typedef struct {
    uint8_t  node_id;
    float    temperature;
    float    humidity;
    uint16_t light;
    uint8_t  led_state;
} SensorPacket;

RF24 radio(RF24_CE, RF24_CSN);
DHT dht(DHT_PIN, DHT_TYPE);

uint8_t ledState = 0;
unsigned long lastSend = 0;

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    dht.begin();

    if (!radio.begin()) {
        Serial.println(F("[ERROR] nRF24L01 not responding"));
        while (1);
    }

    radio.setPALevel(RF24_PA_LOW);      // LOW for bench test; HIGH for field
    radio.setDataRate(RF24_250KBPS);    // Best range
    radio.setChannel(76);               // Avoid WiFi overlap (ch 1,6,11)
    radio.setRetries(5, 15);            // 5 retries, 15*250us delay
    radio.setPayloadSize(sizeof(SensorPacket));

    // Open TX pipe to gateway, RX pipe for commands from gateway
    radio.openWritingPipe(TX_ADDRESS);
    radio.openReadingPipe(1, RX_ADDRESS);

    Serial.print(F("[Node "));
    Serial.print(NODE_ID);
    Serial.println(F("] Ready"));
}

void loop() {
    unsigned long now = millis();

    if (now - lastSend >= SEND_INTERVAL || lastSend == 0) {
        lastSend = now;
        sendSensorData();
    }

    // Check for incoming LED command (non-blocking)
    radio.startListening();
    delay(200);   // brief listen window

    if (radio.available()) {
        uint8_t cmd;
        radio.read(&cmd, 1);
        ledState = (cmd == 1) ? 1 : 0;
        digitalWrite(LED_PIN, ledState);
        Serial.print(F("[Node "));
        Serial.print(NODE_ID);
        Serial.print(F("] LED -> "));
        Serial.println(ledState ? "ON" : "OFF");
    }

    radio.stopListening();
}

void sendSensorData() {
    SensorPacket pkt;
    pkt.node_id     = NODE_ID;
    pkt.temperature = dht.readTemperature();
    pkt.humidity    = dht.readHumidity();
    pkt.light       = analogRead(LDR_PIN);
    pkt.led_state   = ledState;

    // Validate DHT reading
    if (isnan(pkt.temperature) || isnan(pkt.humidity)) {
        Serial.println(F("[WARN] DHT read failed, skipping"));
        return;
    }

    radio.stopListening();
    bool ok = radio.write(&pkt, sizeof(pkt));

    Serial.print(F("[Node "));
    Serial.print(NODE_ID);
    Serial.print(F("] TX -> T="));
    Serial.print(pkt.temperature, 1);
    Serial.print(F("C H="));
    Serial.print(pkt.humidity, 1);
    Serial.print(F("% L="));
    Serial.print(pkt.light);
    Serial.print(F(" | "));
    Serial.println(ok ? "ACK" : "NO ACK");
}
