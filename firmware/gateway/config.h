#pragma once

// ── WiFi credentials ─────────────────────────────────────────────
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// ── MQTT broker ──────────────────────────────────────────────────
#define MQTT_BROKER   "192.168.1.100"   // IP of your Raspberry Pi / PC
#define MQTT_PORT     1883
#define MQTT_CLIENT   "esp32-gateway"

// ── nRF24L01 pins (ESP32 VSPI) ───────────────────────────────────
#define RF24_CE       22
#define RF24_CSN      21
// VSPI: SCK=18, MISO=19, MOSI=23

// ── LoRa SX1278 pins (ESP32 HSPI) ────────────────────────────────
#define LORA_SS       15
#define LORA_RST      27
#define LORA_DIO0     26
// HSPI: SCK=14, MISO=12, MOSI=13

// ── LoRa config (must match lora-node) ──────────────────────────
#define LORA_FREQUENCY    433E6
#define LORA_SPREADING    10
#define LORA_BANDWIDTH    125E3
#define LORA_CODING_RATE  5
#define LORA_SYNC_WORD    0xA5
#define GATEWAY_ADDR      0x00

// ── nRF24 config ─────────────────────────────────────────────────
#define RADIO_CHANNEL     76
#define ADDR_BASE           0xF0F0F0F0A0LL  // node TX → gateway RX
#define ADDR_CMD_BASE       0xF0F0F0F000LL  // gateway TX → node RX
#define ADDR_RELAY_CMD_BASE 0xF0F0F0F0B0LL  // gateway → relay-cmd pipe (node 3)

// ── Nodes ────────────────────────────────────────────────────────
// Direct nRF24 nodes: 1 (Fab Lab), 2 (MPR), 3 (Eng Block/relay),
//                     5 (PNRB, arrives via node 3 relay)
// LoRa node: 4 (Mech Workshop — longest distance, only LoRa module)
// Node 5 CMD path: gateway → relay-cmd pipe on node 3 → node 3 fwds to node 5
#define NUM_NRF_NODES   4    // pipes: nodes 1, 2, 3 direct + node 5 via relay
#define NUM_LORA_NODES  1    // node 4 only
#define RELAY_NODE_ID   3    // node that relays for node 5

// ── Web server port ──────────────────────────────────────────────
#define WEB_PORT        80
