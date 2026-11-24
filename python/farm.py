#!/usr/bin/env python3
"""Command line for what a source on the farm knows: the farm's management server, a robot. It
reads only.

    python farm.py                                   # which drivers are there?
    python farm.py t4c 192.168.1.10 --user erik      # all the source knows (asks the password)
    python farm.py t4c 192.168.1.10 --user erik --record answers.json
                                                     # also save what the server said (logins left out)
    python farm.py t4c 10.4.1.1:6969                 # no --user: the password is the licence key
    python farm.py astronaut 10.4.1.101 --follow     # which cow is at the robot, as it happens
"""
from __future__ import annotations

import argparse
import getpass
import json
import os
import time

from agrigate.session import UNREACHABLE, Session, SessionError, drivers


def show_milk(data: dict) -> None:
    for r in data["robots"]:
        state = r["state"] if r["state"] != "unknown" else f"unknown ({r['raw']})"
        print(f"{r['name'] or r['id']:16} {r['group']:12} {state:16} milkings: {r['milkings'] if r['milkings'] is not None else '-'}")
        for alarm in r["alarms"]:
            print(f"    {alarm['time']}  {alarm['severity']:8} {alarm['text'] or alarm['code']}")


def show_cow(data: dict) -> None:
    for c in data["animals"]:
        last = c["inseminations"][-1]["date"] if c["inseminations"] else "-"
        print(f"{c['number']:>6} {c['name']:18} {c['lifeNumber']:16} {c['production']:8} {c['reproduction']:12} "
              f"lact {c['lactation'] if c['lactation'] is not None else '-'}  dim {c['daysInMilk'] if c['daysInMilk'] is not None else '-'}  last ins. {last}")


def show_identity(data: dict) -> None:
    for p in data["places"]:
        a = p["animal"]
        print(f"{p['name'] or p['id']:16} {p['kind']:14} " + (f"{a['number']:>6} {a['name']}" if a else "-"))


def identified(_session, e: dict) -> None:
    print(f"{time.strftime('%H:%M:%S', time.localtime(e['time'] / 1000))}  {e['place']:16} {e['animal']['number']:>6} {e['animal']['name']}", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("driver", nargs="?")
    ap.add_argument("address", nargs="?", help="the source on the farm network: host, host:port or http://host:port")
    ap.add_argument("--user", default="")
    ap.add_argument("--record", metavar="FILE", help="save every request and answer")
    ap.add_argument("--follow", action="store_true", help="keep showing which animal is identified where (Ctrl+C stops)")
    a = ap.parse_args()
    if not a.address:
        for d in drivers():
            link = d["links"][0]
            print(f"{d['id']:12} {d['name']:12} {', '.join(d['contracts']):12} ({link['kind']}" + (f", port {link['port']})" if "port" in link else ")"))
        return 0
    asks = next((d["links"][0]["kind"] == "http" for d in drivers() if d["id"] == a.driver), False)
    password = (os.environ.get("FARM_PASSWORD") or getpass.getpass("Password: " if a.user else "Licence key: ")) if asks else ""
    recording = [] if a.record else None
    try:
        with Session(a.driver, a.address, a.user, password, recording=recording, on_event=identified if a.follow else None) as source:
            for contract in source.contracts:
                wait = time.time() + 5   # a source that tells by itself needs a moment to be heard
                while True:
                    try:
                        data = source.read(contract)
                        break
                    except SessionError as e:
                        if asks or e.result != UNREACHABLE or time.time() > wait:
                            raise
                        time.sleep(0.2)
                {"milk": show_milk, "cow": show_cow, "identity": show_identity}[contract](data)
                if source.notes:
                    print("left out: " + source.notes)
            while a.follow:
                time.sleep(1)
    except SessionError as e:
        print(f"failed: {e}")
        return 1
    except KeyboardInterrupt:
        pass
    finally:
        if a.record:
            with open(a.record, "w") as f:
                json.dump(recording, f, indent=1)
            print(f"saved {len(recording)} answers in {a.record}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
