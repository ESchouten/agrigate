"""A session with one source on the farm (core/session_c.h): the farm's management server, a
robot, a feeding station. What it knows is read per contract, the same whatever the source is:

    from agrigate.session import Session
    with Session("t4c", "192.168.1.10", "user", "password") as farm:
        for robot in farm.milk()["robots"]:        # core/milk_c.h describes every field
            print(robot["name"], robot["state"])
        for cow in farm.cow()["animals"]:          # core/cow_c.h
            print(cow["number"], cow["name"], cow["reproduction"])
    with Session("astronaut", "10.4.1.101", on_event=print) as robot:
        robot.identity()["places"]                 # core/identity_c.h: which animal is where
        robot.events(after=0)

A T4C server also takes the farm's licence key instead of a login: Session("t4c", address, "", key).

What to say and what the answers mean is in the drivers (drivers/, C++), the same code as in
the apps; this module only moves their bytes: HTTP requests, or UDP datagrams on a thread of its
own for a source that tells by itself. It reads only.
"""
from __future__ import annotations

import ctypes
import http.client
import json
import socket
import threading
import time
import urllib.parse

from .core import LOGIN_REJECTED, OK, UNEXPECTED, UNREACHABLE, UNSUPPORTED, drivers, lib as _lib, text  # noqa: F401

SECRET = ("token", "password", "license")   # answer fields left out of a recording


class SessionError(Exception):
    def __init__(self, result: int, message: str):
        super().__init__(message or ["", "no answer from the source", "user name or password rejected",
                                     "unexpected answer", "the driver does not serve this"][result])
        self.result = result


def now_ms() -> int:
    return int(time.time() * 1000)


def _hide(value):
    if isinstance(value, dict):
        return {k: "..." if any(s in k.lower() for s in SECRET) else _hide(v) for k, v in value.items()}
    return [_hide(v) for v in value] if isinstance(value, list) else value


class Session:
    """One source. `contracts`: what its driver serves ("milk", "cow", "identity").
    on_event: called with each new identity event, on the thread that found it.
    refresh: read every contract again every so many seconds, on a thread of its own, for latest().
    recording: a list that gets every request and its answer, logins and passwords left out (to
    see what a real server says)."""

    def __init__(self, driver: str, address: str, user: str = "", password: str = "", timeout: float = 15,
                 recording: list | None = None, on_event=None, refresh: float = 0):
        info = next((d for d in drivers() if d["id"] == driver), None)
        if not info:
            raise ValueError(f"no driver '{driver}': one of " + ", ".join(d["id"] for d in drivers()))
        self.driver, self.address, self.contracts, self.link = driver, address, info["contracts"], info["links"][0]
        if self.link["kind"] not in ("http", "udp"):
            raise ValueError(f"'{driver}' is reached over {self.link['kind']}: see agrigate.Robot")
        self.timeout, self.recording, self.on_event = timeout, recording, on_event
        self.notes = ""   # what the last read had to leave out
        self._s = _lib().session_open(driver.encode(), address.encode(), user.encode(), password.encode())
        self._lock = threading.RLock()
        self._seen = 0
        self._latest = {c: (self._known(c), "not read yet") for c in self.contracts}
        self._stop = threading.Event()
        self._sock = self._thread = None
        if self.link["kind"] == "udp":
            host, _, port = address.partition(":")
            self._to = (host, int(port or self.link["port"]))
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.bind(("0.0.0.0", 0))
            self._sock.settimeout(0.1)
            self._thread = threading.Thread(target=self._listen, name=f"{driver} {address}", daemon=True)
        elif refresh:
            self._thread = threading.Thread(target=self._refresh, args=(refresh,), name=f"{driver} {address}", daemon=True)
        if self._thread:
            self._thread.start()

    # ---- the contracts
    def milk(self) -> dict:
        """The milking robots, read now (core/milk_c.h)."""
        return self.read("milk")

    def cow(self) -> dict:
        """The herd, read now (core/cow_c.h)."""
        return self.read("cow")

    def identity(self) -> dict:
        """Which animal is at each place (core/identity_c.h)."""
        return self.read("identity")

    def events(self, after: int = 0) -> dict:
        """The identity events after event `after`."""
        with self._lock:
            return json.loads(text(_lib().identity_events_json, self._s, int(after)))

    def read(self, contract: str) -> dict:
        L = _lib()
        with self._lock:
            L.session_tick(self._s, now_ms())
            started = getattr(L, contract + "_read")(self._s)
            if started != OK:
                raise SessionError(started, "")
            while request := text(L.session_request_json, self._s):
                status, body = self._send(json.loads(request))
                L.session_response(self._s, status, body, len(body))
            self.notes = text(L.session_last_error, self._s)
            if L.session_result(self._s) != OK:
                raise SessionError(L.session_result(self._s), self.notes)
            known = self._known(contract)
        if contract == "identity":
            self._tell()
        return known

    def latest(self, contract: str) -> tuple[dict, str]:
        """What is known now, without waiting for the source: (data, what is wrong or ""). For a
        source that has to be asked that is what the last read (every `refresh` seconds) gave."""
        if self._sock is None:
            return self._latest[contract]
        try:
            return self.read(contract), ""
        except SessionError as e:
            return self._known(contract), str(e)

    def _known(self, contract: str) -> dict:
        with self._lock:
            return json.loads(text(getattr(_lib(), contract + "_json"), self._s))

    def _tell(self):
        if not self.on_event:
            return
        with self._lock:
            new = self.events(self._seen)
            self._seen = new["last"]
        for e in new["events"]:
            try:
                self.on_event(self, e)
            except Exception:
                pass

    # ---- http
    def _refresh(self, every: float):
        while not self._stop.is_set():
            for contract in self.contracts:
                try:
                    result = self.read(contract), ""
                except SessionError as e:
                    result = self._known(contract), str(e)
                self._latest[contract] = result
            self._stop.wait(every)

    def _send(self, r: dict) -> tuple[int, bytes]:
        # http.client, not urllib: urllib changes a header's case ("licensekey" -> "Licensekey") and
        # a T4C server then fails; the driver's headers go out exactly as it wrote them
        u = urllib.parse.urlsplit(r["url"])
        conn_type = http.client.HTTPSConnection if u.scheme == "https" else http.client.HTTPConnection
        body = r["body"].encode()
        try:
            conn = conn_type(u.hostname, u.port, timeout=self.timeout)
            try:
                conn.putrequest(r["method"], (u.path or "/") + ("?" + u.query if u.query else ""), skip_accept_encoding=True)
                for k, v in r["headers"].items():
                    conn.putheader(k, v)
                if body:
                    conn.putheader("Content-Length", str(len(body)))
                conn.endheaders(body or None)
                answer = conn.getresponse()
                status, body = answer.status, answer.read()
            finally:
                conn.close()
        except OSError:   # no route, refused, timeout
            status, body = 0, b""
        if self.recording is not None:
            try:
                said = _hide(json.loads(body))
            except ValueError:
                said = body.decode(errors="replace")
            self.recording.append({"request": r["method"] + " " + r["url"], "status": status, "answer": said})
        return status, body

    # ---- udp
    def _flush(self):
        L, buf = _lib(), ctypes.create_string_buffer(2048)
        while True:
            with self._lock:
                n = L.session_outgoing(self._s, buf, len(buf))
            if n <= 0:
                return
            try:
                self._sock.sendto(buf.raw[:n], self._to)
            except OSError:
                pass   # the source is not there (now): the driver tries again on its own

    def _listen(self):
        L = _lib()
        while not self._stop.is_set():
            with self._lock:
                L.session_tick(self._s, now_ms())
            self._flush()
            try:
                data, (host, _) = self._sock.recvfrom(65535)
            except (socket.timeout, ConnectionResetError):
                continue
            except OSError:
                time.sleep(0.2)
                continue
            if host != self._to[0]:
                continue
            with self._lock:
                L.session_incoming(self._s, data, len(data), now_ms())
            self._tell()

    def close(self) -> None:
        if not self._s:
            return
        self._stop.set()
        if self._thread:
            self._thread.join(2)
        if self._sock:
            _lib().session_stop(self._s)
            self._flush()
            self._sock.close()
        with self._lock:
            _lib().session_close(self._s)
            self._s = None

    def __enter__(self):
        return self

    def __exit__(self, *exc) -> None:
        self.close()
