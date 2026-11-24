"""Live: which animal is at each robot right now, from the robots themselves (core/live_c.h).

    from agrigate.live import Listener
    with Listener("astronaut", "10.4.1.101") as robot:      # one per robot
        ...
        robot.state()            # {"connected", "lastMessage", "animal": {"number", "name", "responder", "since"}}
        robot.events(after=0)    # {"events": [{"seq", "time", "kind": "arrival", ...}], "last": n}

What the robot's messages mean is in the driver (drivers/, C++); this module only moves its UDP
datagrams, on a thread of its own. It only listens: a driver sends no more than its registration.
"""
from __future__ import annotations

import ctypes
import json
import socket
import threading
import time

from .core import lib, text


def now_ms() -> int:
    return int(time.time() * 1000)


def _lib():
    L = lib()
    if L.live_open.restype is not ctypes.c_void_p:
        S = ctypes.c_void_p
        L.live_open.restype = S
        L.live_open.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        L.live_close.argtypes = [S]
        L.live_tick.argtypes = [S, ctypes.c_int64]
        L.live_outgoing.argtypes = [S, ctypes.c_char_p, ctypes.c_int32]
        L.live_incoming.argtypes = [S, ctypes.c_char_p, ctypes.c_int32, ctypes.c_int64]
        L.live_stop.argtypes = [S]
        L.live_json.argtypes = [S, ctypes.c_int64, ctypes.c_char_p, ctypes.c_int32]
        L.live_events_json.argtypes = [S, ctypes.c_int64, ctypes.c_char_p, ctypes.c_int32]
        L.live_drivers_json.argtypes = [ctypes.c_char_p, ctypes.c_int32]
    return L


def drivers() -> list[dict]:
    """The live drivers in this build: [{"id", "name", "link": {"kind": "udp", "port"}}]."""
    return json.loads(text(_lib().live_drivers_json))


class Listener:
    """One robot. Starts listening at once; close() (or the with block) says goodbye to the robot.
    on_event: called with each new event (on the listener's thread)."""

    def __init__(self, driver: str, address: str, on_event=None, port: int | None = None):
        L = _lib()
        info = next((d for d in drivers() if d["id"] == driver), None)
        if not info:
            raise ValueError(f"no live driver '{driver}': one of " + ", ".join(d["id"] for d in drivers()))
        self.driver, self.address, self.on_event = driver, address, on_event
        self.port = port or info["link"]["port"]
        self._s = L.live_open(driver.encode(), address.encode())
        self._lock = threading.Lock()
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("0.0.0.0", 0))
        self._sock.settimeout(0.1)
        self._stop = threading.Event()
        self._seen = 0
        self._thread = threading.Thread(target=self._run, name=f"live {address}", daemon=True)
        self._thread.start()

    def _flush(self):
        L, buf = _lib(), ctypes.create_string_buffer(2048)
        while True:
            with self._lock:
                n = L.live_outgoing(self._s, buf, len(buf))
            if n <= 0:
                return
            try:
                self._sock.sendto(buf.raw[:n], (self.address, self.port))
            except OSError:
                pass   # the robot is not there (now): the driver tries again on its own

    def _run(self):
        L = _lib()
        while not self._stop.is_set():
            with self._lock:
                L.live_tick(self._s, now_ms())
            self._flush()
            try:
                data, (host, _) = self._sock.recvfrom(65535)
            except (socket.timeout, ConnectionResetError):
                continue
            except OSError:
                time.sleep(0.2)
                continue
            if host != self.address:
                continue
            with self._lock:
                L.live_incoming(self._s, data, len(data), now_ms())
            if self.on_event:
                new = self.events(self._seen)
                for e in new["events"]:
                    try:
                        self.on_event(self, e)
                    except Exception:
                        pass
                self._seen = new["last"]

    def state(self) -> dict:
        with self._lock:
            return json.loads(text(_lib().live_json, self._s, now_ms()))

    def events(self, after: int = 0) -> dict:
        with self._lock:
            return json.loads(text(_lib().live_events_json, self._s, int(after)))

    def close(self) -> None:
        if not self._s:
            return
        self._stop.set()
        self._thread.join(2)
        with self._lock:
            _lib().live_stop(self._s)
        self._flush()
        self._sock.close()
        with self._lock:
            _lib().live_close(self._s)
            self._s = None

    def __enter__(self):
        return self

    def __exit__(self, *exc) -> None:
        self.close()
