# Campus IoT — Environmental Monitoring System
**MSc CSIS IoT Project I | Ashesi University**

Monitors temperature, humidity, and lighting across 5 campus locations.
Data flows from ATmega328P field nodes → ESP32 gateway → MQTT → SQLite → Grafana dashboard.
The gateway also serves a web UI to remotely toggle LEDs on any node.

---

## Architecture

```
[Node 1-3]  nRF24L01 ──┐
                        ├──> [ESP32 Gateway] ──WiFi──> [MQTT Broker]
[Node 4-5]  LoRa    ───┘         │                          │
                              [Web UI]               [Python Subscriber]
                           (LED control)                     │
                                                        [SQLite DB]
                                                             │
                                                       [Grafana :3000]
```

| Node | Location | Radio | Distance |
|------|----------|-------|----------|
| 1 | Fab Lab | nRF24L01 | ~50m |
| 2 | MPR | nRF24L01 | ~100m |
| 3 | Engineering Block | nRF24L01 | ~150m |
| 4 | Mech Workshop | LoRa SX1278 | ~400m |
| 5 | PNRB | LoRa SX1278 | ~300m |

---

## Project Structure

```
csis-iot/
  docs/
    network-diagram.md       ← Mermaid source (render with mermaid-cli)
  simulation/
    field-node/              ← Wokwi: Arduino + nRF24 + DHT22 + LDR
    gateway/                 ← Wokwi: ESP32 + nRF24 receiver
  firmware/
    field-node/              ← ATmega328P + nRF24L01 (nodes 1-3)
    lora-node/               ← ATmega328P + LoRa SX1278 (nodes 4-5)
    gateway/                 ← ESP32 dual-radio + MQTT + web server
  backend/
    mqtt_subscriber.py       ← MQTT → SQLite writer
    db.py                    ← DB query helpers + CLI
    requirements.txt
  dashboard/
    grafana/dashboard.json   ← Import into Grafana
```

---

## Step-by-Step Setup

### 1. Simulate (no hardware needed)

Open two browser tabs at [wokwi.com](https://wokwi.com):
- **Tab 1 (Field Node):** paste `simulation/field-node/diagram.json` + `sketch.ino`
- **Tab 2 (Gateway):**   paste `simulation/gateway/diagram.json`  + `sketch.ino`

In the gateway Serial Monitor type `1:ON` or `1:OFF` to test LED control.

### 2. Flash field nodes (near, nRF24L01)

1. Open `firmware/field-node/field_node.ino` in Arduino IDE
2. Edit `config.h` → set `NODE_ID` (1, 2, or 3)
3. Set `RADIO_PA_LEVEL RF24_PA_HIGH` for field use
4. Board: **Arduino Nano (ATmega328P)**
5. Libraries: RF24, DHT sensor library, Adafruit Unified Sensor
6. Flash, repeat for each node with different `NODE_ID`

### 3. Flash LoRa nodes (far, >200m)

1. Open `firmware/lora-node/lora_node.ino`
2. Edit `config.h` → set `NODE_ID` (4 or 5)
3. Board: **Arduino Nano (ATmega328P)**
4. Libraries: arduino-LoRa, DHT sensor library
5. Flash

### 4. Flash gateway

1. Open `firmware/gateway/gateway.ino`
2. Edit `firmware/gateway/config.h`:
   - `WIFI_SSID`, `WIFI_PASSWORD`
   - `MQTT_BROKER` — IP of the machine running Mosquitto
3. Board: **ESP32 Dev Module**
4. Libraries: RF24, arduino-LoRa, PubSubClient, ESPAsyncWebServer, AsyncTCP, ArduinoJson
5. Flash
6. Open Serial Monitor — note the IP address printed
7. Visit `http://<gateway-ip>/` in a browser

### 5. Start the backend

```bash
# Install Mosquitto (broker)
sudo apt install mosquitto mosquitto-clients -y
sudo systemctl start mosquitto

# Python subscriber
cd backend
pip install -r requirements.txt
python mqtt_subscriber.py

# Verify data is flowing
python db.py
```

### 6. Grafana Dashboard

```bash
# Install Grafana
sudo apt install grafana -y
sudo systemctl start grafana-server

# Install SQLite datasource plugin
grafana-cli plugins install frser-sqlite-datasource
sudo systemctl restart grafana-server
```

1. Open `http://localhost:3000` (admin/admin)
2. Add datasource → SQLite → path: `/absolute/path/to/campus_iot.db`
3. Dashboards → Import → upload `dashboard/grafana/dashboard.json`

---

## MQTT Topics

| Topic | Payload | Description |
|-------|---------|-------------|
| `campus/sensors/{id}/temperature` | float °C | Temperature |
| `campus/sensors/{id}/humidity` | float % | Humidity |
| `campus/sensors/{id}/light` | int 0-1023 | Light level |
| `campus/status/{id}` | online/offline | Node heartbeat |
| `campus/control/{id}/led` | ON / OFF | LED command |

Monitor live:
```bash
mosquitto_sub -h localhost -t "campus/#" -v
```

Send a test LED command:
```bash
mosquitto_pub -h localhost -t "campus/control/1/led" -m "ON"
```

---

## Testing Checklist

- [ ] Wokwi simulation: node TX → gateway RX visible in Serial
- [ ] Wokwi simulation: LED command `1:ON` reflects back
- [ ] Bench test: node 1 nRF24 packet received by gateway
- [ ] Bench test: gateway publishes to MQTT (check `mosquitto_sub`)
- [ ] Bench test: SQLite rows appear after MQTT publish
- [ ] Field test: node 4 (Mech Workshop, ~400m) packets received
- [ ] Field test: node 5 (PNRB, ~300m) packets received
- [ ] Web UI: LED toggle button changes LED on physical node
- [ ] Grafana: dashboard updates live (30s refresh)

---

## Libraries (Arduino IDE)

Install via **Sketch → Include Library → Manage Libraries**:

| Library | Author | Use |
|---------|--------|-----|
| RF24 | TMRh20 | nRF24L01 |
| arduino-LoRa | sandeepmistry | LoRa SX1278 |
| DHT sensor library | Adafruit | DHT22 |
| Adafruit Unified Sensor | Adafruit | required by DHT |
| PubSubClient | Nick O'Leary | MQTT client |
| ESPAsyncWebServer | me-no-dev | Async web server |
| AsyncTCP | me-no-dev | required by ESPAsyncWebServer |
| ArduinoJson | Benoit Blanchon | JSON for /data endpoint |
# campmon
