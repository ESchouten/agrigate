#!/usr/bin/env python3
"""Command line for robots that are read through the farm's management server. It reads only.

    python farm.py                                   # which server software is known?
    python farm.py t4c 192.168.1.10 --user erik      # the milking robots (asks the password)
    python farm.py t4c 192.168.1.10 --user erik --record answers.json
                                                     # also save what the server said (logins left out)
    python farm.py t4c 10.4.1.1:6969 --cow           # the herd; no --user: the password is the licence key
"""
from __future__ import annotations

import argparse
import getpass
import json
import os

from agrigate.farm import Farm, FarmError, drivers


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("driver", nargs="?", help="the server software")
    ap.add_argument("address", nargs="?", help="the server on the farm network: host, host:port or http://host:port")
    ap.add_argument("--user", default="")
    ap.add_argument("--record", metavar="FILE", help="save every request and answer")
    ap.add_argument("--cow", action="store_true", help="also the herd (core/cow_c.h)")
    a = ap.parse_args()
    if not a.address:
        for d in drivers():
            print(f"{d['id']:12} {d['name']:12} {', '.join(d['kinds'])}   (port {d['link']['port']})")
        return 0
    password = os.environ.get("FARM_PASSWORD") or getpass.getpass("Password: " if a.user else "Licence key: ")
    recording = [] if a.record else None
    try:
        with Farm(a.driver, a.address, a.user, password, recording=recording) as farm:
            for r in farm.milk()["robots"]:
                state = r["state"] if r["state"] != "unknown" else f"unknown ({r['raw']})"
                print(f"{r['name'] or r['id']:16} {r['group']:12} {state:16} milkings: {r['milkings'] if r['milkings'] is not None else '-'}")
                for alarm in r["alarms"]:
                    print(f"    {alarm['time']}  {alarm['severity']:8} {alarm['text'] or alarm['code']}")
            if farm.notes:
                print("left out: " + farm.notes)
            if a.cow:
                for c in farm.cow()["animals"]:
                    last = c["inseminations"][-1]["date"] if c["inseminations"] else "-"
                    print(f"{c['number']:>6} {c['name']:18} {c['lifeNumber']:16} {c['production']:8} {c['reproduction']:12} "
                          f"lact {c['lactation'] if c['lactation'] is not None else '-'}  dim {c['daysInMilk'] if c['daysInMilk'] is not None else '-'}  last ins. {last}")
                if farm.notes:
                    print("left out: " + farm.notes)
    except FarmError as e:
        print(f"failed: {e}")
        return 1
    finally:
        if a.record:
            with open(a.record, "w") as f:
                json.dump(recording, f, indent=1)
            print(f"saved {len(recording)} answers in {a.record}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
