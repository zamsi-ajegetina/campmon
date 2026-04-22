"""
MQTT → Database Subscriber
---------------------------
Subscribes to all campus/# MQTT topics and writes readings to SQLite.
Run on the same machine as the Mosquitto broker (Raspberry Pi or PC).

Usage:
    pip install -r requirements.txt
    python mqtt_subscriber.py

Environment variables (optional, override defaults):
    MQTT_BROKER  — broker IP   (default: localhost)
    MQTT_PORT    — broker port (default: 1883)
    DB_PATH      — SQLite file (default: campus_iot.db)
"""

import os
import re
import sqlite3
import logging
from datetime import datetime

import paho.mqtt.client as mqtt

# ── Config ──────────────────────────────────────────────────────
BROKER   = os.getenv("MQTT_BROKER", "localhost")
PORT     = int(os.getenv("MQTT_PORT", "1883"))
DB_PATH  = os.getenv("DB_PATH", "campus_iot.db")
TOPICS   = [
    ("campus/sensors/#", 0),
    ("campus/status/#",  0),
    ("campus/control/#", 0),
]

# ── Logging ─────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger(__name__)

# ── Database ─────────────────────────────────────────────────────
def init_db(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path, check_same_thread=False)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS sensor_readings (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            ts          TEXT    NOT NULL,
            node_id     INTEGER NOT NULL,
            measurement TEXT    NOT NULL,
            value       REAL    NOT NULL
        )
    """)
    conn.execute("""
        CREATE INDEX IF NOT EXISTS idx_node_ts
        ON sensor_readings (node_id, ts)
    """)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS node_status (
            node_id   INTEGER PRIMARY KEY,
            status    TEXT    NOT NULL,
            updated   TEXT    NOT NULL
        )
    """)
    conn.commit()
    log.info("Database ready at %s", path)
    return conn


def insert_reading(conn: sqlite3.Connection, node_id: int,
                   measurement: str, value: float) -> None:
    ts = datetime.utcnow().isoformat(timespec="seconds")
    conn.execute(
        "INSERT INTO sensor_readings (ts, node_id, measurement, value) VALUES (?,?,?,?)",
        (ts, node_id, measurement, value),
    )
    conn.commit()


def upsert_status(conn: sqlite3.Connection, node_id: int, status: str) -> None:
    ts = datetime.utcnow().isoformat(timespec="seconds")
    conn.execute(
        """INSERT INTO node_status (node_id, status, updated) VALUES (?,?,?)
           ON CONFLICT(node_id) DO UPDATE SET status=excluded.status, updated=excluded.updated""",
        (node_id, status, ts),
    )
    conn.commit()

# ── MQTT handlers ────────────────────────────────────────────────
# Topic patterns:
#   campus/sensors/{node_id}/{measurement}   → float value
#   campus/status/{node_id}                  → "online" | "offline"
SENSOR_RE = re.compile(r"^campus/sensors/(\d+)/(\w+)$")
STATUS_RE  = re.compile(r"^campus/status/(\d+)$")


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        log.info("Connected to MQTT broker at %s:%d", BROKER, PORT)
        for topic, qos in TOPICS:
            client.subscribe(topic, qos)
            log.info("Subscribed: %s", topic)
    else:
        log.error("Connection failed, rc=%d", rc)


def on_message(client, userdata, msg):
    conn: sqlite3.Connection = userdata
    topic   = msg.topic
    payload = msg.payload.decode("utf-8", errors="ignore").strip()

    m = SENSOR_RE.match(topic)
    if m:
        node_id     = int(m.group(1))
        measurement = m.group(2)
        try:
            value = float(payload)
        except ValueError:
            log.warning("Non-numeric payload on %s: %r", topic, payload)
            return
        insert_reading(conn, node_id, measurement, value)
        log.info("Node %d | %-12s = %.2f", node_id, measurement, value)
        return

    m = STATUS_RE.match(topic)
    if m:
        node_id = int(m.group(1))
        upsert_status(conn, node_id, payload)
        log.info("Node %d | status = %s", node_id, payload)


def on_disconnect(client, userdata, rc):
    if rc != 0:
        log.warning("Unexpected disconnect (rc=%d), will auto-reconnect", rc)


# ── Entry point ──────────────────────────────────────────────────
def main():
    conn = init_db(DB_PATH)

    client = mqtt.Client(userdata=conn)
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect

    client.connect(BROKER, PORT, keepalive=60)
    log.info("Starting MQTT loop...")
    client.loop_forever()


if __name__ == "__main__":
    main()
