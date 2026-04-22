#pragma once

// ── Node identity ────────────────────────────────────────────────
// Set NODE_ID uniquely per physical node before flashing.
//   1 → Fab Lab        (nRF24, direct to gateway)
//   2 → MPR            (nRF24, direct to gateway)
//   3 → Eng Block      (nRF24, direct to gateway + RELAY for node 5)
//   4 → Mech Workshop  (LoRa  — use lora-node firmware)
//   5 → PNRB           (nRF24, hops via node 3)
#define NODE_ID     1

// ── Relay configuration ──────────────────────────────────────────
// Uncomment IS_RELAY_NODE only when flashing Node 3 (Eng Block).
// Node 3 will also forward packets for RELAY_SRC_NODE_ID to the gateway.
// #define IS_RELAY_NODE
#define RELAY_SRC_NODE_ID   5       // which node this relay serves

// Node 5: set TX destination to relay (node 3) not gateway directly.
// Uncomment VIA_RELAY when flashing Node 5.
// #define VIA_RELAY

// ── Sensor pins ──────────────────────────────────────────────────
#define DHT_PIN     7
#define DHT_TYPE    DHT22
#define LDR_PIN     A0
#define LED_PIN     4

// ── nRF24L01 pins ────────────────────────────────────────────────
#define RF24_CE     9
#define RF24_CSN    10

// ── Radio config ─────────────────────────────────────────────────
#define RADIO_CHANNEL       76     // 2476 MHz — outside WiFi bands
#define RADIO_DATA_RATE     RF24_250KBPS
#define RADIO_PA_LEVEL      RF24_PA_HIGH   // HIGH for field; LOW for bench
#define RADIO_RETRY_DELAY   5      // 5 * 250us = 1.25ms
#define RADIO_RETRY_COUNT   15

// ── nRF24 pipe addresses ─────────────────────────────────────────
// Node TX to gateway:       ADDR_BASE + node_id
// Gateway CMD to node:      ADDR_CMD_BASE + node_id
// Node 5 TX to relay:       ADDR_RELAY_BASE + node_id
// Gateway relay-CMD to N3:  ADDR_RELAY_CMD_BASE (single address)
//
// All addresses share upper 4 bytes (0xF0F0F0F0) — required by nRF24
// for pipes 2-5 to work correctly alongside pipe 1.
#define ADDR_BASE           0xF0F0F0F0A0LL
#define ADDR_CMD_BASE       0xF0F0F0F000LL
#define ADDR_RELAY_BASE     0xF0F0F0F0C0LL  // node 5 → relay (node 3)
#define ADDR_RELAY_CMD_BASE 0xF0F0F0F0B0LL  // gateway → relay cmd pipe

// ── Timing ───────────────────────────────────────────────────────
#define SEND_INTERVAL_MS    30000UL   // 30 seconds between readings
#define LISTEN_WINDOW_MS    500UL     // time to listen for CMD after TX
#define RELAY_POLL_MS       600UL     // relay listen window for node 5 packets
