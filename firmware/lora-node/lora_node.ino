/**
 * LoRa Field Node Firmware
 * Target: ATmega328P (Arduino Nano / Pro Mini 3.3V)
 * Radio: LoRa SX1278 (433 MHz)
 *
 * Use for nodes >200m from gateway (Mech Workshop, PNRB).
 *
 * Libraries:  arduino-LoRa, DHT sensor library, Adafruit Unified Sensor
 */

#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include "config.h"

// ── Packet format (binary, 12 bytes) ────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    float    temperature;
    float    humidity;
    uint16_t light;
    uint8_t  led_state;
} SensorPacket;

DHT dht(DHT_PIN, DHT_TYPE);
uint8_t       ledState = 0;
unsigned long lastSend = 0;

ISR(WDT_vect) {}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    dht.begin();
    delay(2000);

    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQUENCY)) {
        Serial.println(F("[ERROR] LoRa init failed"));
        while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(300); }
    }

    LoRa.setSpreadingFactor(LORA_SPREADING);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setTxPower(LORA_TX_POWER);
    LoRa.setSyncWord(LORA_SYNC_WORD);

    Serial.print(F("[LoRa Node "));
    Serial.print(NODE_ID);
    Serial.println(F("] Online"));
}

void loop() {
    unsigned long now = millis();

    if (now - lastSend >= SEND_INTERVAL_MS || lastSend == 0) {
        lastSend = now;
        sendData();
        listenForCommand();
    }

    sleepMCU();
}

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

    // Transmit as raw binary packet
    LoRa.beginPacket();
    LoRa.write(GATEWAY_ADDR);         // destination
    LoRa.write(NODE_ID);              // source
    LoRa.write((uint8_t *)&pkt, sizeof(pkt));
    LoRa.endPacket();                 // blocking TX

    Serial.print(F("TX T="));
    Serial.print(pkt.temperature, 1);
    Serial.print(F(" H="));
    Serial.print(pkt.humidity, 1);
    Serial.print(F(" L="));
    Serial.println(pkt.light);
}

void listenForCommand() {
    unsigned long start = millis();

    while (millis() - start < LISTEN_WINDOW_MS) {
        int pktSize = LoRa.parsePacket();
        if (pktSize > 0) {
            uint8_t dest = LoRa.read();   // destination byte
            uint8_t src  = LoRa.read();   // source (gateway = 0x00)

            if (dest == NODE_ID && src == GATEWAY_ADDR && LoRa.available()) {
                uint8_t cmd = LoRa.read();
                ledState = (cmd == 1) ? 1 : 0;
                digitalWrite(LED_PIN, ledState);
                Serial.print(F("LED -> "));
                Serial.println(ledState ? F("ON") : F("OFF"));
            }
            break;
        }
    }
}

void sleepMCU() {
    MCUSR &= ~(1 << WDRF);
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = (1 << WDIE) | (1 << WDP3) | (1 << WDP0);   // 8s

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_cpu();
    sleep_disable();
}
