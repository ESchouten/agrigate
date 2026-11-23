"""The farm service: live animal per robot and the herd over HTTP (docs/FARM_API.md).

    python farm_server.py farm_server.json
    python farm_server.py --robot "Robot 1=10.4.1.101" --robot "Robot 2=10.4.1.102" --t4c 10.4.1.1:6969

With --t4c the licence key comes from FARM_SERVER_PASSWORD (or "password" in the file), so that
it is not on the command line. Stop with Ctrl+C.
"""
import argparse
import json
import sys

from scraper_control.farm_server import serve


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("config", nargs="?", help="configuration file (JSON)")
    p.add_argument("--robot", action="append", default=[], metavar="NAME=ADDRESS", help="a Lely Astronaut (lely-lfw); repeat per robot")
    p.add_argument("--t4c", metavar="ADDRESS", help="the T4C server (licence key in FARM_SERVER_PASSWORD)")
    p.add_argument("--listen", default=None, help="address:port to serve on (default 0.0.0.0:8099)")
    a = p.parse_args()
    config = {}
    if a.config:
        with open(a.config, encoding="utf-8") as f:
            config = json.load(f)
    for r in a.robot:
        name, _, address = r.rpartition("=")
        config.setdefault("robots", []).append({"name": name or address, "driver": "lely-lfw", "address": address})
    if a.t4c:
        config["server"] = {"driver": "t4c", "address": a.t4c, "user": ""}
    if a.listen:
        config["listen"] = a.listen
    if not config.get("robots") and not config.get("server"):
        p.error("no robots and no server: give a configuration file or --robot / --t4c")
    httpd, service = serve(config)
    print(f"farm service on http://{config.get('listen', '0.0.0.0:8099')}  ({len(service.robots)} robots"
          + (", herd from " + config["server"]["address"] if config.get("server") else "") + ")", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        service.close()
        httpd.server_close()


if __name__ == "__main__":
    sys.exit(main())
