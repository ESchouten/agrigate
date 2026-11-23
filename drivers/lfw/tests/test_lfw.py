"""Offline tests: the LFW live driver through its C API (core/live_c.h), fed with datagrams as
a robot's proxy sends them (the format was read on two Astronaut A4s). Run:
python3 drivers/lfw/tests/test_lfw.py"""
import ctypes, json, os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from farm_test import run  # noqa: builds the library and points the Python API at it
from scraper_control.core import text
from scraper_control.live import _lib, drivers

T0 = 1_795_430_000_000   # ms


def message(mid, data, sender=3, dest=1):
    return struct.pack("<IHHHH", 0xD156, mid, sender, dest, len(data)) + data


def cow(responder, number, name):
    return message(0x116, struct.pack("<II", responder, number) + name.encode("utf-16-le") + b"\0\0")


class Robot:
    def __init__(self):
        self.L = _lib()
        self.s = self.L.live_open(b"lfw", b"10.4.1.101")

    def tick(self, t):
        self.L.live_tick(self.s, t)
        return self.out()

    def out(self):
        sent, buf = [], ctypes.create_string_buffer(256)
        while (n := self.L.live_outgoing(self.s, buf, len(buf))) > 0:
            sent.append(buf.raw[:n])
        return sent

    def hear(self, datagram, t):
        self.L.live_incoming(self.s, datagram, len(datagram), t)

    def state(self, t):
        return json.loads(text(self.L.live_json, self.s, t))

    def events(self, after=0):
        return json.loads(text(self.L.live_events_json, self.s, after))


def registration(on):
    return struct.pack("<IH", 0xD157, 1 if on else 0) + b"scraper-control".ljust(22, b"\0")


def test_listed():
    assert {"id": "lfw", "name": "LFW", "link": {"kind": "udp", "port": 6000}} in drivers()


def test_registers_and_renews_every_two_seconds():
    r = Robot()
    assert r.tick(T0) == [registration(True)]
    assert r.tick(T0 + 1500) == []
    assert r.tick(T0 + 2000) == [registration(True)]


def test_goodbye():
    r = Robot()
    r.tick(T0)
    r.L.live_stop(r.s)
    assert r.out() == [registration(False)]


def test_a_cow_arrives():
    r = Robot()
    assert r.state(T0)["animal"] is None and r.state(T0)["connected"] is False
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 10)
    st = r.state(T0 + 20)
    assert st["connected"] and st["lastMessage"] == T0 + 10
    assert st["animal"] == {"number": "15", "name": "DINA 1", "responder": "1000015", "since": T0 + 10}
    assert r.events() == {"events": [{"seq": 1, "time": T0 + 10, "kind": "arrival", "number": "15", "name": "DINA 1", "responder": "1000015"}], "last": 1}


def test_names_beyond_ascii():
    r = Robot()
    r.hear(cow(1000117, 117, "HERMINA Ü 6  "), T0)
    assert r.state(T0)["animal"]["name"] == "HERMINA Ü 6"


def test_the_same_cow_again_within_five_seconds_is_one_arrival():
    r = Robot()
    r.hear(cow(1000015, 15, "DINA 1"), T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 3000)
    assert r.events()["last"] == 1
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 600_000)   # a next visit
    r.hear(cow(1000041, 41, "JANNA 2"), T0 + 600_500)
    ev = r.events(after=1)["events"]
    assert [e["number"] for e in ev] == ["15", "41"] and r.state(T0 + 600_600)["animal"]["number"] == "41"


def test_other_messages_only_keep_it_connected():
    r = Robot()
    r.hear(message(0x13B, bytes([10, 12, 9, 11])), T0)                    # quarter milk speed
    r.hear(cow(0, 0, ""), T0 + 1)                                         # no cow
    r.hear(b"\x57\xd1\x00\x00" + b"\0" * 20, T0 + 2)                     # not a message
    r.hear(cow(1000015, 15, "DINA 1")[:20], T0 + 3)                     # cut off
    st = r.state(T0 + 5)
    assert st["animal"] is None and st["connected"] and r.events()["events"] == []
    assert r.state(T0 + 3 + 10_000)["connected"] is False                # silent for 10 s


def test_unknown_driver():
    assert _lib().live_open(b"nope", b"10.4.1.101") is None


if __name__ == "__main__":
    run(globals())
