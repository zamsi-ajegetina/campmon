#pragma once

// ── WiFi credentials ─────────────────────────────────────────────
#define WIFI_SSID     "HUAWEI-B612-8D3"
#define WIFI_PASSWORD "homerouter123"

// ── MQTT broker ──────────────────────────────────────────────────
#define MQTT_BROKER   "192.168.8.101"   // IP of your Raspberry Pi / PC
#define MQTT_PORT     1883
#define MQTT_CLIENT   "esp32-gateway"

// ── nRF24L01 pins (ESP32 default SPI) ────────────────────────────
#define RF24_CE       14
#define RF24_CSN      15
// SPI: SCK=18, MISO=19, MOSI=23 (shared with LoRa)

// ── LoRa SX1278 pins (ESP32 default SPI) ─────────────────────────
#define LORA_SS       5
#define LORA_RST      4
#define LORA_DIO0     26
// SPI: SCK=18, MISO=19, MOSI=23 (shared with nRF24)

// ── LoRa config (must match team-lora-node-a) ───────────────────
#define LORA_FREQUENCY    433.5E6
#define LORA_SPREADING    7       // SF7 — matches team-lora-node-a
#define LORA_BANDWIDTH    125E3
#define LORA_CODING_RATE  5
#define LORA_SYNC_WORD    0xF3   // Sync word — must match all LoRa nodes

// ── nRF24 config ─────────────────────────────────────────────────
#define RADIO_CHANNEL   100       // channel used by all team nodes
#define ADDR_GTWY1      "GTWY1"  // Node C → gateway (relays B + own data)
#define ADDR_GTWY2      "ATWY1"  // Node D → gateway (direct)

// ── Nodes ────────────────────────────────────────────────────────
// Node A (id=1): LoRa, far,    sends direct to gateway
// Node B (id=2): nRF24, far,   multihop via Node C → "GTWY1"
// Node C (id=3): nRF24, relay, sends own data + forwards B → "GTWY1"
// Node D (id=4): nRF24, close, sends direct to gateway → "ATWY1"
#define NUM_NRF_NODES   2    // 2 RX pipes: GTWY1, GTWY2
#define NUM_LORA_NODES  1    // Node A only

// ── Web server port ──────────────────────────────────────────────
#define WEB_PORT        80
