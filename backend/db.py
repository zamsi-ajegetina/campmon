"""
Database query helpers — reads from PostgreSQL.
Also provides a simple CLI to inspect recent readings.

Usage:
    python db.py                         # show last 20 readings
    python db.py --node 1                # readings for node 1
    python db.py --node 2 --limit 50    # last 50 readings for node 2

Environment variables (optional):
    PG_HOST / PG_PORT / PG_DB / PG_USER / PG_PASS
"""

import argparse
import os

import psycopg2

PG_HOST = os.getenv("PG_HOST", "")   # empty = Unix socket (peer auth, no password)
PG_PORT = int(os.getenv("PG_PORT",   "5432"))
PG_DB   = os.getenv("PG_DB",   "campus_iot")
PG_USER = os.getenv("PG_USER", os.getenv("USER", "postgres"))
PG_PASS = os.getenv("PG_PASS", "")


def connect() -> psycopg2.extensions.connection:
    kwargs = dict(dbname=PG_DB, user=PG_USER)
    if PG_HOST:
        kwargs.update(host=PG_HOST, port=PG_PORT, password=PG_PASS)
    return psycopg2.connect(**kwargs)


def get_recent(conn: psycopg2.extensions.connection, node_id=None, limit=20):
    with conn.cursor() as cur:
        if node_id:
            cur.execute(
                """SELECT ts, node_id, measurement, value
                   FROM sensor_readings
                   WHERE node_id = %s
                   ORDER BY id DESC LIMIT %s""",
                (node_id, limit),
            )
        else:
            cur.execute(
                """SELECT ts, node_id, measurement, value
                   FROM sensor_readings
                   ORDER BY id DESC LIMIT %s""",
                (limit,),
            )
        return cur.fetchall()


def get_status(conn: psycopg2.extensions.connection):
    with conn.cursor() as cur:
        cur.execute(
            "SELECT node_id, status, updated FROM node_status ORDER BY node_id"
        )
        return cur.fetchall()


def main():
    parser = argparse.ArgumentParser(description="Query campus IoT database")
    parser.add_argument("--node",  type=int, help="Filter by node ID")
    parser.add_argument("--limit", type=int, default=20, help="Number of rows")
    args = parser.parse_args()

    conn = connect()

    print("\n=== Node Status ===")
    for row in get_status(conn):
        print(f"  Node {row[0]:>2} | {row[1]:<8} | last seen {row[2]}")

    print(f"\n=== Last {args.limit} Readings ===")
    rows = get_recent(conn, node_id=args.node, limit=args.limit)
    for r in rows:
        print(f"  {r[0]}  Node {r[1]}  {r[2]:<12}  {r[3]:.2f}")

    conn.close()


if __name__ == "__main__":
    main()
