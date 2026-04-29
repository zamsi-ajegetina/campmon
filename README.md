# Campus IoT — Environmental Monitoring System
**MSc CSIS IoT Project I | Ashesi University**

Monitors lighting conditions across 4 campus locations.
Data flows from 4 ESP32 field nodes → ESP32 gateway → MQTT → SQLite → Grafana dashboard.

---

## Architecture

```
[Node A]  LoRa SX1278 ─────┐
[Node B]  nRF24 → Node C ──┤
[Node C]  nRF24 (relay) ───┼──> [ESP32 Gateway] ──WiFi──> [MQTT Broker]
[Node D]  nRF24 (direct) ──┘         │                          │
                                   [Web UI]               [Python Subscriber]
                                  (node status)                  │
                                                           [SQLite DB]
                                                                 │
                                                          [Grafana :3000]
```

| Node | Location | Radio | Link | Distance |
|------|----------|-------|------|----------|
| A | Mech Workshop | LoRa SX1278 | Direct via LoRa | ~400m |
| B | PNRB | nRF24L01 | **Multi-hop via Node C** | ~300m total |
| C | Eng Block | nRF24L01 | Direct + **relay for Node B** | ~150m |
| D | Fab Lab / MPR | nRF24L01 | Direct | ~50–100m |
| Gateway | Central | — | WiFi + MQTT | — |

Message format: `char[32]` string `"NodeX LDR:YYYY"` (all nodes).

---

## Project Structure

```
csis-iot/
  docs/
    network-diagram.md       ← Mermaid network diagram
  firmware/
    gateway/                 ← ESP32 dual-radio gateway (nRF24 + LoRa + MQTT + web UI)
      gateway.ino
      config.h               ← WiFi, MQTT broker IP, radio settings
      web_ui.h               ← Inline HTML for the web dashboard
  team-lora-node-a/          ← Node A firmware (Arduino, LoRa SX1278)
  team-node-b/               ← Node B firmware (ESP32, nRF24, sends to Node C)
  team-node-c-multihop/      ← Node C firmware (ESP32, nRF24, relay + own data)
  team-node-c-direct-nrf/    ← Node D firmware (ESP32, nRF24, direct to gateway)
  team-gateway/              ← Team test gateway (reference only, not deployed)
  backend/
    mqtt_subscriber.py       ← MQTT → SQLite writer
    db.py                    ← DB query helpers + CLI
    requirements.txt
  dashboard/
    grafana/dashboard.json   ← Import into Grafana
  simulation/                ← Wokwi browser simulation (original design, reference only)
```

---

## Step-by-Step Setup

### 1. Flash Node A (LoRa — Mech Workshop, ~400m)

1. Open `team-lora-node-a/team-lora-node-a.ino` in Arduino IDE
2. Board: **Arduino Nano (ATmega328P)**
3. Wiring: LoRa SX1278 — SS=8, RST=7, DIO0=2; LDR on A0
4. Libraries: arduino-LoRa
5. Flash — sends `"NodeA LDR:XXXX"` every 10 s at 433 MHz, SF7

### 2. Flash Node B (nRF24, far — PNRB, ~300m via relay)

1. Open `team-node-b/team-node-b.ino`
2. Board: **ESP32 Dev Module**
3. Wiring: nRF24L01 — CE=4, CSN=5; LDR on pin 34
4. Libraries: RF24
5. Flash — sends `"NodeB LDR:XXXX"` to Node C (`"NODEC"`) every 10 s

### 3. Flash Node C (nRF24, relay — Eng Block, ~150m)

1. Open `team-node-c-multihop/team-node-c-multihop.ino`
2. Board: **ESP32 Dev Module**
3. Wiring: same as Node B (CE=4, CSN=5; LDR on 34)
4. Libraries: RF24
5. Flash — listens on `"NODEC"` for Node B, forwards to gateway `"GTWY1"`, also sends own `"NodeC LDR:XXXX"` every 10 s

> Node C must stay powered (no sleep) — it acts as the relay for Node B.

### 4. Flash Node D (nRF24, direct — Fab Lab / MPR, ~50–100m)

1. Open `team-node-c-direct-nrf/team-node-c-direct-nrf.ino`
2. Board: **ESP32 Dev Module**
3. Wiring: same as Node B (CE=4, CSN=5; LDR on 34)
4. Libraries: RF24
5. Flash — sends `"NodeD LDR:XXXX"` to gateway `"GTWY2"` every 30 s

### 5. Flash the Gateway

1. Open `firmware/gateway/gateway.ino` in Arduino IDE
2. Edit `firmware/gateway/config.h`:
   - `WIFI_SSID`, `WIFI_PASSWORD`
   - `MQTT_BROKER` — IP of the machine running Mosquitto
3. Board: **ESP32 Dev Module**
4. Wiring:
   - nRF24L01 on VSPI: SCK=18, MISO=19, MOSI=23, CE=22, CSN=21
   - LoRa SX1278 on HSPI: SCK=14, MISO=12, MOSI=13, SS=15, RST=27, DIO0=26
5. Libraries: RF24, arduino-LoRa, PubSubClient, ESPAsyncWebServer, AsyncTCP, ArduinoJson
6. Flash — erasing flash first is recommended (`Tools → Erase Flash → All Flash Contents`)
7. Open Serial Monitor at **115200** baud — note IP printed after `[WiFi] IP:`
8. Visit `http://<gateway-ip>/` in a browser — shows live LDR values for all 4 nodes

### 6. Start the Backend

```bash
# Install Mosquitto
sudo apt install mosquitto mosquitto-clients -y
sudo systemctl start mosquitto

# Python subscriber
cd backend
pip install -r requirements.txt
python mqtt_subscriber.py

# Verify data is flowing
python db.py
```

### 7. Grafana Dashboard

```bash
sudo apt install grafana -y
sudo systemctl start grafana-server
grafana-cli plugins install frser-sqlite-datasource
sudo systemctl restart grafana-server
```

1. Open `http://localhost:3000` (admin/admin)
2. Add datasource → SQLite → absolute path to `backend/campus_iot.db`
3. Dashboards → Import → upload `dashboard/grafana/dashboard.json`

---

## MQTT Topics

| Topic | Payload | Description |
|-------|---------|-------------|
| `campus/sensors/1/light` | int 0–4095 | Node A light level |
| `campus/sensors/2/light` | int 0–4095 | Node B light level |
| `campus/sensors/3/light` | int 0–4095 | Node C light level |
| `campus/sensors/4/light` | int 0–4095 | Node D light level |
| `campus/status/{1–4}` | online / offline | Node heartbeat (2-min timeout) |

Monitor live:
```bash
mosquitto_sub -h localhost -t "campus/#" -v
```

---

## Testing Checklist

- [ ] Node A: Serial Monitor shows `Sent: NodeA LDR:XXXX` every 10 s
- [ ] Node B: Serial Monitor shows `Sent to Node C: NodeB LDR:XXXX`
- [ ] Node C: Serial Monitor shows `Forwarded to Gateway: NodeB LDR:XXXX` and own `NodeC LDR:XXXX`
- [ ] Node D: Serial Monitor shows `Sent to Gateway: NodeD LDR:XXXX`
- [ ] Gateway: Serial Monitor shows `[LoRa] NodeA LDR:XXXX` and `[nRF24 pipe1/2] ...`
- [ ] MQTT: `mosquitto_sub -h localhost -t "campus/#" -v` receives all 4 nodes
- [ ] SQLite: `python db.py` shows rows for nodes 1–4
- [ ] Web UI: `http://<gateway-ip>/` — all 4 node cards update live
- [ ] Grafana: dashboard refreshes with new readings

---

## Libraries (Arduino IDE)

Install via **Sketch → Include Library → Manage Libraries**:

| Library | Author | Use |
|---------|--------|-----|
| RF24 | TMRh20 | nRF24L01 (gateway + nodes B, C, D) |
| arduino-LoRa | sandeepmistry | LoRa SX1278 (gateway + node A) |
| PubSubClient | Nick O'Leary | MQTT client (gateway) |
| ESPAsyncWebServer | me-no-dev | Async web server (gateway) |
| AsyncTCP | me-no-dev | Required by ESPAsyncWebServer |
| ArduinoJson | Benoit Blanchon | JSON for /data endpoint (gateway) |
