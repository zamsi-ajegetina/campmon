# Campus IoT Network Diagram

## Overview

5 field nodes (3 nRF24L01, 2 LoRa) across the Ashesi campus transmit
temperature, humidity, and light data to a central ESP32 gateway.
The gateway publishes via MQTT to a backend that stores and displays the data.
Bidirectional: the gateway can also push LED ON/OFF commands back to each node.

---

## Mermaid Network Diagram

```mermaid
graph TB
    subgraph FIELD_NODES["Field Nodes (ATmega328P)"]
        N1["Node 1 — Fab Lab<br/>nRF24L01 | ~50m<br/>DHT22 + LDR + LED"]
        N2["Node 2 — MPR<br/>nRF24L01 | ~100m<br/>DHT22 + LDR + LED"]
        N3["Node 3 — Eng Block<br/>nRF24L01 | ~150m<br/>DHT22 + LDR + LED"]
        N4["Node 4 — Mech Workshop<br/>LoRa SX1278 | ~400m<br/>DHT22 + LDR + LED"]
        N5["Node 5 — PNRB<br/>LoRa SX1278 | ~300m<br/>DHT22 + LDR + LED"]
    end

    subgraph GATEWAY["Gateway (ESP32 — central location)"]
        GW["ESP32 Gateway<br/>────────────────<br/>• nRF24L01 receiver<br/>• LoRa SX1278 receiver<br/>• WiFi client<br/>• Web server (192.168.x.x)<br/>• MQTT publisher"]
    end

    subgraph BACKEND["Backend Server (Raspberry Pi / PC)"]
        MQTT["Mosquitto<br/>MQTT Broker<br/>:1883"]
        PY["Python Subscriber<br/>paho-mqtt"]
        DB["InfluxDB<br/>(time-series DB)"]
        GRAF["Grafana Dashboard<br/>:3000"]
    end

    subgraph USER["User Access"]
        WEBUI["Web Browser<br/>ESP32 Web UI<br/>→ LED Control"]
        DASH["Web Browser<br/>Grafana Dashboard<br/>→ Live Charts"]
    end

    %% Sensor data uplinks
    N1 -- "nRF24L01\n2.4GHz" --> GW
    N2 -- "nRF24L01\n2.4GHz" --> GW
    N3 -- "nRF24L01\n2.4GHz" --> GW
    N4 -- "LoRa 433MHz\n~400m" --> GW
    N5 -- "LoRa 433MHz\n~300m" --> GW

    %% Control downlinks
    GW -- "LED cmd\n(nRF24/LoRa)" --> N1
    GW -- "LED cmd\n(nRF24/LoRa)" --> N2
    GW -- "LED cmd\n(nRF24/LoRa)" --> N3
    GW -- "LED cmd\n(nRF24/LoRa)" --> N4
    GW -- "LED cmd\n(nRF24/LoRa)" --> N5

    %% Gateway to backend
    GW -- "WiFi / MQTT\ncampus/sensors/#" --> MQTT

    %% Backend pipeline
    MQTT --> PY
    PY --> DB
    DB --> GRAF

    %% User access
    WEBUI -- "HTTP / WebSocket" --> GW
    DASH -- "HTTP" --> GRAF

    %% Styles
    classDef node fill:#4CAF50,color:#fff,stroke:#388E3C
    classDef gateway fill:#2196F3,color:#fff,stroke:#1565C0
    classDef backend fill:#FF9800,color:#fff,stroke:#E65100
    classDef user fill:#9C27B0,color:#fff,stroke:#6A1B9A

    class N1,N2,N3,N4,N5 node
    class GW gateway
    class MQTT,PY,DB,GRAF backend
    class WEBUI,DASH user
```

---

## MQTT Topic Structure

| Topic | Direction | Payload | Description |
|---|---|---|---|
| `campus/sensors/{id}/temperature` | Node → Broker | `float` °C | Temperature reading |
| `campus/sensors/{id}/humidity` | Node → Broker | `float` % | Humidity reading |
| `campus/sensors/{id}/light` | Node → Broker | `int` 0-1023 | LDR raw ADC value |
| `campus/sensors/{id}/rssi` | Node → Broker | `int` dBm | Radio signal strength |
| `campus/control/{id}/led` | Broker → Gateway → Node | `ON` / `OFF` | LED toggle command |
| `campus/status/{id}` | Node → Broker | `online`/`offline` | Node heartbeat |

---

## Node Locations & Distances

| Node | Location | Radio | Est. Distance to Gateway | Battery / Power |
|---|---|---|---|---|
| 1 | Fab Lab | nRF24L01 | ~50m | USB / mains |
| 2 | MPR | nRF24L01 | ~100m | Battery pack |
| 3 | Engineering Block | nRF24L01 | ~150m | USB / mains |
| 4 | Mechanical Workshop | LoRa SX1278 | ~400m | Battery pack |
| 5 | PNRB | LoRa SX1278 | ~300m | Battery pack |

**Gateway:** positioned centrally (e.g., main admin area or library rooftop)

---

## Radio Link Budget

### nRF24L01 (PA+LNA module)
- Frequency: 2.4 GHz
- Tx Power: +20 dBm (PA version)
- Sensitivity: -95 dBm
- Expected range: 100–1000m (open field), 50–200m (through walls)
- Data rate: 250 kbps (long range mode)

### LoRa SX1278
- Frequency: 433 MHz
- Spreading Factor: SF10–SF12 for >300m indoors
- Tx Power: +20 dBm
- Expected range: 2–5 km (open), 300–800m (urban/indoor)

---

## Data Packet Format (C struct, 12 bytes)

```c
typedef struct {
    uint8_t  node_id;       // 1 byte  — node identifier (1–5)
    float    temperature;   // 4 bytes — °C
    float    humidity;      // 4 bytes — %RH
    uint16_t light;         // 2 bytes — ADC 0–1023
    uint8_t  led_state;     // 1 byte  — current LED state
} SensorPacket;             // Total: 12 bytes
```
