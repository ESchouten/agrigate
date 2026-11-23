"""For the tests of the farm server drivers (drivers/<name>/tests): the Python API on a freshly
built library, and a farm's server on this computer that answers what a test gives it."""
import json, os, sys, tempfile, threading, types
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "python"))
try:
    import bleak  # noqa: the package also holds the Bluetooth robots
except ImportError:
    m = types.ModuleType("bleak"); m.BleakClient = m.BleakScanner = object; sys.modules["bleak"] = m

from scraper_control.build_core import LIBNAME, build
os.environ.setdefault("SCRAPER_CORE_LIB", str(build(Path(tempfile.mkdtemp()) / LIBNAME)))

from scraper_control.farm import Farm, FarmError, drivers  # noqa: for the tests


class FakeFarm:
    """routes: {"GET /path?query": answer}. An answer is given as JSON; (status, text) is given
    as it is; a function gets the request's JSON and returns one of those. Every path but
    `login` wants the login the server last handed out (`token`), else 401. With `key` (a server
    that takes a licence key instead, as T4C does) every path wants the header "licensekey" with
    that key, written exactly so: a real T4C fails on "Licensekey"."""

    def __init__(self, login: str, routes: dict = None, key: str = None):
        self.login, self.routes, self.token, self.asked, self.key = login, routes or {}, "login-1", [], key
        farm = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                key = f"{self.command} {self.path}"
                body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
                farm.asked.append((key, dict(self.headers)))
                if farm.key is not None:
                    taken = any(k == "licensekey" and v == farm.key for k, v in self.headers.items())
                    answer = farm.routes.get(key, (404, "")) if taken else (500, '{"Data":null,"ServerEror":{"Code":500,"Message":"Object reference not set to an instance of an object."}}')
                elif key != farm.login and self.headers.get("Authorization") != "Bearer " + farm.token:
                    answer = (401, "")
                else:
                    answer = farm.routes.get(key, (404, ""))
                if callable(answer):
                    answer = answer(json.loads(body))
                status, text = answer if isinstance(answer, tuple) else (200, json.dumps(answer))
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(text.encode())))
                self.end_headers()
                self.wfile.write(text.encode())

            do_POST = do_GET

            def log_message(self, *a):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.address = "127.0.0.1:%d" % self.server.server_port
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def count(self, key: str) -> int:
        return sum(1 for k, _ in self.asked if k == key)


def closed_address() -> str:
    """An address nothing listens on."""
    server = ThreadingHTTPServer(("127.0.0.1", 0), BaseHTTPRequestHandler)
    server.server_close()
    return "127.0.0.1:%d" % server.server_port


def run(tests: dict) -> None:
    for name, fn in list(tests.items()):
        if name.startswith("test_"):
            fn(); print("ok", name)
