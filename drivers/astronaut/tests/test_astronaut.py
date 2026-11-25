"""Offline tests: the Astronaut driver through the C API (core/session_c.h, identity_c.h), fed
with datagrams as a robot's proxy sends them (the format was read on two Astronaut A4s), and once
through the Python API with a robot made up on this computer. Run:
python3 drivers/astronaut/tests/test_astronaut.py"""
import ctypes, json, os, socket, struct, sys, threading, time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from farm_test import Session, drivers, run  # noqa: builds the library and points the Python API at it
from agrigate.core import text
from agrigate.session import OK, UNREACHABLE, _lib

T0 = 1_795_430_000_000   # ms
ADDRESS = "10.4.1.101"


def message(mid, data, sender=3, dest=1):
    return struct.pack("<IHHHH", 0xD156, mid, sender, dest, len(data)) + data


def cow(responder, number, name):
    return message(0x116, struct.pack("<II", responder, number) + name.encode("utf-16-le") + b"\0\0")


def start_milking():
    return message(0x002, bytes(38))   # the teat positions; the driver only needs to know the milking started


def visit_end(v=1):
    return message(0x402, bytes([v, 0, 0, 0]))


def registration(on):
    return struct.pack("<IH", 0xD157, 1 if on else 0) + b"agrigate".ljust(22, b"\0")


class Robot:
    def __init__(self):
        self.L = _lib()
        self.s = self.L.session_open(b"astronaut", ADDRESS.encode(), b"", b"")

    def tick(self, t):
        self.L.session_tick(self.s, t)
        return self.out()

    def out(self):
        sent, buf = [], ctypes.create_string_buffer(256)
        while (n := self.L.session_outgoing(self.s, buf, len(buf))) > 0:
            sent.append(buf.raw[:n])
        return sent

    def hear(self, datagram, t):
        self.L.session_incoming(self.s, datagram, len(datagram), t)

    def read(self, t):
        """(result, the identity JSON) at time t."""
        self.L.session_tick(self.s, t)
        assert self.L.identity_read(self.s) == OK
        return self.L.session_result(self.s), json.loads(text(self.L.identity_json, self.s))

    def events(self, after=0):
        return json.loads(text(self.L.identity_events_json, self.s, after))


def test_listed():
    assert {"id": "astronaut", "name": "Astronaut", "contracts": ["identity"], "links": [{"kind": "udp", "port": 6000}]} in drivers()


def test_registers_and_renews_every_two_seconds():
    r = Robot()
    assert r.tick(T0) == [registration(True)]
    assert r.tick(T0 + 1500) == []
    assert r.tick(T0 + 2000) == [registration(True)]


def test_goodbye():
    r = Robot()
    r.tick(T0)
    r.L.session_stop(r.s)
    assert r.out() == [registration(False)]


def test_the_robot_is_a_place_before_it_says_anything():
    r = Robot()
    result, identity = r.read(T0)
    assert result == UNREACHABLE and text(r.L.session_last_error, r.s) == "no message from " + ADDRESS
    assert identity == {"updated": None, "places": [{"id": ADDRESS, "name": "", "kind": "milkingRobot", "animal": None, "visitSince": None}]}


def test_a_cow_is_identified():
    r = Robot()
    r.tick(T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 10)
    result, identity = r.read(T0 + 20)
    assert result == OK and identity["updated"] == T0 + 10
    assert identity["places"][0]["animal"] == {"number": "15", "name": "DINA 1", "responder": "1000015", "since": T0 + 10}
    assert r.events() == {"events": [{"seq": 1, "time": T0 + 10, "kind": "identified", "place": ADDRESS,
                                      "animal": {"number": "15", "name": "DINA 1", "responder": "1000015"}}], "last": 1}


def test_names_beyond_ascii():
    r = Robot()
    r.hear(cow(1000117, 117, "HERMINA Ü 6  "), T0)
    assert r.read(T0)[1]["places"][0]["animal"]["name"] == "HERMINA Ü 6"


def test_the_same_cow_again_within_five_seconds_is_one_identification():
    r = Robot()
    r.hear(cow(1000015, 15, "DINA 1"), T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 3000)
    assert r.events()["last"] == 1
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 600_000)   # a next visit
    r.hear(cow(1000041, 41, "JANNA 2"), T0 + 600_500)
    ev = r.events(after=1)["events"]
    assert [e["animal"]["number"] for e in ev] == ["15", "41"] and r.read(T0 + 600_600)[1]["places"][0]["animal"]["number"] == "41"


def test_other_messages_only_say_the_robot_is_there():
    r = Robot()
    r.hear(message(0x13B, bytes([10, 12, 9, 11])), T0)                    # quarter milk speed
    r.hear(cow(0, 0, ""), T0 + 1)                                         # no cow
    r.hear(b"\x57\xd1\x00\x00" + b"\0" * 20, T0 + 2)                     # not a message
    r.hear(cow(1000015, 15, "DINA 1")[:20], T0 + 3)                     # cut off
    result, identity = r.read(T0 + 5)
    assert result == OK and identity["updated"] == T0 + 3 and identity["places"][0]["animal"] is None and r.events()["events"] == []
    assert r.read(T0 + 3 + 10_000)[0] == UNREACHABLE                     # silent for 10 s


def test_unknown_driver():
    assert _lib().session_open(b"nope", ADDRESS.encode(), b"", b"") is None


def test_through_python_with_a_robot_on_this_computer():
    robot = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    robot.bind(("127.0.0.1", 0))
    robot.settimeout(5)
    address = "127.0.0.1:%d" % robot.getsockname()[1]
    seen = threading.Event()
    events = []
    with Session("astronaut", address, on_event=lambda s, e: (events.append(e), seen.set())) as s:
        said, client = robot.recvfrom(100)
        assert said == registration(True)
        robot.sendto(cow(1000015, 15, "DINA 1"), client)
        assert seen.wait(5) and events[0]["animal"]["name"] == "DINA 1" and events[0]["place"] == address
        assert s.identity()["places"][0]["animal"]["number"] == "15" and s.latest("identity")[1] == ""
    deadline = time.time() + 5
    while (said := robot.recvfrom(100)[0]) != registration(False):   # the goodbye
        assert time.time() < deadline
    robot.close()



def test_milking_and_visit_end():
    r = Robot()
    r.tick(T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 1000)
    r.hear(start_milking(), T0 + 40000)
    _, identity = r.read(T0 + 41000)
    place = identity["places"][0]
    assert place["visitSince"] == T0 + 40000 and place["animal"]["number"] == "15"
    r.hear(start_milking(), T0 + 41000)          # the same milking (the cups again)
    r.hear(visit_end(1), T0 + 400000)
    r.hear(visit_end(0), T0 + 430000)            # the robot's second word for the same end: not an event
    _, identity = r.read(T0 + 431000)
    assert identity["places"][0]["visitSince"] is None
    kinds = [(e["kind"], e["animal"]["number"], e["time"]) for e in r.events()["events"]]
    assert kinds == [("identified", "15", T0 + 1000), ("visitStarted", "15", T0 + 40000), ("visitEnded", "15", T0 + 400000)]


def test_a_cow_walking_past_does_not_take_the_box():
    """Robot 1 also identifies the cows walking through to robot 2 (a TCS passage): the cow milked
    is the one identified closest to the start of the milking."""
    r = Robot()
    r.tick(T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 1000)       # into the box
    r.hear(cow(1000041, 41, "JANNA 2"), T0 + 30000)     # walks past to robot 2
    r.hear(start_milking(), T0 + 9000)
    _, identity = r.read(T0 + 31000)
    assert identity["places"][0]["animal"]["number"] == "15"   # not the passer-by identified after the start
    r.hear(visit_end(1), T0 + 300000)
    # as on a real robot: the cow milked is identified at the very moment her milking starts, the one
    # walking past to the next robot some seconds before (or after) it
    r.hear(cow(1000041, 41, "JANNA 2"), T0 + 670000)    # walks past
    r.hear(start_milking(), T0 + 690000)
    r.hear(cow(1000077, 77, "RIKA 3"), T0 + 690000)     # in the box, identified with the start
    r.hear(cow(1000042, 42, "BOUKJE 4"), T0 + 720000)   # walks past during the milking
    r.hear(visit_end(1), T0 + 1000000)
    _, identity = r.read(T0 + 1001000)
    events = r.events()["events"]
    started = [e for e in events if e["kind"] == "visitStarted"]
    assert [e["animal"]["number"] for e in started] == ["15", "77"]
    ended = [e["animal"]["number"] for e in events if e["kind"] == "visitEnded"]
    assert ended == ["15", "77"]   # the cow of that milking, not the passer-by identified since


def test_milking_without_an_identification():
    r = Robot()
    r.tick(T0)
    r.hear(start_milking(), T0 + 1000)
    r.hear(visit_end(1), T0 + 300000)
    events = r.events()["events"]
    assert [(e["kind"], e["animal"]) for e in events] == [("visitStarted", {"number": "", "name": "", "responder": ""}),
                                                          ("visitEnded", {"number": "", "name": "", "responder": ""})]



def kinds(r):
    return [(e["kind"], e["animal"]["number"]) for e in r.events()["events"]]


def test_the_cups_go_on_again_during_a_milking():
    r = Robot()
    r.tick(T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 1000)
    r.hear(start_milking(), T0 + 1000)
    r.hear(cow(1000041, 41, "JANNA 2"), T0 + 150000)    # walks past
    r.hear(start_milking(), T0 + 200000)                # a cup was kicked off: on again
    r.hear(visit_end(1), T0 + 500000)
    assert kinds(r) == [("identified", "15"), ("visitStarted", "15"), ("identified", "41"), ("visitEnded", "15")]


def test_a_visit_end_lost_on_the_way():
    r = Robot()
    r.tick(T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 1000)
    r.hear(start_milking(), T0 + 1000)
    r.hear(visit_end(0), T0 + 430000)                   # its 1 never arrived: the 0 ends the milking
    assert r.read(T0 + 431000)[1]["places"][0]["visitSince"] is None
    assert kinds(r) == [("identified", "15"), ("visitStarted", "15"), ("visitEnded", "15")]
    r.hear(cow(1000077, 77, "RIKA 3"), T0 + 600000)
    r.hear(start_milking(), T0 + 600000)
    r.hear(visit_end(1), T0 + 900000)
    r.hear(cow(1000081, 81, "DINA 8"), T0 + 905000)     # the next cow is quick
    r.hear(start_milking(), T0 + 905000)
    r.hear(visit_end(0), T0 + 930000)                   # the 0 of RIKA's end: not the end of DINA's milking
    assert r.read(T0 + 931000)[1]["places"][0]["visitSince"] == T0 + 905000
    # neither word arrives: the next cow's start, with her identification, starts a new visit
    r.hear(cow(1000042, 42, "BOUKJE 4"), T0 + 1500000)
    r.hear(start_milking(), T0 + 1500500)
    r.hear(visit_end(1), T0 + 1800000)
    assert kinds(r)[-3:] == [("identified", "42"), ("visitStarted", "42"), ("visitEnded", "42")]


def test_the_place_has_the_cow_in_the_box_with_her_own_time():
    r = Robot()
    r.tick(T0)
    r.hear(cow(1000015, 15, "DINA 1"), T0 + 1000)
    r.hear(start_milking(), T0 + 1000)
    r.hear(cow(1000041, 41, "JANNA 2"), T0 + 5000)      # walks past
    animal = r.read(T0 + 20000)[1]["places"][0]["animal"]
    assert (animal["number"], animal["since"]) == ("15", T0 + 1000)


def refusal_record(responder):
    return message(0x3F7, bytes(6) + struct.pack("<HI", 18, responder) + bytes(18))   # 32 bytes, as on the robots


def test_a_refused_cow_and_one_walking_through():
    r = Robot()
    r.tick(T0)
    r.hear(cow(1000081, 81, "DINA 8"), T0 + 1000)
    r.hear(refusal_record(1000081), T0 + 9000)        # too early: let out without milking
    r.hear(cow(1000041, 41, "JANNA 2"), T0 + 60000)   # walks through to the next robot: no record
    r.hear(message(0x3F7, bytes(6) + struct.pack("<HI", 9, 1000041) + bytes(18)), T0 + 61000)   # another table: not a refusal
    events = r.events()["events"]
    assert [(e["kind"], e["animal"]["number"], e["animal"]["name"]) for e in events] == [
        ("identified", "81", "DINA 8"), ("refused", "81", "DINA 8"), ("identified", "41", "JANNA 2")]


if __name__ == "__main__":
    run(globals())
