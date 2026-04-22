#pragma once

// ── Node identity ────────────────────────────────────────────────
// Far nodes: 4 (Mech Workshop) or 5 (PNRB)
#define NODE_ID     4

// ── Sensor pins ──────────────────────────────────────────────────
#define DHT_PIN     7
#define DHT_TYPE    DHT22
#define LDR_PIN     A0
#define LED_PIN     4

// ── LoRa SX1278 pins (SPI) ───────────────────────────────────────
#define LORA_SS     10
#define LORA_RST    9
#define LORA_DIO0   2

// ── LoRa radio config ────────────────────────────────────────────
#define LORA_FREQUENCY      433E6    // 433 MHz
#define LORA_SPREADING      10       // SF10 — good range/speed balance
#define LORA_BANDWIDTH      125E3    // 125 kHz
#define LORA_CODING_RATE    5        // 4/5
#define LORA_TX_POWER       20       // dBm (max for SX1278)

// ── Packet sync word (network ID) ────────────────────────────────
#define LORA_SYNC_WORD      0xA5     // custom — filters other LoRa traffic

// ── Gateway LoRa address ─────────────────────────────────────────
#define GATEWAY_ADDR        0x00

// ── Timing ───────────────────────────────────────────────────────
#define SEND_INTERVAL_MS    30000UL
#define LISTEN_WINDOW_MS    2000UL   // longer window for LoRa latency
