"""Robots that are read through the farm's management server (core/robot_c.h): milking robots.

    from scraper_control.farm import Farm
    with Farm("t4c", "192.168.1.10", "user", "password") as farm:
        for robot in farm.milking()["robots"]:      # core/milking_c.h describes every field
            print(robot["name"], robot["state"])

What to ask and what the answers mean is in the drivers (drivers/, C++), the same code as in
the apps; this module only sends their HTTP requests. It reads only.
"""
from __future__ import annotations

import ctypes
import json
import urllib.error
import urllib.request

from .core import lib

OK, UNREACHABLE, LOGIN_REJECTED, UNEXPECTED, UNSUPPORTED = range(5)
SECRET = ("token", "password", "license")   # answer fields left out of a recording


class FarmError(Exception):
    def __init__(self, result: int, message: str):
        super().__init__(message or ["", "no answer from the server", "user name or password rejected",
                                     "unexpected answer", "the driver does not know this kind of robot"][result])
        self.result = result


def _text(fn, *args) -> str:
    n = fn(*args, None, 0)
    buf = ctypes.create_string_buffer(n + 1)
    fn(*args, buf, n + 1)
    return buf.value.decode(errors="replace")


def _lib():
    L = lib()
    if L.robot_open.restype is not ctypes.c_void_p:
        S, text = ctypes.c_void_p, [ctypes.c_char_p, ctypes.c_int32]
        L.robot_open.restype = S
        L.robot_open.argtypes = [ctypes.c_char_p] * 4
        L.robot_close.argtypes = [S]
        L.robot_response.argtypes = [S, ctypes.c_int32, ctypes.c_char_p, ctypes.c_int32]
        L.robot_drivers_json.argtypes = text
        for name in ("robot_request_json", "robot_last_error", "milking_json"):
            getattr(L, name).argtypes = [S] + text
        for name in ("robot_result", "milking_read"):
            getattr(L, name).argtypes = [S]
    return L


def drivers() -> list[dict]:
    """The server software this build knows: [{"id", "name", "kinds", "link"}, ...]."""
    return json.loads(_text(_lib().robot_drivers_json))


def _hide(value):
    if isinstance(value, dict):
        return {k: "..." if any(s in k.lower() for s in SECRET) else _hide(v) for k, v in value.items()}
    return [_hide(v) for v in value] if isinstance(value, list) else value


class Farm:
    """A session with one farm's server. `recording`: a list that gets every request and its
    answer, logins and passwords left out (to see what a real server says)."""

    def __init__(self, driver: str, address: str, user: str, password: str, timeout: float = 15, recording: list | None = None):
        self._s = _lib().robot_open(driver.encode(), address.encode(), user.encode(), password.encode())
        if not self._s:
            raise ValueError(f"no driver '{driver}': one of " + ", ".join(d["id"] for d in drivers()))
        self.timeout, self.recording = timeout, recording
        self.notes = ""   # what the last read had to leave out

    def close(self) -> None:
        if self._s:
            _lib().robot_close(self._s)
            self._s = None

    def __enter__(self):
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def milking(self) -> dict:
        """The farm's milking robots, read now."""
        return self._read("milking")

    def _read(self, kind: str) -> dict:
        L = _lib()
        started = getattr(L, kind + "_read")(self._s)
        if started != OK:
            raise FarmError(started, "")
        while request := _text(L.robot_request_json, self._s):
            status, body = self._send(json.loads(request))
            L.robot_response(self._s, status, body, len(body))
        self.notes = _text(L.robot_last_error, self._s)
        if L.robot_result(self._s) != OK:
            raise FarmError(L.robot_result(self._s), self.notes)
        return json.loads(_text(getattr(L, kind + "_json"), self._s))

    def _send(self, r: dict) -> tuple[int, bytes]:
        request = urllib.request.Request(r["url"], r["body"].encode() or None, r["headers"], method=r["method"])
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as answer:
                status, body = answer.status, answer.read()
        except urllib.error.HTTPError as e:
            status, body = e.code, e.read()
        except OSError:   # no route, refused, timeout
            status, body = 0, b""
        if self.recording is not None:
            try:
                said = _hide(json.loads(body))
            except ValueError:
                said = body.decode(errors="replace")
            self.recording.append({"request": r["method"] + " " + r["url"], "status": status, "answer": said})
        return status, body
