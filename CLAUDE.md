# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Campus IoT Environmental Monitoring System — MSc CSIS IoT Project, Ashesi University.
4 ESP32 field nodes send LDR (light) readings over radio to an ESP32 gateway, which publishes via MQTT to a Python subscriber that writes to SQLite, visualized in Grafana.

## Backend commands

```bash
cd backend
pip install -r requirements.txt      # paho-mqtt only

# Start MQTT subscriber (writes to campus_iot.db)
MQTT_BROKER=localhost python mqtt_subscriber.py

# Inspect the database
python db.py                          # last 20 readings, all nodes
python db.py --node 1 --limit 50     # filter by node

# Override defaults via env vars
MQTT_BROKER=<ip> MQTT_PORT=1883 DB_PATH=/path/to/campus_iot.db python mqtt_subscriber.py
```

```bash
# Mosquitto broker
sudo systemctl start mosquitto

# Monitor all topics live
mosquitto_sub -h localhost -t "campus/#" -v
```

## Firmware structure

| Folder | Target | Role |
|--------|--------|------|
| `firmware/gateway/` | ESP32 Dev Module | Gateway (deployed) |
| `team-lora-node-a/` | Arduino Nano (ATmega328P) | Node A — LoRa, far |
| `team-node-b/` | ESP32 Dev Module | Node B — nRF24, far, multi-hop via C |
| `team-node-c-multihop/` | ESP32 Dev Module | Node C — nRF24, relay |
| `team-node-c-direct-nrf/` | ESP32 Dev Module | Node D — nRF24, close, direct |
| `team-gateway/` | ESP32 Dev Module | Team test gateway (reference only) |

Gateway credentials live in `firmware/gateway/config.h` (`WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_BROKER`).

## Key architectural decisions

**Node C (Eng Block) is a dual-role relay.** It sends its own LDR data to the gateway via `"GTWY1"` *and* forwards packets received from Node B (`"NODEC"` pipe). It must never sleep — power from mains.

**nRF24 address scheme** — ASCII string addresses, no hex pipe scheme:
- `"NODEC"` — Node B TX → Node C RX (relay uplink)
- `"GTWY1"` — Node C TX → gateway pipe 1 (Node C own data + Node B forwarded)
- `"GTWY2"` — Node D TX → gateway pipe 2 (direct)

**Message format is a plain 32-byte ASCII string:** `"NodeX LDR:YYYY"` where X is A/B/C/D and YYYY is the ADC reading. Gateway parses with `strncmp` + `strstr("LDR:")` + `atoi`.

**Node IDs for MQTT:** A=1, B=2, C=3, D=4. MQTT topics: `campus/sensors/{1–4}/light` and `campus/status/{1–4}`.

**No temperature, humidity, or LED control** — team nodes only carry an LDR. The gateway web UI and MQTT output light values only.

**ESP32 gateway runs dual SPI buses concurrently** — nRF24L01 on VSPI (pins 18/19/23, CE=22, CSN=21) and LoRa SX1278 on HSPI (pins 14/12/13, SS=15, RST=27, DIO0=26).

**Radio settings (must match across all nodes):**
- nRF24: channel 100, 250 kbps, PA_LOW
- LoRa: 433 MHz, SF7, BW=125 kHz, CR=4/5, library-default sync word

**SQLite DB schema** — two tables: `sensor_readings (id, ts, node_id, measurement, value)` and `node_status (node_id, status, updated)`. The subscriber uses WAL mode so Grafana can read while the writer is active.

## Simulation

`simulation/` contains the original design (ATmega328P + DHT22 + struct-based protocol) — kept for reference but not compatible with the deployed team nodes.

## Grafana setup

```bash
sudo systemctl start grafana-server          # default :3000, admin/admin
grafana-cli plugins install frser-sqlite-datasource
sudo systemctl restart grafana-server
```

Add datasource: SQLite → absolute path to `campus_iot.db`. Import `dashboard/grafana/dashboard.json`.
