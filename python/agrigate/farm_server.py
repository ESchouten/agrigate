"""A small HTTP service on a computer on the farm network: what the farm's sources know, over one
API. Which animal is where (identity), the herd (cow) and the milking robots (milk), whatever
each comes from. For other programs on the farm network, such as a camera system that wants to
know which cow it sees (docs/farm/API.md).

    python farm_server.py farm_server.json

The configuration (JSON):
    {"listen": "0.0.0.0:8099",
     "token": "",
     "sources": [{"name": "Robot 1", "driver": "astronaut", "address": "10.4.1.101"},
                 {"driver": "t4c", "address": "10.4.1.1:6969", "user": "", "password": ""}],
     "refreshSeconds": 300}
"token": what every request carries (Authorization: Bearer <token>); without one only this
computer itself is answered. A source is anything a driver reaches (python farm.py lists them);
"name" names its place when the source has none of its own; "password" is the user's, or the
farm's licence key with an empty user where the server takes one. A source that has to be asked
is asked again every "refreshSeconds". Token and password can also come from the environment
(FARM_SERVER_TOKEN, FARM_SERVER_PASSWORD), so that they do not have to be in the file. It only
reads: nothing here changes anything on the farm.
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

from .session import Session, now_ms

VERSION = 2
EVENTS_KEPT = 1000
WAIT_MAX = 60
# what an identified animal carries from the herd, next to what the source said
HERD_FIELDS = ("lifeNumber", "workNumber", "production", "reproduction", "lactation", "daysInMilk", "pregnantDays", "expectedCalving")


class FarmService:
    """A session per source, and the identity events of all of them in one row with its own
    numbering. Thread-safe; the HTTP handler only reads from it."""

    def __init__(self, config: dict, session=Session):
        self.token = config.get("token") or os.environ.get("FARM_SERVER_TOKEN", "")
        self._lock = threading.Condition()
        self._events: list[dict] = []
        self._seq = 0
        every = max(30, int(config.get("refreshSeconds", 300)))
        self.sources = []   # (name, session)
        for c in config.get("sources", []):
            password = c.get("password") or os.environ.get("FARM_SERVER_PASSWORD", "")
            name = c.get("name", "")
            self.sources.append((name, session(c["driver"], c["address"], c.get("user", ""), password, refresh=every, on_event=self._on_event(name))))

    def _latest(self, contract: str):
        """(name, session, data, error) of every source that serves the contract."""
        return [(name, s, *s.latest(contract)) for name, s in self.sources if contract in s.contracts]

    # ---- identity
    def _herd_of(self):
        """How to find the herd's record of an animal a source identified: by responder, else by number."""
        herd = [a for _, _, data, _ in self._latest("cow") for a in data.get("animals", [])]
        by_responder = {a["responder"]: a for a in herd if a.get("responder")}
        by_number = {a["number"]: a for a in herd if a.get("number")}

        def enrich(animal: dict | None) -> dict | None:
            if animal is None:
                return None
            known = by_responder.get(animal["responder"]) or by_number.get(animal["number"])
            return dict(animal, **{f: (known.get(f) if known else None) for f in HERD_FIELDS})
        return enrich

    def places_json(self) -> dict:
        enrich = self._herd_of()
        return {"places": [dict(p, name=p["name"] or name or p["id"], driver=s.driver, connected=not error, updated=data["updated"], animal=enrich(p["animal"]))
                           for name, s, data, error in self._latest("identity") for p in data["places"]]}

    def _on_event(self, name: str):
        def on_event(session, e):
            place = next((p for p in session.latest("identity")[0]["places"] if p["id"] == e["place"]), {"id": e["place"], "name": "", "kind": "unknown"})
            event = {"time": e["time"], "kind": e["kind"], "place": {"id": place["id"], "name": place["name"] or name or place["id"], "kind": place["kind"]},
                     "animal": self._herd_of()(e["animal"])}
            with self._lock:
                self._seq += 1
                self._events.append({"seq": self._seq, **event})
                del self._events[:-EVENTS_KEPT]
                self._lock.notify_all()
        return on_event

    def events(self, after: int = 0, wait: float = 0) -> dict:
        deadline = time.time() + min(max(wait, 0), WAIT_MAX)
        with self._lock:
            while not (self._events and self._events[-1]["seq"] > after) and time.time() < deadline:
                self._lock.wait(deadline - time.time())
            return {"events": [e for e in self._events if e["seq"] > after], "last": self._seq}

    # ---- cow, milk
    def animals_json(self) -> dict:
        at = {}
        for p in self.places_json()["places"]:
            if p["animal"]:
                at[p["animal"]["responder"] or p["animal"]["number"]] = {"place": p["name"], "since": p["animal"]["since"]}
        return {"animals": [dict(a, at=at.get(a.get("responder")) or at.get(a.get("number")))
                            for _, _, data, _ in self._latest("cow") for a in data.get("animals", [])]}

    def robots_json(self) -> dict:
        return {"robots": [dict(r, driver=s.driver, updated=data["updated"]) for _, s, data, _ in self._latest("milk") for r in data.get("robots", [])]}

    def status_json(self) -> dict:
        sources = []
        for name, s in self.sources:
            contracts = {}
            for c in s.contracts:
                data, error = s.latest(c)
                contracts[c] = {"updated": data.get("updated"), "error": error}
            sources.append({"name": name, "driver": s.driver, "address": s.address, "contracts": contracts})
        with self._lock:
            return {"service": "agrigate farm", "version": VERSION, "time": now_ms(), "sources": sources, "lastEvent": self._seq}

    def close(self):
        for _, s in self.sources:
            s.close()


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
                if path == "/api/identity/places":
                    return self._json(200, service.places_json())
                if path == "/api/identity/events":
                    return self._json(200, service.events(int(q.get("after", 0) or 0), float(q.get("wait", 0) or 0)))
                if path == "/api/cow/animals":
                    return self._json(200, service.animals_json())
                if path.startswith("/api/cow/animals/"):
                    key = urllib.parse.unquote(path[len("/api/cow/animals/"):])
                    for a in service.animals_json()["animals"]:
                        if key in (a.get("number"), a.get("responder"), a.get("lifeNumber")):
                            return self._json(200, a)
                    return self._json(404, {"error": "no such animal"})
                if path == "/api/milk/robots":
                    return self._json(200, service.robots_json())
                return self._json(404, {"error": "unknown path"})
            except (ValueError, KeyError) as e:
                return self._json(400, {"error": str(e)})

    return Handler


def serve(config: dict, session=Session) -> tuple[ThreadingHTTPServer, FarmService]:
    """Start the service and its HTTP server (serve_forever is the caller's)."""
    host, _, port = config.get("listen", "0.0.0.0:8099").rpartition(":")
    service = FarmService(config, session)
    httpd = ThreadingHTTPServer((host or "0.0.0.0", int(port)), handler(service))
    httpd.daemon_threads = True
    return httpd, service
