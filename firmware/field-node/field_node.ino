/**
 * Field Node Firmware (nRF24L01)
 * Target: ATmega328P (Arduino Nano / Pro Mini 3.3V)
 *
 * Three firmware roles selected via config.h defines:
 *
 *  Default (NODE_ID 1 or 2):
 *    Normal node — read sensors, TX to gateway, listen for LED cmd.
 *
 *  IS_RELAY_NODE (NODE_ID 3, Eng Block):
 *    Same as above PLUS:
 *    - Opens extra pipe listening for Node 5's packets
 *    - Forwards them to the gateway using Node 5's gateway TX address
 *    - Opens relay-cmd pipe; forwards gateway LED commands to Node 5
 *    - No deep sleep (must stay awake to relay)
 *
 *  VIA_RELAY (NODE_ID 5, PNRB):
 *    Sends data to relay (Node 3) instead of gateway directly.
 *    Listens for LED cmd on its own CMD address (sent by relay).
 *
 * Libraries: RF24, DHT sensor library, Adafruit Unified Sensor, avr/sleep
 */

#include <SPI.h>
#include <RF24.h>
#include <DHT.h>
#ifndef IS_RELAY_NODE
  #include <avr/sleep.h>
  #include <avr/wdt.h>
#endif
#include "config.h"

// ── Packet structs ───────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    float    temperature;
    float    humidity;
    uint16_t light;
    uint8_t  led_state;
} SensorPacket;

// 2-byte command the gateway sends to the relay, asking it to
// forward a LED command to a specific node.
typedef struct __attribute__((packed)) {
    uint8_t dest_node_id;
    uint8_t cmd;          // 1 = ON, 0 = OFF
} RelayCmd;

// ── Globals ──────────────────────────────────────────────────────
RF24 radio(RF24_CE, RF24_CSN);
DHT  dht(DHT_PIN, DHT_TYPE);

uint8_t       ledState = 0;
unsigned long lastSend = 0;

#ifndef IS_RELAY_NODE
ISR(WDT_vect) {}
#endif

// ── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    dht.begin();
    delay(2000);

    initRadio();

    Serial.print(F("[Node "));
    Serial.print(NODE_ID);
#ifdef IS_RELAY_NODE
    Serial.println(F("] Online (RELAY)"));
#elif defined(VIA_RELAY)
    Serial.println(F("] Online (via relay)"));
#else
    Serial.println(F("] Online"));
#endif
}

// ── Main loop ────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    if (now - lastSend >= SEND_INTERVAL_MS || lastSend == 0) {
        lastSend = now;
        sendData();
        listenForCommand();
    }

#ifdef IS_RELAY_NODE
    // Relay stays awake — poll for node 5 packets continuously
    pollRelay();
#else
    sleepMCU();
#endif
}

// ── Radio init ───────────────────────────────────────────────────
void initRadio() {
    if (!radio.begin()) {
        Serial.println(F("[ERROR] Radio init failed"));
        while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(200); }
    }

    radio.setPALevel(RADIO_PA_LEVEL);
    radio.setDataRate(RADIO_DATA_RATE);
    radio.setChannel(RADIO_CHANNEL);
    radio.setRetries(RADIO_RETRY_DELAY, RADIO_RETRY_COUNT);
    radio.setPayloadSize(sizeof(SensorPacket));  // largest packet size

#ifdef VIA_RELAY
    // Node 5: TX to relay (Node 3), RX for cmd from relay
    radio.openWritingPipe(ADDR_RELAY_BASE + NODE_ID);
    radio.openReadingPipe(1, ADDR_CMD_BASE + NODE_ID);
#elif defined(IS_RELAY_NODE)
    // Node 3: TX to gateway (own data + forwarded data), 3 RX pipes:
    //   pipe 1 — LED cmd from gateway for itself
    //   pipe 2 — sensor data from Node 5 to be relayed
    //   pipe 3 — relay-cmd from gateway (to forward to Node 5)
    radio.openWritingPipe(ADDR_BASE + NODE_ID);
    radio.openReadingPipe(1, ADDR_CMD_BASE + NODE_ID);
    radio.openReadingPipe(2, ADDR_RELAY_BASE + RELAY_SRC_NODE_ID);
    radio.openReadingPipe(3, ADDR_RELAY_CMD_BASE);
#else
    // Normal node: TX to gateway, RX for cmd from gateway
    radio.openWritingPipe(ADDR_BASE + NODE_ID);
    radio.openReadingPipe(1, ADDR_CMD_BASE + NODE_ID);
#endif
}

// ── Read sensors and transmit ────────────────────────────────────
void sendData() {
    SensorPacket pkt;
    pkt.node_id     = NODE_ID;
    pkt.temperature = dht.readTemperature();
    pkt.humidity    = dht.readHumidity();
    pkt.light       = analogRead(LDR_PIN);
    pkt.led_state   = ledState;

    if (isnan(pkt.temperature) || isnan(pkt.humidity)) {
        Serial.println(F("[WARN] Sensor read failed"));
        return;
    }

    radio.stopListening();

#ifdef IS_RELAY_NODE
    // Relay node: TX address is already set to gateway
    radio.openWritingPipe(ADDR_BASE + NODE_ID);
#endif

    bool ok = radio.write(&pkt, sizeof(pkt));

    Serial.print(F("TX T="));
    Serial.print(pkt.temperature, 1);
    Serial.print(F(" H="));
    Serial.print(pkt.humidity, 1);
    Serial.print(F(" L="));
    Serial.print(pkt.light);
    Serial.println(ok ? F(" OK") : F(" FAIL"));

    radio.startListening();
}

// ── Listen for LED command ───────────────────────────────────────
void listenForCommand() {
    radio.startListening();
    unsigned long start = millis();

    while (millis() - start < LISTEN_WINDOW_MS) {
        uint8_t pipe;
        if (radio.available(&pipe)) {
            if (pipe == 1) {   // own CMD pipe
                uint8_t cmd;
                radio.read(&cmd, 1);
                ledState = (cmd == 1) ? 1 : 0;
                digitalWrite(LED_PIN, ledState);
                Serial.print(F("LED -> "));
                Serial.println(ledState ? F("ON") : F("OFF"));
            } else {
                // Not our cmd — skip (shouldn't happen here)
                uint8_t buf[32];
                radio.read(buf, radio.getPayloadSize());
            }
            break;
        }
    }

    radio.stopListening();
}

// ── Relay-specific: poll and forward ────────────────────────────
#ifdef IS_RELAY_NODE
void pollRelay() {
    radio.startListening();
    unsigned long start = millis();

    while (millis() - start < RELAY_POLL_MS) {
        uint8_t pipe;
        if (!radio.available(&pipe)) continue;

        if (pipe == 2) {
            // Node 5 sensor data — forward to gateway
            SensorPacket pkt;
            radio.read(&pkt, sizeof(pkt));
            radio.stopListening();

            radio.openWritingPipe(ADDR_BASE + pkt.node_id);
            bool ok = radio.write(&pkt, sizeof(pkt));

            Serial.print(F("[RELAY] Fwd Node "));
            Serial.print(pkt.node_id);
            Serial.println(ok ? F(" OK") : F(" FAIL"));

            radio.openWritingPipe(ADDR_BASE + NODE_ID);  // restore own TX
            radio.startListening();

        } else if (pipe == 3) {
            // Gateway relay-cmd — extract dest + cmd, forward to target node
            RelayCmd rc;
            radio.read(&rc, sizeof(rc));
            radio.stopListening();

            radio.openWritingPipe(ADDR_CMD_BASE + rc.dest_node_id);
            bool ok = radio.write(&rc.cmd, 1);

            Serial.print(F("[RELAY] CMD Node "));
            Serial.print(rc.dest_node_id);
            Serial.print(F(" LED "));
            Serial.println(rc.cmd ? F("ON") : F("OFF"));
            Serial.println(ok ? F(" relayed OK") : F(" relay FAIL"));

            radio.openWritingPipe(ADDR_BASE + NODE_ID);  // restore own TX
            radio.startListening();

        } else if (pipe == 1) {
            // Own LED cmd
            uint8_t cmd;
            radio.read(&cmd, 1);
            ledState = (cmd == 1) ? 1 : 0;
            digitalWrite(LED_PIN, ledState);
            Serial.print(F("LED -> "));
            Serial.println(ledState ? F("ON") : F("OFF"));
        }
    }

    radio.stopListening();
}
#endif

// ── ATmega328P power-down sleep (non-relay nodes only) ──────────
#ifndef IS_RELAY_NODE
void sleepMCU() {
    MCUSR &= ~(1 << WDRF);
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = (1 << WDIE) | (1 << WDP3) | (1 << WDP0);   // 8s

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}
#endif
