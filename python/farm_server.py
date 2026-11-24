"""The farm service: what the farm's sources know, over HTTP (docs/farm/API.md).

    python farm_server.py farm_server.json

Stop with Ctrl+C.
"""
import json
import sys

from agrigate.farm_server import serve


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as f:
        config = json.load(f)
    httpd, service = serve(config)
    print(f"farm service on http://{httpd.server_address[0]}:{httpd.server_port}  ({len(service.sources)} sources"
          + ("" if service.token else ", no token: this computer only") + ")", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        service.close()
        httpd.server_close()


if __name__ == "__main__":
    main()
