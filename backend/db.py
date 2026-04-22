"""
Database query helpers — used by any scripts that need to read from the DB.
Also provides a simple CLI to inspect recent readings.

Usage:
    python db.py                         # show last 20 readings
    python db.py --node 1                # readings for node 1
    python db.py --node 2 --limit 50    # last 50 readings for node 2
"""

import argparse
import sqlite3
import os

DB_PATH = os.getenv("DB_PATH", "campus_iot.db")


def get_recent(conn: sqlite3.Connection, node_id=None, limit=20):
    if node_id:
        return conn.execute(
            """SELECT ts, node_id, measurement, value
               FROM sensor_readings
               WHERE node_id = ?
               ORDER BY id DESC LIMIT ?""",
            (node_id, limit),
        ).fetchall()
    return conn.execute(
        """SELECT ts, node_id, measurement, value
           FROM sensor_readings
           ORDER BY id DESC LIMIT ?""",
        (limit,),
    ).fetchall()


def get_status(conn: sqlite3.Connection):
    return conn.execute(
        "SELECT node_id, status, updated FROM node_status ORDER BY node_id"
    ).fetchall()


def main():
    parser = argparse.ArgumentParser(description="Query campus IoT database")
    parser.add_argument("--node",  type=int, help="Filter by node ID")
    parser.add_argument("--limit", type=int, default=20, help="Number of rows")
    args = parser.parse_args()

    conn = sqlite3.connect(DB_PATH)

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
