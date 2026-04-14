/**
 * Gateway Simulation (Wokwi)
 * Hardware: ESP32 + nRF24L01
 *
 * In simulation: MQTT is stubbed to Serial output.
 * On real hardware: replace Serial.println with mqtt.publish().
 *
 * Behaviour:
 *   - Listens on 3 nRF24 pipes (one per near node)
 *   - Prints received data to Serial (simulating MQTT publish)
 *   - Accepts commands via Serial ("1:ON", "2:OFF", etc.)
 *     and forwards LED command to the specified node
 */

#include <SPI.h>
#include <RF24.h>

// ── Pin definitions (ESP32) ──────────────────────────────────────
#define RF24_CE    22
#define RF24_CSN   21

// ── nRF24L01 pipe addresses ──────────────────────────────────────
// Node i uses TX address 0xF0F0F0F0A0 + i
// Gateway opens reading pipe i at that same address
const uint64_t NODE_ADDRS[3] = {
    0xF0F0F0F0A1LL,   // Node 1 → pipe 1
    0xF0F0F0F0A2LL,   // Node 2 → pipe 2
    0xF0F0F0F0A3LL    // Node 3 → pipe 3
};
// Gateway TX back to node i uses RX address of that node
const uint64_t NODE_CMD_ADDRS[3] = {
    0xF0F0F0F001LL,
    0xF0F0F0F002LL,
    0xF0F0F0F003LL
};

// ── Packet struct (must match field node) ────────────────────────
typedef struct {
    uint8_t  node_id;
    float    temperature;
    float    humidity;
    uint16_t light;
    uint8_t  led_state;
} SensorPacket;

RF24 radio(RF24_CE, RF24_CSN);

void setup() {
    Serial.begin(115200);
    delay(500);

    if (!radio.begin()) {
        Serial.println("[ERROR] nRF24L01 not responding");
        while (1);
    }

    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(76);
    radio.setPayloadSize(sizeof(SensorPacket));

    // Open reading pipes for each near node
    for (int i = 0; i < 3; i++) {
        radio.openReadingPipe(i + 1, NODE_ADDRS[i]);
    }

    radio.startListening();
    Serial.println("[Gateway] Ready — listening on 3 pipes");
    Serial.println("[Gateway] Serial commands: <node_id>:<ON|OFF>  e.g. '1:ON'");
}

void loop() {
    // ── Receive sensor data ─────────────────────────────────────
    uint8_t pipe;
    if (radio.available(&pipe)) {
        SensorPacket pkt;
        radio.read(&pkt, sizeof(pkt));
        publishToMQTT(pkt);
    }

    // ── Handle Serial commands (simulating web UI in Wokwi) ─────
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        parseAndSendCommand(cmd);
    }
}

void publishToMQTT(const SensorPacket &pkt) {
    // In simulation: print to Serial.
    // On real hardware: replace with mqtt.publish(topic, payload).
    char buf[80];

    snprintf(buf, sizeof(buf), "MQTT campus/sensors/%d/temperature %.1f",
             pkt.node_id, pkt.temperature);
    Serial.println(buf);

    snprintf(buf, sizeof(buf), "MQTT campus/sensors/%d/humidity %.1f",
             pkt.node_id, pkt.humidity);
    Serial.println(buf);

    snprintf(buf, sizeof(buf), "MQTT campus/sensors/%d/light %d",
             pkt.node_id, pkt.light);
    Serial.println(buf);

    Serial.print("MQTT campus/status/");
    Serial.print(pkt.node_id);
    Serial.println(" online");
}

// Parse "1:ON" or "2:OFF" and send LED command to that node
void parseAndSendCommand(const String &cmd) {
    int colonIdx = cmd.indexOf(':');
    if (colonIdx < 0) return;

    int nodeId = cmd.substring(0, colonIdx).toInt();
    String action = cmd.substring(colonIdx + 1);
    action.trim();

    if (nodeId < 1 || nodeId > 3) {
        Serial.println("[WARN] Node ID out of range (1-3)");
        return;
    }

    uint8_t ledCmd = (action == "ON") ? 1 : 0;

    radio.stopListening();
    radio.openWritingPipe(NODE_CMD_ADDRS[nodeId - 1]);
    bool ok = radio.write(&ledCmd, 1);
    radio.startListening();

    Serial.print("[Gateway] CMD Node ");
    Serial.print(nodeId);
    Serial.print(" LED -> ");
    Serial.print(action);
    Serial.println(ok ? " | ACK" : " | NO ACK");
}
