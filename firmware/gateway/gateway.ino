/**
 * Gateway Firmware
 * Target: ESP32 DevKit V1
 *
 * Receives LDR readings from 4 team field nodes and bridges to MQTT + web UI.
 * Supports LED on/off commands from the web UI and MQTT.
 *
 * Node topology:
 *  Node A (id=1, LoRa, far)           → gateway direct via LoRa
 *  Node B (id=2, nRF24, far)          → Node C relay → gateway via "GTWY1"
 *  Node C (id=3, nRF24, relay)        → gateway via "GTWY1" (own + forwarded B)
 *  Node D (id=4, nRF24, close/direct) → gateway via "GTWY2"
 *
 * Message format: char[32] "NodeX LDR:YYYY"
 * LED command format: char[32] "LED:1" or "LED:0"
 *
 * LED command routing:
 *  Node A → LoRa packet "LED:1/0" during STATE_LORA
 *  Node C → nRF24 TX to "CMDC1" during STATE_NRF
 *  Node D → nRF24 TX to "CMDD1" during STATE_NRF
 *  Node B → nRF24 TX to "CMDB1" (Node C receives + relays to Node B) during STATE_NRF
 *
 * Radio loop: STATE_LORA (2 min) → STATE_IDLE (1 min) → STATE_NRF (2 min) → repeat
 * Only one radio is active at a time (shared SPI bus).
 *
 * Libraries:
 *  RF24, arduino-LoRa, PubSubClient, ESPAsyncWebServer, AsyncTCP, ArduinoJson
 *
 * Hardware wiring (shared SPI — matches team-gateway):
 *  nRF24L01    → CE=14, CSN=15, SPI: SCK=18, MISO=19, MOSI=23
 *  LoRa SX1278 → SS=5,  RST=4,  DIO0=26, same SPI bus
 */

#include <SPI.h>
#include <RF24.h>
#include <LoRa.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "web_ui.h"

// ── State machine ────────────────────────────────────────────────
enum GatewayState { STATE_LORA, STATE_IDLE, STATE_NRF };
GatewayState currentState;
unsigned long stateStart;

#define LORA_LISTEN  120000UL
#define IDLE_PAUSE    60000UL
#define NRF_LISTEN   120000UL

// ── Per-node state (index 1–4; index 0 unused) ───────────────────
struct NodeState {
    uint16_t light    = 0;
    bool     online   = false;
    bool     ledOn    = false;
    uint32_t lastSeen = 0;
};
NodeState nodes[5];

// ── Pending LED commands (set by /control or MQTT) ───────────────
bool ledPending[5] = {};  // true = command waiting to be sent
bool ledTarget[5]  = {};  // desired LED state for each node

// ── nRF24 command TX addresses ───────────────────────────────────
const uint8_t CMD_ADDR_C[6] = "CMDC1";  // gateway → Node C LED
const uint8_t CMD_ADDR_D[6] = "CMDD1";  // gateway → Node D LED
const uint8_t CMD_ADDR_B[6] = "CMDB1";  // gateway → Node C (relay LED to Node B)

// ── Hardware objects ─────────────────────────────────────────────
RF24           radio(RF24_CE, RF24_CSN);
AsyncWebServer server(WEB_PORT);
WiFiClient     wifiClient;
PubSubClient   mqtt(wifiClient);

// ── Forward declarations ─────────────────────────────────────────
void connectWiFi();
void initMQTT();
void reconnectMQTT();
void initLoRa();
void initNRF24();
void initWebServer();
void startLoRa();
void startIdle();
void startNRF();
void parseLoRaPacket();
bool sendNrfLedCmd(uint8_t nodeId, bool on);
void sendLoRaLedCmd(bool on);
void publishLight(uint8_t nodeId, uint16_t ldr);
void publishStatus(uint8_t nodeId, bool online);
bool parseNodeMsg(const char *msg, uint8_t &nodeId, uint16_t &ldr);
void onNodeData(uint8_t nodeId, uint16_t ldr);

// ── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(3000);

    connectWiFi();
    initMQTT();
    initLoRa();
    initNRF24();
    initWebServer();

    currentState = STATE_LORA;
    stateStart   = millis();
    startLoRa();

    Serial.println("[Gateway] All systems ready");
}

// ── Main loop ────────────────────────────────────────────────────
void loop() {
    if (mqtt.connected()) {
        mqtt.loop();
    } else {
        static unsigned long _mqttRetry = 0;
        if (millis() - _mqttRetry >= 5000) {
            _mqttRetry = millis();
            reconnectMQTT();
        }
    }

    unsigned long now     = millis();
    unsigned long elapsed = now - stateStart;

    switch (currentState) {

        case STATE_LORA:
            if (LoRa.parsePacket()) {
                parseLoRaPacket();  // LED cmd for Node A is sent inside here, reactively
            }
            if (elapsed >= LORA_LISTEN) {
                currentState = STATE_IDLE;
                stateStart   = now;
                startIdle();
            }
            break;

        case STATE_IDLE:
            if (elapsed >= IDLE_PAUSE) {
                currentState = STATE_NRF;
                stateStart   = now;
                startNRF();
            }
            break;

        case STATE_NRF: {
            uint8_t pipe;
            if (radio.available(&pipe)) {
                char msg[32] = "";
                radio.read(msg, sizeof(msg));
                uint8_t nodeId = 0;
                if (pipe == 2) {
                    // Pipe 2 = ATWY1 = always Node D (id=4)
                    const char *p = strstr(msg, "LDR:");
                    if (p) {
                        nodeId = 4;
                        Serial.printf("[nRF24 pipe2/D] %s\n", msg);
                        onNodeData(4, (uint16_t)atoi(p + 4));
                    }
                } else {
                    // Pipe 1 = GTWY1 = Node B or C, parse by message string
                    uint8_t nid; uint16_t ldr;
                    if (parseNodeMsg(msg, nid, ldr) && nid >= 2 && nid <= 3) {
                        nodeId = nid;
                        Serial.printf("[nRF24 pipe1/%c] %s\n", nid == 2 ? 'B' : 'C', msg);
                        onNodeData(nid, ldr);
                    }
                }
                // Node just finished TX and is now in its 1s RX window — send immediately.
                // Only clear pending on confirmed ACK; keep true on FAIL so it retries next cycle.
                if (nodeId && ledPending[nodeId]) {
                    if (sendNrfLedCmd(nodeId, ledTarget[nodeId]))
                        ledPending[nodeId] = false;
                }
            }
            if (elapsed >= NRF_LISTEN) {
                currentState = STATE_LORA;
                stateStart   = now;
                startLoRa();
            }
            break;
        }
    }

    // Mark nodes offline after 2 minutes of silence
    // Use fresh millis() — 'now' was captured before onNodeData() updated lastSeen,
    // so now < lastSeen by a few ms, causing unsigned wrap to ~4B and instant offline flip.
    unsigned long ts = millis();
    for (int i = 1; i <= 4; i++) {
        if (nodes[i].online && (ts - nodes[i].lastSeen > 120000UL)) {
            nodes[i].online = false;
            publishStatus(i, false);
        }
    }
}

// ── State transitions ────────────────────────────────────────────
void startLoRa() {
    radio.stopListening();
    LoRa.receive();
    Serial.println("[State] LoRa — listening 2 min");
}

void startIdle() {
    LoRa.idle();
    radio.stopListening();
    Serial.println("[State] Idle — 1 min");
}

void startNRF() {
    LoRa.idle();
    radio.startListening();
    Serial.println("[State] nRF24 — listening 2 min");
}

// ── LED command senders ──────────────────────────────────────────
bool sendNrfLedCmd(uint8_t nodeId, bool on) {
    char cmd[32] = "";
    snprintf(cmd, sizeof(cmd), "LED:%d", on ? 1 : 0);

    const uint8_t *addr = nullptr;
    if      (nodeId == 3) addr = CMD_ADDR_C;
    else if (nodeId == 4) addr = CMD_ADDR_D;
    else if (nodeId == 2) addr = CMD_ADDR_B;  // Node C will relay to B
    else return false;

    radio.stopListening();
    radio.openWritingPipe(addr);
    bool ok = radio.write(cmd, sizeof(cmd));
    radio.startListening();  // re-enables GTWY1 + GTWY2 reading pipes

    if (ok) nodes[nodeId].ledOn = on;  // only update confirmed state on ACK
    Serial.printf("[nRF24 CMD] Node %d LED:%d %s\n", nodeId, on ? 1 : 0, ok ? "OK" : "FAIL");
    return ok;
}

void sendLoRaLedCmd(bool on) {
    char cmd[32] = "";
    snprintf(cmd, sizeof(cmd), "LED:%d", on ? 1 : 0);
    LoRa.beginPacket();
    LoRa.print(cmd);
    LoRa.endPacket();
    LoRa.receive();  // resume RX after TX
    Serial.printf("[LoRa CMD] Node 1 LED:%d\n", on ? 1 : 0);
}

// ── Message parser ───────────────────────────────────────────────
bool parseNodeMsg(const char *msg, uint8_t &nodeId, uint16_t &ldr) {
    if      (strncmp(msg, "NodeA", 5) == 0) nodeId = 1;
    else if (strncmp(msg, "NodeB", 5) == 0) nodeId = 2;
    else if (strncmp(msg, "NodeC", 5) == 0) nodeId = 3;
    else if (strncmp(msg, "NodeD", 5) == 0) nodeId = 4;
    else return false;
    const char *p = strstr(msg, "LDR:");
    if (!p) return false;
    ldr = (uint16_t)atoi(p + 4);
    return true;
}

void onNodeData(uint8_t nodeId, uint16_t ldr) {
    bool wasOffline        = !nodes[nodeId].online;
    nodes[nodeId].light    = ldr;
    nodes[nodeId].online   = true;
    nodes[nodeId].lastSeen = millis();
    publishLight(nodeId, ldr);
    if (wasOffline) publishStatus(nodeId, true);
}

// ── WiFi ─────────────────────────────────────────────────────────
void connectWiFi() {
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\n[WiFi] IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WiFi] Failed — continuing without network");
    }
}

// ── MQTT ─────────────────────────────────────────────────────────
void initMQTT() {
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback([](char *topic, byte *payload, unsigned int len) {
        // campus/led/<id> → ON or OFF
        char t[48]; strncpy(t, topic, sizeof(t));
        char *last = strrchr(t, '/');
        if (!last) return;
        int nodeId = atoi(last + 1);
        if (nodeId < 1 || nodeId > 4) return;
        bool on = (len > 0 && payload[0] == '1');
        ledPending[nodeId] = true;
        ledTarget[nodeId]  = on;
    });
    reconnectMQTT();
}

void reconnectMQTT() {
    // Non-blocking single attempt — loop() retries every 5 s if still disconnected
    Serial.print("[MQTT] Connecting...");
    if (mqtt.connect(MQTT_CLIENT)) {
        Serial.println("OK");
        mqtt.subscribe("campus/led/+");
    } else {
        Serial.printf("failed rc=%d — will retry\n", mqtt.state());
    }
}

void publishLight(uint8_t nodeId, uint16_t ldr) {
    if (!mqtt.connected()) return;
    char topic[48], val[8];
    snprintf(topic, sizeof(topic), "campus/sensors/%d/light", nodeId);
    snprintf(val,   sizeof(val),   "%d", ldr);
    mqtt.publish(topic, val, true);
}

void publishStatus(uint8_t nodeId, bool online) {
    char topic[40];
    snprintf(topic, sizeof(topic), "campus/status/%d", nodeId);
    mqtt.publish(topic, online ? "online" : "offline", true);
}

// ── LoRa (init first — matches team-gateway order) ───────────────
void initLoRa() {
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, LOW);  delay(500);
    digitalWrite(LORA_RST, HIGH); delay(500);

    int attempts = 0;
    while (!LoRa.begin(LORA_FREQUENCY)) {
        Serial.print("[LoRa] Attempt ");
        Serial.print(++attempts);
        Serial.println(" failed...");
        delay(500);
        if (attempts >= 10) {
            Serial.println("[ERROR] LoRa gave up");
            while (true);
        }
    }
    LoRa.setSpreadingFactor(LORA_SPREADING);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.idle();
    Serial.println("[LoRa] Ready");
}

// ── nRF24L01 ─────────────────────────────────────────────────────
void initNRF24() {
    if (!radio.begin()) {
        Serial.println("[ERROR] nRF24 init failed");
        Serial.print("Chip connected? ");
        Serial.println(radio.isChipConnected() ? "YES" : "NO");
        return;
    }
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(RADIO_CHANNEL);
    radio.openReadingPipe(1, (const uint8_t *)ADDR_GTWY1);
    radio.openReadingPipe(2, (const uint8_t *)ADDR_GTWY2);
    radio.stopListening();
    Serial.println("[nRF24] Ready (GTWY1=C/B relay, GTWY2=D direct)");
}

// ── LoRa packet handler ──────────────────────────────────────────
void parseLoRaPacket() {
    String msg = "";
    while (LoRa.available()) {
        char c = (char)LoRa.read();
        if (c >= 32 && c <= 126) msg += c;
    }
    int rssi = LoRa.packetRssi();
    Serial.printf("[LoRa] %s  RSSI:%d\n", msg.c_str(), rssi);

    // LoRa = always Node A (id=1), regardless of message string
    const char *p = strstr(msg.c_str(), "LDR:");
    if (p) {
        onNodeData(1, (uint16_t)atoi(p + 4));
        // Node A is in its 2s RX window — send if desired state differs from confirmed state.
        // LoRa has no ACK so treat as delivered; mismatch check provides automatic retry each cycle.
        if (nodes[1].ledOn != ledTarget[1]) {
            sendLoRaLedCmd(ledTarget[1]);
            nodes[1].ledOn = ledTarget[1];
        }
    }
}

// ── Web server ───────────────────────────────────────────────────
void initWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    // JSON data for all 4 nodes
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *req) {
        StaticJsonDocument<512> doc;
        JsonArray arr = doc.to<JsonArray>();
        const char *names[] = {"", "A - Mech Workshop", "B - PNRB", "C - Eng Block", "D - Fab Lab"};
        for (int i = 1; i <= 4; i++) {
            JsonObject o = arr.createNestedObject();
            o["node_id"] = i;
            o["name"]    = names[i];
            o["online"]  = nodes[i].online;
            o["light"]   = nodes[i].light;
            o["led"]     = ledTarget[i];
        }
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // LED control endpoint: POST /control  body: node=1&cmd=ON
    server.on("/control", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("node", true) || !req->hasParam("cmd", true)) {
            req->send(400, "text/plain", "Bad request");
            return;
        }
        int  nodeId = req->getParam("node", true)->value().toInt();
        bool on     = req->getParam("cmd",  true)->value() == "ON";

        if (nodeId >= 1 && nodeId <= 4) {
            ledPending[nodeId] = true;
            ledTarget[nodeId]  = on;
            Serial.printf("[Web] LED cmd: Node %d → %s\n", nodeId, on ? "ON" : "OFF");
        }
        req->send(200, "text/plain", "OK");
    });

    server.begin();
    Serial.print("[Web] Serving at http://");
    Serial.println(WiFi.localIP());
}
