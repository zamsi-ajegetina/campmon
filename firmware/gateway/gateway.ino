/**
 * Gateway Firmware
 * Target: ESP32 DevKit V1
 *
 * Features:
 *  - Receives sensor packets from 3 nRF24L01 near-nodes (pipes 1-3)
 *  - Receives sensor packets from 2 LoRa far-nodes (node 4 & 5)
 *  - Publishes all readings to MQTT broker over WiFi
 *  - Serves a web UI at http://<gateway-ip>/ for LED control
 *  - Forwards LED commands back to nodes via the respective radio
 *
 * Libraries:
 *  RF24, arduino-LoRa, PubSubClient, ESPAsyncWebServer, AsyncTCP,
 *  ArduinoJson
 *
 * Hardware wiring:
 *  nRF24L01 → VSPI (SCK=18, MISO=19, MOSI=23, CSN=21, CE=22)
 *  LoRa SX1278 → HSPI (SCK=14, MISO=12, MOSI=13, SS=15, RST=27, DIO0=26)
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

// ── Packet struct (must match field nodes) ───────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    float    temperature;
    float    humidity;
    uint16_t light;
    uint8_t  led_state;
} SensorPacket;

// ── Last known readings per node ─────────────────────────────────
struct NodeState {
    float    temperature = NAN;
    float    humidity    = NAN;
    uint16_t light       = 0;
    bool     online      = false;
    uint32_t lastSeen    = 0;
};
NodeState nodes[6];   // index 1-5

// ── Hardware objects ─────────────────────────────────────────────
SPIClass vspi(VSPI);
SPIClass hspi(HSPI);

RF24             radio(RF24_CE, RF24_CSN);
AsyncWebServer   server(WEB_PORT);
WiFiClient       wifiClient;
PubSubClient     mqtt(wifiClient);

// ── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    connectWiFi();
    initMQTT();
    initNRF24();
    initLoRa();
    initWebServer();

    Serial.println("[Gateway] All systems ready");
}

// ── Main loop ────────────────────────────────────────────────────
void loop() {
    // Keep MQTT alive
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();

    // Poll nRF24 radio
    uint8_t pipe;
    if (radio.available(&pipe)) {
        SensorPacket pkt;
        radio.read(&pkt, sizeof(pkt));
        // Accept nodes 1,2,3 (direct) and 5 (arrives via relay on node 3)
        if ((pkt.node_id >= 1 && pkt.node_id <= 3) || pkt.node_id == 5) {
            handlePacket(pkt, "nRF24");
        }
    }

    // Poll LoRa radio (non-blocking)
    int loraSize = LoRa.parsePacket();
    if (loraSize > 0) {
        parseLoRaPacket(loraSize);
    }

    // Mark nodes offline if no packet received in 2 minutes
    uint32_t now = millis();
    for (int i = 1; i <= 5; i++) {
        if (nodes[i].online && (now - nodes[i].lastSeen > 120000UL)) {
            nodes[i].online = false;
            publishStatus(i, false);
        }
    }
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
    mqtt.setCallback(mqttCallback);
    reconnectMQTT();
}

void reconnectMQTT() {
    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting...");
        if (mqtt.connect(MQTT_CLIENT)) {
            Serial.println("OK");
            // Subscribe to control topics so dashboard/scripts can send commands
            mqtt.subscribe("campus/control/#");
        } else {
            Serial.print("failed rc=");
            Serial.println(mqtt.state());
            delay(3000);
        }
    }
}

// MQTT → gateway: someone published a control command
void mqttCallback(char *topic, byte *payload, unsigned int length) {
    // Expected topic: campus/control/{node_id}/led
    // Payload: "ON" or "OFF"
    String t(topic);
    String p;
    for (unsigned int i = 0; i < length; i++) p += (char)payload[i];

    // Extract node ID from topic
    int lastSlash  = t.lastIndexOf('/');
    int thirdSlash = t.indexOf('/', 8);
    if (thirdSlash < 0 || lastSlash <= thirdSlash) return;
    int nodeId = t.substring(thirdSlash + 1, lastSlash).toInt();

    if (nodeId >= 1 && nodeId <= 5) {
        sendLedCommand(nodeId, p == "ON" ? 1 : 0);
    }
}

void publishSensor(const SensorPacket &pkt) {
    char topic[48], payload[16];

    snprintf(topic, sizeof(topic), "campus/sensors/%d/temperature", pkt.node_id);
    snprintf(payload, sizeof(payload), "%.1f", pkt.temperature);
    mqtt.publish(topic, payload, true);

    snprintf(topic, sizeof(topic), "campus/sensors/%d/humidity", pkt.node_id);
    snprintf(payload, sizeof(payload), "%.1f", pkt.humidity);
    mqtt.publish(topic, payload, true);

    snprintf(topic, sizeof(topic), "campus/sensors/%d/light", pkt.node_id);
    snprintf(payload, sizeof(payload), "%d", pkt.light);
    mqtt.publish(topic, payload, true);
}

void publishStatus(uint8_t nodeId, bool online) {
    char topic[40];
    snprintf(topic, sizeof(topic), "campus/status/%d", nodeId);
    mqtt.publish(topic, online ? "online" : "offline", true);
}

// ── nRF24L01 ─────────────────────────────────────────────────────
void initNRF24() {
    vspi.begin(18, 19, 23, RF24_CSN);
    if (!radio.begin(&vspi)) {
        Serial.println("[ERROR] nRF24 init failed");
        return;
    }
    radio.setPALevel(RF24_PA_HIGH);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(RADIO_CHANNEL);
    radio.setPayloadSize(sizeof(SensorPacket));

    // Open 4 reading pipes:
    //   pipe 1 → node 1 (Fab Lab,   direct)
    //   pipe 2 → node 2 (MPR,       direct)
    //   pipe 3 → node 3 (Eng Block, direct + relay for node 5)
    //   pipe 4 → node 5 (PNRB,     arrives via node 3 relay, uses node 5 addr)
    // Pipes 2-4 share the upper 4 address bytes with pipe 1 (nRF24 requirement).
    const uint8_t nrfNodeIds[4] = {1, 2, 3, 5};
    for (int i = 0; i < NUM_NRF_NODES; i++) {
        uint64_t addr = ADDR_BASE + nrfNodeIds[i];
        radio.openReadingPipe(i + 1, addr);
    }
    radio.startListening();
    Serial.println("[nRF24] Ready (pipes: nodes 1,2,3,5)");
}

// ── LoRa ─────────────────────────────────────────────────────────
void initLoRa() {
    hspi.begin(14, 12, 13, LORA_SS);
    LoRa.setSPI(hspi);
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQUENCY)) {
        Serial.println("[ERROR] LoRa init failed");
        return;
    }
    LoRa.setSpreadingFactor(LORA_SPREADING);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    Serial.println("[LoRa] Ready");
}

void parseLoRaPacket(int size) {
    if (size < (int)(2 + sizeof(SensorPacket))) return;

    uint8_t dest   = LoRa.read();
    uint8_t src    = LoRa.read();

    if (dest != GATEWAY_ADDR) return;   // not for us

    SensorPacket pkt;
    for (size_t i = 0; i < sizeof(pkt); i++) {
        ((uint8_t *)&pkt)[i] = LoRa.read();
    }

    if (pkt.node_id != src) return;     // sanity check
    handlePacket(pkt, "LoRa");
}

// ── Common packet handler ────────────────────────────────────────
void handlePacket(const SensorPacket &pkt, const char *radio) {
    uint8_t id = pkt.node_id;
    if (id < 1 || id > 5) return;

    nodes[id].temperature = pkt.temperature;
    nodes[id].humidity    = pkt.humidity;
    nodes[id].light       = pkt.light;
    nodes[id].lastSeen    = millis();

    bool wasOffline = !nodes[id].online;
    nodes[id].online = true;

    Serial.printf("[%s] Node %d T=%.1f H=%.1f L=%d\n",
                  radio, id, pkt.temperature, pkt.humidity, pkt.light);

    publishSensor(pkt);
    if (wasOffline) publishStatus(id, true);
}

// ── LED command sender ───────────────────────────────────────────
// Routing:
//   Nodes 1,2,3 → direct nRF24 CMD address
//   Node 4      → LoRa addressed packet
//   Node 5      → relay-cmd packet to Node 3, which forwards to Node 5
void sendLedCommand(uint8_t nodeId, uint8_t cmd) {
    radio.stopListening();

    if (nodeId == 4) {
        // LoRa: node 4 only
        LoRa.beginPacket();
        LoRa.write(nodeId);
        LoRa.write(GATEWAY_ADDR);
        LoRa.write(cmd);
        LoRa.endPacket();

    } else if (nodeId == 5) {
        // Node 5 is out of gateway range — send RelayCmd to Node 3.
        // Node 3 will retransmit the cmd to Node 5's CMD address.
        struct { uint8_t dest; uint8_t c; } rc = { nodeId, cmd };
        radio.openWritingPipe(ADDR_RELAY_CMD_BASE);
        radio.write(&rc, sizeof(rc));

    } else {
        // Nodes 1, 2, 3 — direct nRF24
        radio.openWritingPipe(ADDR_CMD_BASE + nodeId);
        radio.write(&cmd, 1);
    }

    radio.startListening();
    Serial.printf("[CMD] Node %d LED -> %s\n", nodeId, cmd ? "ON" : "OFF");
}

// ── Web server ───────────────────────────────────────────────────
void initWebServer() {
    // Serve dashboard page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    // JSON endpoint: last known state of all nodes
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *req) {
        StaticJsonDocument<512> doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 1; i <= 5; i++) {
            JsonObject o = arr.createNestedObject();
            o["node_id"]     = i;
            o["online"]      = nodes[i].online;
            o["temperature"] = isnan(nodes[i].temperature) ? (float)0 : nodes[i].temperature;
            o["humidity"]    = isnan(nodes[i].humidity)    ? (float)0 : nodes[i].humidity;
            o["light"]       = nodes[i].light;
        }
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /control — body: node=<id>&cmd=<ON|OFF>
    server.on("/control", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (req->hasParam("node", true) && req->hasParam("cmd", true)) {
            int    nodeId = req->getParam("node", true)->value().toInt();
            String action = req->getParam("cmd",  true)->value();
            if (nodeId >= 1 && nodeId <= 5) {
                uint8_t ledCmd = (action == "ON") ? 1 : 0;
                sendLedCommand(nodeId, ledCmd);
                // Also publish to MQTT so it's logged
                char topic[40];
                snprintf(topic, sizeof(topic), "campus/control/%d/led", nodeId);
                mqtt.publish(topic, action.c_str());
            }
        }
        req->send(200, "text/plain", "OK");
    });

    server.begin();
    Serial.print("[Web] Serving at http://");
    Serial.println(WiFi.localIP());
}
