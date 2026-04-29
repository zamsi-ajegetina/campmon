"""
MQTT → Database Subscriber
---------------------------
Subscribes to all campus/# MQTT topics and writes readings to PostgreSQL.
Run on the same machine as the Mosquitto broker.

Usage:
    pip install -r requirements.txt
    python mqtt_subscriber.py

Environment variables (optional, override defaults):
    MQTT_BROKER  — broker IP        (default: localhost)
    MQTT_PORT    — broker port      (default: 1883)
    PG_HOST      — postgres host    (default: localhost)
    PG_PORT      — postgres port    (default: 5432)
    PG_DB        — database name    (default: campus_iot)
    PG_USER      — postgres user    (default: current OS user)
    PG_PASS      — postgres password (default: empty)
"""

import os
import re
import logging
import threading
from datetime import datetime, timezone

import paho.mqtt.client as mqtt
import psycopg2

# ── Config ──────────────────────────────────────────────────────
BROKER  = os.getenv("MQTT_BROKER", "localhost")
PORT    = int(os.getenv("MQTT_PORT",  "1883"))
PG_HOST = os.getenv("PG_HOST", "")   # empty = Unix socket (peer auth, no password)
PG_PORT = int(os.getenv("PG_PORT",   "5432"))
PG_DB   = os.getenv("PG_DB",   "campus_iot")
PG_USER = os.getenv("PG_USER", os.getenv("USER", "postgres"))
PG_PASS = os.getenv("PG_PASS", "")

TOPICS = [
    ("campus/sensors/#", 0),
    ("campus/status/#",  0),
]

# ── Logging ─────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger(__name__)

# ── Database ─────────────────────────────────────────────────────
def connect_db() -> psycopg2.extensions.connection:
    kwargs = dict(dbname=PG_DB, user=PG_USER)
    if PG_HOST:
        kwargs.update(host=PG_HOST, port=PG_PORT, password=PG_PASS)
    return psycopg2.connect(**kwargs)


def init_db(conn: psycopg2.extensions.connection) -> None:
    with conn.cursor() as cur:
        cur.execute("""
            CREATE TABLE IF NOT EXISTS sensor_readings (
                id          BIGSERIAL PRIMARY KEY,
                ts          TIMESTAMP NOT NULL,
                node_id     INTEGER   NOT NULL,
                measurement TEXT      NOT NULL,
                value       REAL      NOT NULL
            )
        """)
        cur.execute("""
            CREATE INDEX IF NOT EXISTS idx_node_ts
            ON sensor_readings (node_id, ts)
        """)
        cur.execute("""
            CREATE TABLE IF NOT EXISTS node_status (
                node_id INTEGER   PRIMARY KEY,
                status  TEXT      NOT NULL,
                updated TIMESTAMP NOT NULL
            )
        """)
    conn.commit()
    log.info("Database ready (PostgreSQL %s:%d/%s)", PG_HOST, PG_PORT, PG_DB)


def insert_reading(conn: psycopg2.extensions.connection, lock: threading.Lock,
                   node_id: int, measurement: str, value: float) -> None:
    with lock:
        with conn.cursor() as cur:
            cur.execute(
                "INSERT INTO sensor_readings (ts, node_id, measurement, value) VALUES (%s,%s,%s,%s)",
                (datetime.now(timezone.utc), node_id, measurement, value),
            )
        conn.commit()


def upsert_status(conn: psycopg2.extensions.connection, lock: threading.Lock,
                  node_id: int, status: str) -> None:
    with lock:
        with conn.cursor() as cur:
            cur.execute(
                """INSERT INTO node_status (node_id, status, updated) VALUES (%s,%s,%s)
                   ON CONFLICT (node_id) DO UPDATE
                   SET status = EXCLUDED.status, updated = EXCLUDED.updated""",
                (node_id, status, datetime.now(timezone.utc)),
            )
        conn.commit()

# ── MQTT handlers ────────────────────────────────────────────────
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
    conn, lock = userdata
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
        insert_reading(conn, lock, node_id, measurement, value)
        log.info("Node %d | %-12s = %.2f", node_id, measurement, value)
        return

    m = STATUS_RE.match(topic)
    if m:
        node_id = int(m.group(1))
        upsert_status(conn, lock, node_id, payload)
        log.info("Node %d | status = %s", node_id, payload)


def on_disconnect(client, userdata, rc):
    if rc != 0:
        log.warning("Unexpected disconnect (rc=%d), will auto-reconnect", rc)


# ── Entry point ──────────────────────────────────────────────────
def main():
    conn = connect_db()
    init_db(conn)
    lock = threading.Lock()

    client = mqtt.Client(userdata=(conn, lock))
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect

    client.connect(BROKER, PORT, keepalive=60)
    log.info("Starting MQTT loop...")
    client.loop_forever()


if __name__ == "__main__":
    main()
