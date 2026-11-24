"""A small HTTP service on a computer on the farm network: which animal is at which robot, live,
and what the farm's management server knows about each animal. For other programs on the farm
network, such as a camera system that wants to know which cow it sees (docs/FARM_API.md).

    python farm_server.py farm_server.json

The configuration (JSON):
    {"listen": "0.0.0.0:8099",
     "token": "",
     "robots": [{"name": "Robot 1", "driver": "lfw", "address": "10.4.1.101"}, ...],
     "server": {"driver": "t4c", "address": "10.4.1.1:6969", "user": "", "password": ""},
     "cowRefreshSeconds": 300}
"token": what every request carries (Authorization: Bearer <token>); without one only this
computer itself is answered. "server" is optional (without it only the live part); its password
is the user's, or the farm's licence key with an empty user where the server takes one. Token and
password can also come from the environment (FARM_SERVER_TOKEN, FARM_SERVER_PASSWORD), so that
they do not have to be in the file. It only reads: nothing here changes anything on the farm.
"""
from __future__ import annotations

import hmac
import ipaddress
import json
import os
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .farm import Farm, FarmError
from .live import Listener, now_ms

VERSION = 1
EVENTS_KEPT = 1000
WAIT_MAX = 60
# what an event and a robot's animal carry from the herd, next to what the robot said
HERD_FIELDS = ("lifeNumber", "workNumber", "production", "reproduction", "lactation", "daysInMilk", "pregnantDays", "expectedCalving")


class FarmService:
    """The robots' listeners, the herd (read again every cowRefreshSeconds) and the events of all
    robots in one row with its own numbering. Thread-safe; the HTTP handler only reads from it."""

    def __init__(self, config: dict, listener=Listener, farm=Farm):
        self.config = config
        self.token = config.get("token") or os.environ.get("FARM_SERVER_TOKEN", "")
        self._lock = threading.Condition()
        self._events: list[dict] = []
        self._seq = 0
        self._herd: list[dict] = []
        self._herd_at = 0
        self._herd_error = ""
        self._by_responder: dict[str, dict] = {}
        self._by_number: dict[str, dict] = {}
        self._farm_cls = farm
        self._stop = threading.Event()
        self.robots = []
        for r in config.get("robots", []):
            name = r.get("name") or r["address"]
            self.robots.append((name, r["driver"], listener(r["driver"], r["address"], on_event=self._on_event(name))))
        self._herd_thread = None
        if config.get("server"):
            self._herd_thread = threading.Thread(target=self._herd_loop, name="herd", daemon=True)
            self._herd_thread.start()

    # ---- the herd
    def _herd_loop(self):
        every = max(30, int(self.config.get("cowRefreshSeconds", 300)))
        while not self._stop.is_set():
            self.refresh_herd()
            self._stop.wait(every)

    def refresh_herd(self):
        s = self.config["server"]
        password = s.get("password") or os.environ.get("FARM_SERVER_PASSWORD", "")
        try:
            with self._farm_cls(s["driver"], s["address"], s.get("user", ""), password) as farm:
                animals = farm.cow()["animals"]
            with self._lock:
                self._herd, self._herd_at, self._herd_error = animals, now_ms(), ""
                self._by_responder = {a["responder"]: a for a in animals if a.get("responder")}
                self._by_number = {a["number"]: a for a in animals if a.get("number")}
        except (FarmError, OSError, ValueError) as e:
            with self._lock:
                self._herd_error = str(e)

    def _known(self, animal: dict | None) -> dict | None:
        """The herd's record of an animal a robot identified: by responder, else by number."""
        if not animal:
            return None
        return self._by_responder.get(animal.get("responder") or "") or self._by_number.get(animal.get("number") or "")

    def _enrich(self, animal: dict | None) -> dict | None:
        if animal is None:
            return None
        k = self._known(animal)
        return dict(animal, **{f: (k.get(f) if k else None) for f in HERD_FIELDS})

    # ---- events
    def _on_event(self, robot_name):
        def on_event(_listener, e):
            with self._lock:
                self._seq += 1
                ev = {"seq": self._seq, "time": e["time"], "robot": robot_name, "kind": e["kind"],
                      **self._enrich({"number": e["number"], "name": e["name"], "responder": e["responder"]})}
                self._events.append(ev)
                del self._events[:-EVENTS_KEPT]
                self._lock.notify_all()
        return on_event

    def events(self, after: int = 0, wait: float = 0) -> dict:
        deadline = time.time() + min(max(wait, 0), WAIT_MAX)
        with self._lock:
            while not (self._events and self._events[-1]["seq"] > after) and time.time() < deadline:
                self._lock.wait(deadline - time.time())
            return {"events": [e for e in self._events if e["seq"] > after], "last": self._seq}

    # ---- what the API shows
    def robots_json(self) -> dict:
        out = []
        for name, driver, lst in self.robots:
            st = lst.state()
            with self._lock:
                animal = self._enrich(st["animal"])
            out.append({"name": name, "driver": driver, "address": st["address"], "connected": st["connected"],
                        "lastMessage": st["lastMessage"], "animal": animal})
        return {"robots": out}

    def animals_json(self) -> dict:
        at = {}
        for name, _, lst in self.robots:
            a = lst.state()["animal"]
            if a:
                at[a["responder"] or a["number"]] = {"robot": name, "since": a["since"]}
        with self._lock:
            animals = [dict(a, atRobot=at.get(a.get("responder")) or at.get(a.get("number"))) for a in self._herd]
            return {"updated": self._herd_at or None, "error": self._herd_error, "animals": animals}

    def status_json(self) -> dict:
        robots = [lst.state() for _, _, lst in self.robots]
        with self._lock:
            return {"service": "agrigate farm", "version": VERSION, "time": now_ms(),
                    "robots": len(robots), "robotsConnected": sum(1 for r in robots if r["connected"]),
                    "cow": None if not self.config.get("server") else
                    {"animals": len(self._herd), "updated": self._herd_at or None, "error": self._herd_error},
                    "lastEvent": self._seq}

    def close(self):
        self._stop.set()
        for _, _, lst in self.robots:
            lst.close()


def allowed(token: str, authorization: str, client: str) -> bool:
    """With a token: a request that carries it. Without one: only this computer itself."""
    if token:
        return hmac.compare_digest(authorization.encode(), b"Bearer " + token.encode())
    return ipaddress.ip_address(client).is_loopback


def handler(service: FarmService):
    class Handler(BaseHTTPRequestHandler):
        server_version = "agrigate-farm"

        def log_message(self, *args):   # quiet: a camera system polls often
            pass

        def _json(self, status: int, body: dict):
            data = json.dumps(body, ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if not allowed(service.token, self.headers.get("Authorization", ""), self.client_address[0]):
                return self._json(401, {"error": "Authorization: Bearer <token> needed" if service.token else "no token set: only this computer is answered"})
            u = urllib.parse.urlsplit(self.path)
            q = {k: v[-1] for k, v in urllib.parse.parse_qs(u.query).items()}
            path = u.path.rstrip("/")
            try:
                if path in ("", "/api", "/api/status"):
                    return self._json(200, service.status_json())
                if path == "/api/robots":
                    return self._json(200, service.robots_json())
                if path == "/api/animals":
                    return self._json(200, service.animals_json())
                if path.startswith("/api/animals/"):
                    key = urllib.parse.unquote(path[len("/api/animals/"):])
                    for a in service.animals_json()["animals"]:
                        if key in (a.get("number"), a.get("responder"), a.get("lifeNumber")):
                            return self._json(200, a)
                    return self._json(404, {"error": "no such animal"})
                if path == "/api/events":
                    return self._json(200, service.events(int(q.get("after", 0) or 0), float(q.get("wait", 0) or 0)))
                return self._json(404, {"error": "unknown path"})
            except (ValueError, KeyError) as e:
                return self._json(400, {"error": str(e)})

    return Handler


def serve(config: dict, listener=Listener, farm=Farm) -> tuple[ThreadingHTTPServer, FarmService]:
    """Start the service and its HTTP server (serve_forever is the caller's)."""
    host, _, port = config.get("listen", "0.0.0.0:8099").rpartition(":")
    service = FarmService(config, listener, farm)
    httpd = ThreadingHTTPServer((host or "0.0.0.0", int(port)), handler(service))
    httpd.daemon_threads = True
    return httpd, service
