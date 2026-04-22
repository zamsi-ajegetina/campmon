# Campus IoT Network Diagram

## Overview

5 field nodes across the Ashesi campus transmit temperature, humidity, and
light data to a central ESP32 gateway.

**Radio allocation (hardware available: 5× nRF24L01, 1× LoRa):**
- Nodes 1, 2, 3: nRF24L01, direct link to gateway
- Node 4 (Mech Workshop, ~400m): LoRa — longest distance, only LoRa module
- Node 5 (PNRB, ~300m): nRF24L01, **multi-hop via Node 3** (Eng Block)
  - Node 3 acts as a transparent relay: forwards Node 5 packets to gateway
    and relays LED commands from gateway back to Node 5

---

## Mermaid Network Diagram

```mermaid
graph TB
    subgraph FIELD_NEAR["Near Field Nodes (nRF24L01, direct)"]
        N1["Node 1 — Fab Lab<br/>nRF24L01 | ~50m<br/>DHT22 + LDR + LED"]
        N2["Node 2 — MPR<br/>nRF24L01 | ~100m<br/>DHT22 + LDR + LED"]
        N3["Node 3 — Eng Block<br/>nRF24L01 | ~150m<br/>DHT22 + LDR + LED<br/><b>+ RELAY for Node 5</b>"]
    end

    subgraph FIELD_FAR["Far Field Nodes (>200m)"]
        N4["Node 4 — Mech Workshop<br/>LoRa SX1278 | ~400m<br/>DHT22 + LDR + LED"]
        N5["Node 5 — PNRB<br/>nRF24L01 | ~300m<br/>DHT22 + LDR + LED<br/>(multi-hop via Node 3)"]
    end

    subgraph GATEWAY["Gateway (ESP32 — central location)"]
        GW["ESP32 Gateway<br/>────────────────────<br/>• nRF24L01 (VSPI): 4 pipes<br/>  nodes 1,2,3 direct + node 5 relayed<br/>• LoRa SX1278 (HSPI): node 4<br/>• WiFi client → MQTT<br/>• Web server (LED control UI)"]
    end

    subgraph BACKEND["Backend Server"]
        MQTT["Mosquitto<br/>MQTT Broker :1883"]
        PY["Python Subscriber<br/>paho-mqtt"]
        DB["SQLite DB"]
        GRAF["Grafana :3000"]
    end

    subgraph USER["User Access"]
        WEBUI["Web Browser<br/>ESP32 Web UI<br/>LED Control"]
        DASH["Web Browser<br/>Grafana Dashboard"]
    end

    %% Direct nRF24 links
    N1 -- "nRF24L01\n2.4GHz | ~50m" --> GW
    N2 -- "nRF24L01\n2.4GHz | ~100m" --> GW
    N3 -- "nRF24L01\n2.4GHz | ~150m" --> GW

    %% Multi-hop: Node 5 → Node 3 (relay) → Gateway
    N5 -- "nRF24L01\n~150m hop" --> N3
    N3 -. "relay fwd\n(node 5 pkt)" .-> GW

    %% LoRa direct
    N4 -- "LoRa 433MHz\n~400m" --> GW

    %% LED command downlinks
    GW -- "CMD (nRF24)" --> N1
    GW -- "CMD (nRF24)" --> N2
    GW -- "CMD (nRF24)" --> N3
    GW -- "CMD (LoRa)" --> N4
    GW -. "relay-cmd to N3\nN3 fwds to N5" .-> N5

    %% Backend pipeline
    GW -- "WiFi / MQTT\ncampus/sensors/#" --> MQTT
    MQTT --> PY
    PY --> DB
    DB --> GRAF

    %% User access
    WEBUI -- "HTTP POST /control" --> GW
    DASH -- "HTTP" --> GRAF

    classDef nodeNear fill:#4CAF50,color:#fff,stroke:#388E3C
    classDef nodeFar  fill:#FF5722,color:#fff,stroke:#BF360C
    classDef relay    fill:#8BC34A,color:#fff,stroke:#558B2F,stroke-width:3px
    classDef gateway  fill:#2196F3,color:#fff,stroke:#1565C0
    classDef backend  fill:#FF9800,color:#fff,stroke:#E65100
    classDef user     fill:#9C27B0,color:#fff,stroke:#6A1B9A

    class N1,N2 nodeNear
    class N3 relay
    class N4,N5 nodeFar
    class GW gateway
    class MQTT,PY,DB,GRAF backend
    class WEBUI,DASH user
```

---

## Multi-Hop Detail (Node 5 via Node 3)

```
Data path (up):
  Node 5 ──[nRF24, ~150m]──> Node 3 ──[nRF24, ~150m]──> Gateway
  (PNRB)                     (Eng Block, relay)            (ESP32)

Control path (down):
  Gateway ──[nRF24 relay-cmd]──> Node 3 ──[nRF24 fwd]──> Node 5
           ADDR_RELAY_CMD_BASE    (extracts dest+cmd)   ADDR_CMD_BASE+5
```

**Why Node 3 as relay:**
- Node 3 (Eng Block) is ~150m from gateway — solid direct nRF24 link
- PNRB is ~150m from Eng Block — another solid hop
- Total path ~300m in two hops vs. attempting 300m direct (unreliable indoors)
- Eng Block is likely mains-powered → relay stays awake (no deep sleep)

**Node 3 pipe map:**
| Pipe | Address | Purpose |
|------|---------|---------|
| TX | `ADDR_BASE + 3` | Own data → gateway |
| TX | `ADDR_BASE + 5` | Forwarded Node 5 data → gateway |
| TX | `ADDR_CMD_BASE + 5` | Forward LED cmd → Node 5 |
| RX 1 | `ADDR_CMD_BASE + 3` | LED cmd for itself from gateway |
| RX 2 | `ADDR_RELAY_BASE + 5` | Node 5 sensor data (to relay) |
| RX 3 | `ADDR_RELAY_CMD_BASE` | Relay-cmd from gateway (for Node 5) |

---

## MQTT Topics

| Topic | Direction | Payload | Description |
|---|---|---|---|
| `campus/sensors/{id}/temperature` | Node → Broker | float °C | Temperature |
| `campus/sensors/{id}/humidity` | Node → Broker | float % | Humidity |
| `campus/sensors/{id}/light` | Node → Broker | int 0-1023 | Light level |
| `campus/status/{id}` | Node → Broker | online/offline | Heartbeat |
| `campus/control/{id}/led` | Broker → Gateway → Node | ON/OFF | LED toggle |

---

## Node Summary

| Node | Location | Radio | Link to Gateway | Distance | Power |
|---|---|---|---|---|---|
| 1 | Fab Lab | nRF24L01 | Direct | ~50m | USB/mains |
| 2 | MPR | nRF24L01 | Direct | ~100m | Battery |
| 3 | Eng Block | nRF24L01 | Direct + **relay** | ~150m | Mains (relay needs awake) |
| 4 | Mech Workshop | LoRa SX1278 | Direct (LoRa) | ~400m | Battery |
| 5 | PNRB | nRF24L01 | **Via Node 3** | ~300m total | Battery |

---

## Firmware Flash Guide

| Node | Firmware folder | config.h setting to change |
|---|---|---|
| 1 | `firmware/field-node` | `NODE_ID 1` |
| 2 | `firmware/field-node` | `NODE_ID 2` |
| 3 | `firmware/field-node` | `NODE_ID 3` + uncomment `IS_RELAY_NODE` |
| 4 | `firmware/lora-node` | `NODE_ID 4` |
| 5 | `firmware/field-node` | `NODE_ID 5` + uncomment `VIA_RELAY` |
| GW | `firmware/gateway` | Set WiFi + MQTT broker IP |

---

## Data Packet Format (12 bytes)

```c
typedef struct {
    uint8_t  node_id;       // 1 byte
    float    temperature;   // 4 bytes — °C
    float    humidity;      // 4 bytes — %RH
    uint16_t light;         // 2 bytes — ADC 0-1023
    uint8_t  led_state;     // 1 byte
} SensorPacket;             // 12 bytes total

typedef struct {
    uint8_t dest_node_id;   // target node for the command
    uint8_t cmd;            // 1=ON, 0=OFF
} RelayCmd;                 // 2 bytes — used only on relay-cmd pipe
```
