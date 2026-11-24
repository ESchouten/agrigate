"""ctypes binding to the C APIs (core/session_c.h and the contracts on it), built from the
drivers, and a manure robot's session on it (core/clean_c.h).

The drivers hold all logic (screens, route steps, safety rules, what a server's answers mean);
this module only moves bytes. The same code runs on the M5Stack and in the apps.

The library is $AGRIGATE_CORE_LIB, or the one next to this file, which is built on first
use and after a change (needs a C++17 compiler). Or build it yourself:  python -m agrigate.build_core
"""
from __future__ import annotations

import ctypes
import json
import os
import threading
from pathlib import Path

# results: session_c.h, then clean_c.h
OK, UNREACHABLE, LOGIN_REJECTED, UNEXPECTED, UNSUPPORTED = range(5)
BUSY, NO_CONFIRMATION, CANCELLED = 10, 11, 12
IN_PROGRESS = 100

HERE = Path(__file__).resolve().parent


def library_path() -> Path:
    from .build_core import LIBNAME, build, sources
    env = os.environ.get("AGRIGATE_CORE_LIB")
    if env and Path(env).exists():
        return Path(env)
    p = HERE / LIBNAME
    # rebuild when the driver or the build secrets changed
    if p.exists() and p.stat().st_mtime >= max(f.stat().st_mtime for f in sources()):
        return p
    try:   # first use: build it next to this package (needs g++ or clang++)
        return build()
    except Exception as e:
        raise FileNotFoundError(f"{LIBNAME} not found and building it failed ({e}); "
                                "build it with: python -m agrigate.build_core") from e


_lib = None


def lib():
    global _lib
    if _lib is None:
        L = ctypes.CDLL(str(library_path()))
        S, out, i32 = ctypes.c_void_p, [ctypes.c_char_p, ctypes.c_int32], ctypes.c_int32
        L.session_open.restype = S
        L.session_open.argtypes = [ctypes.c_char_p] * 4
        L.session_tick.argtypes = [S, ctypes.c_int64]
        L.session_response.argtypes = [S, i32, ctypes.c_char_p, i32]
        L.session_incoming.argtypes = [S, ctypes.c_char_p, i32, ctypes.c_int64]
        L.session_wait.argtypes = [S, i32]
        L.session_drivers_json.argtypes = out
        L.identity_events_json.argtypes = [S, ctypes.c_int64] + out
        L.clean_start_route.argtypes = [S, ctypes.c_char, i32]
        L.clean_input.argtypes = [S, ctypes.c_char_p, i32]
        L.clean_set_language.argtypes = L.clean_remember.argtypes = [S, ctypes.c_char_p]
        for name in ("clean_set_planning", "clean_answer_stop"):
            getattr(L, name).argtypes = [S, i32]
        for name in ("session_close", "session_stop", "session_result", "session_link_down", "milk_read", "cow_read",
                     "identity_read", "clean_connect", "clean_stop_route", "clean_read_routes", "clean_finish",
                     "clean_cancel", "clean_release"):
            getattr(L, name).argtypes = [S]
        for name in ("session_request_json", "session_last_error", "session_outgoing", "milk_json", "cow_json",
                     "identity_json", "clean_status_json", "clean_routes_json", "clean_next_step", "clean_memory"):
            getattr(L, name).argtypes = [S] + out
        _lib = L
    return _lib


def text(fn, *args) -> str:
    """What a C call that copies text into (buf, size) gives: asked for its length, then read."""
    n = fn(*args, None, 0)
    buf = ctypes.create_string_buffer(n + 1)
    fn(*args, buf, n + 1)
    return buf.value.decode(errors="replace")


def drivers() -> list[dict]:
    """The drivers in this build: [{"id", "name", "contracts", "links"}, ...] (core/session_c.h)."""
    return json.loads(text(lib().session_drivers_json))


class CoreSession:
    """A session with one manure robot (core/clean_c.h). `write(bytes)` is called (from the
    session's own thread) for every frame to send; call `feed(bytes)` for what the robot sends.
    memory: what `memory()` gave last time for this robot.

    Commands are blocking here (`connect()`, `start_route()`, ...) and return a result
    code; run them in a thread (e.g. asyncio.to_thread) when on an event loop."""

    def __init__(self, write, pin: str | None = None, memory: str = "", language: str = "en", driver: str = "discovery"):
        L = lib()
        self._write = write
        self._s = L.session_open(driver.encode(), b"", b"", (pin or "").encode())
        if not self._s:
            raise ValueError(f"no driver '{driver}'")
        L.clean_set_language(self._s, language.encode())   # of last_error(): "nl" or "en"
        L.clean_remember(self._s, memory.encode())
        self._news = threading.Condition()
        self._pump = threading.Thread(target=self._run, daemon=True)
        self._pump.start()

    def _run(self) -> None:   # the link's exchange (core/session_c.h)
        L, buf, s = lib(), ctypes.create_string_buffer(4096), self._s
        while self._s:
            L.session_wait(s, 250)
            while (n := L.session_outgoing(s, buf, len(buf))) > 0:
                self._write(buf.raw[:n])
            with self._news:
                self._news.notify_all()

    def close(self) -> None:
        if self._s:
            s, self._s = self._s, None
            self._pump.join()
            lib().session_close(s)

    def __del__(self):
        self.close()

    # bytes in / link state
    def feed(self, data: bytes) -> None:
        if self._s:   # a robot may still say something while the session closes
            lib().session_incoming(self._s, data, len(data), 0)

    def link_down(self) -> None:
        if self._s:
            lib().session_link_down(self._s)

    def cancel(self) -> None:
        """Cancel the running command from another thread (e.g. on Ctrl+C): it ends with
        CANCELLED and the robot back as it was, until Start is pressed for a route."""
        if self._s:
            lib().clean_cancel(self._s)

    # commands (blocking)
    def _do(self, started: int) -> int:
        if started != OK:
            return started
        with self._news:
            while (r := lib().session_result(self._s)) == IN_PROGRESS:
                self._news.wait(0.5)
        return r

    def connect(self) -> int:
        return self._do(lib().clean_connect(self._s))

    def start_route(self, letter: str, dry_run: bool = False) -> int:
        return self._do(lib().clean_start_route(self._s, letter.upper().encode(), int(dry_run)))

    def stop_route(self) -> int:
        return self._do(lib().clean_stop_route(self._s))

    def answer_stop(self, finish: bool) -> int:
        return self._do(lib().clean_answer_stop(self._s, int(finish)))

    def set_planning(self, on: bool) -> int:
        return self._do(lib().clean_set_planning(self._s, int(on)))

    def press(self, name: str) -> int:
        """One of the robot's own inputs (a panel key), by the driver's name for it."""
        return self._do(lib().clean_input(self._s, name.encode(), 0))

    def finish(self) -> int:
        """Back home from a safe menu, planning as at connect (before close)."""
        return self._do(lib().clean_finish(self._s))

    # state
    def status(self) -> dict:
        return json.loads(text(lib().clean_status_json, self._s))

    def last_error(self) -> str:
        return text(lib().session_last_error, self._s)

    def memory(self) -> str:
        """What to keep for this robot until the next session."""
        return text(lib().clean_memory, self._s)
