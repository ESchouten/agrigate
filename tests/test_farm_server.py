"""Offline tests of the farm service (python/agrigate/farm_server.py): its HTTP API with
sources made up here, no robots or server needed. Run: python3 tests/test_farm_server.py"""
import json, sys, threading, time, types, urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "python"))
try:
    import bleak  # noqa: the package also holds the Bluetooth robots
except ImportError:
    m = types.ModuleType("bleak"); m.BleakClient = m.BleakScanner = object; sys.modules["bleak"] = m

from agrigate.farm_server import allowed, serve

HERD = [
    {"id": "4", "number": "15", "name": "DINA 1", "lifeNumber": "NL 100000001", "workNumber": "", "responder": "1000015",
     "production": "milking", "reproduction": "pregnant", "lactation": 6, "daysInMilk": 162, "pregnantDays": 67, "expectedCalving": "2027-06-24",
     "inseminations": [{"date": "2026-09-17", "sireName": "GERRIT"}]},
    {"id": "7", "number": "41", "name": "JANNA 2", "lifeNumber": "NL 100000002", "workNumber": "", "responder": "1000041",
     "production": "milking", "reproduction": "inseminated", "lactation": 2, "daysInMilk": 100, "pregnantDays": None, "expectedCalving": ""},
]
ROBOTS = [{"id": "101", "name": "Robot 1", "group": "", "state": "milking", "raw": "3", "since": "", "lastVisit": "", "alarms": [],
           "milkKg": None, "milkings": 162, "failedMilkings": None}]


class FakeSession:
    """A source as the service sees it: a robot that identifies animals, or a server with a herd
    and milking robots."""
    made = []
    server_fails = False

    def __init__(self, driver, address, user="", password="", refresh=0, on_event=None):
        self.driver, self.address, self.on_event, self.seq = driver, address, on_event, 0
        self.contracts = ["identity"] if driver == "astronaut" else ["milk", "cow"]
        self.place = {"id": address, "name": "", "kind": "milkingRobot", "animal": None}
        FakeSession.made.append(self)

    def arrive(self, number, name, responder):
        self.seq += 1
        t = int(time.time() * 1000)
        self.place["animal"] = {"number": number, "name": name, "responder": responder, "since": t}
        self.on_event(self, {"seq": self.seq, "time": t, "kind": "identified", "place": self.address,
                             "animal": {"number": number, "name": name, "responder": responder}})

    def latest(self, contract):
        if contract == "identity":
            return {"updated": 1, "places": [self.place]}, ""
        if FakeSession.server_fails:
            return {"updated": None, "animals": [], "robots": []}, "no answer from the source"
        return {"updated": 2, "animals": HERD, "robots": ROBOTS}, ""

    def close(self):
        pass


def start(server=True, token=""):
    FakeSession.made = []
    config = {"listen": "127.0.0.1:0", "token": token,
              "sources": [{"name": "Robot 1", "driver": "astronaut", "address": "10.4.1.101"}, {"name": "Robot 2", "driver": "astronaut", "address": "10.4.1.102"}]}
    if server:
        config["sources"].append({"driver": "t4c", "address": "10.4.1.1:6969", "user": "", "password": "key"})
    httpd, service = serve(config, session=FakeSession)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d" % httpd.server_port
    return base, service, httpd


def get(base, path, status=200, token=None):
    request = urllib.request.Request(base + path, headers={"Authorization": "Bearer " + token} if token else {})
    try:
        with urllib.request.urlopen(request, timeout=10) as r:
            assert r.status == status
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        assert e.code == status, (e.code, path)
        return json.loads(e.read())


def test_status():
    base, service, httpd = start()
    st = get(base, "/api/status")
    assert st["version"] == 2 and st["lastEvent"] == 0
    assert [(s["name"], s["driver"], s["address"]) for s in st["sources"]] == [("Robot 1", "astronaut", "10.4.1.101"), ("Robot 2", "astronaut", "10.4.1.102"), ("", "t4c", "10.4.1.1:6969")]
    assert st["sources"][0]["contracts"] == {"identity": {"updated": 1, "error": ""}}
    assert st["sources"][2]["contracts"] == {"milk": {"updated": 2, "error": ""}, "cow": {"updated": 2, "error": ""}}
    httpd.shutdown()


def test_places_with_the_herd_added():
    base, service, httpd = start()
    assert [p["animal"] for p in get(base, "/api/identity/places")["places"]] == [None, None]
    FakeSession.made[1].arrive("15", "DINA 1", "1000015")
    place = get(base, "/api/identity/places")["places"][1]
    assert (place["name"], place["id"], place["kind"], place["driver"], place["connected"]) == ("Robot 2", "10.4.1.102", "milkingRobot", "astronaut", True)
    a = place["animal"]
    assert (a["number"], a["name"], a["responder"], a["lifeNumber"], a["reproduction"], a["daysInMilk"], a["expectedCalving"]) == (
        "15", "DINA 1", "1000015", "NL 100000001", "pregnant", 162, "2027-06-24")
    httpd.shutdown()


def test_animals_and_where_they_are():
    base, service, httpd = start()
    FakeSession.made[0].arrive("41", "JANNA 2", "1000041")
    animals = get(base, "/api/cow/animals")["animals"]
    assert [a["number"] for a in animals] == ["15", "41"]
    assert animals[0]["at"] is None and animals[1]["at"]["place"] == "Robot 1" and animals[1]["at"]["since"]
    for key in ("15", "1000015", "NL%20100000001"):
        assert get(base, "/api/cow/animals/" + key)["name"] == "DINA 1"
    assert get(base, "/api/cow/animals/999", 404)["error"]
    httpd.shutdown()


def test_milking_robots():
    base, service, httpd = start()
    robot, = get(base, "/api/milk/robots")["robots"]
    assert (robot["name"], robot["state"], robot["milkings"], robot["driver"], robot["updated"]) == ("Robot 1", "milking", 162, "t4c", 2)
    httpd.shutdown()


def test_events_of_all_sources_in_one_row():
    base, service, httpd = start()
    FakeSession.made[0].arrive("15", "DINA 1", "1000015")
    FakeSession.made[1].arrive("41", "JANNA 2", "1000041")
    ev = get(base, "/api/identity/events?after=0")
    assert [(e["seq"], e["kind"], e["place"]["name"], e["animal"]["number"], e["animal"]["lifeNumber"]) for e in ev["events"]] == [
        (1, "identified", "Robot 1", "15", "NL 100000001"), (2, "identified", "Robot 2", "41", "NL 100000002")]
    assert ev["events"][0]["place"] == {"id": "10.4.1.101", "name": "Robot 1", "kind": "milkingRobot"}
    assert ev["last"] == 2 and get(base, "/api/identity/events?after=2") == {"events": [], "last": 2}
    httpd.shutdown()


def test_waiting_for_the_next_cow():
    base, service, httpd = start()
    threading.Timer(0.5, lambda: FakeSession.made[0].arrive("15", "DINA 1", "1000015")).start()
    t = time.time()
    ev = get(base, "/api/identity/events?after=0&wait=20")
    assert ev["last"] == 1 and ev["events"][0]["animal"]["number"] == "15" and time.time() - t < 5
    t = time.time()
    assert get(base, "/api/identity/events?after=1&wait=1")["events"] == [] and time.time() - t >= 0.9
    httpd.shutdown()


def test_a_cow_the_herd_does_not_know():
    base, service, httpd = start()
    FakeSession.made[0].arrive("999", "NIEUW", "1234")
    a = get(base, "/api/identity/places")["places"][0]["animal"]
    assert (a["number"], a["name"], a["lifeNumber"], a["reproduction"]) == ("999", "NIEUW", None, None)
    httpd.shutdown()


def test_without_a_herd_only_identity():
    base, service, httpd = start(server=False)
    FakeSession.made[0].arrive("15", "DINA 1", "1000015")
    assert get(base, "/api/cow/animals") == {"animals": []} and get(base, "/api/milk/robots") == {"robots": []}
    a = get(base, "/api/identity/places")["places"][0]["animal"]
    assert a["name"] == "DINA 1" and a["lifeNumber"] is None
    httpd.shutdown()


def test_a_source_not_answering_keeps_the_others():
    FakeSession.server_fails = True
    try:
        base, service, httpd = start()
        assert get(base, "/api/status")["sources"][2]["contracts"]["cow"] == {"updated": None, "error": "no answer from the source"}
        FakeSession.made[0].arrive("15", "DINA 1", "1000015")
        assert get(base, "/api/identity/events")["events"][0]["animal"]["name"] == "DINA 1"
        httpd.shutdown()
    finally:
        FakeSession.server_fails = False


def test_with_a_token_every_request_carries_it():
    base, service, httpd = start(token="geheim")
    assert len(get(base, "/api/status", token="geheim")["sources"]) == 3
    for wrong in (None, "fout", "geheim2"):
        assert get(base, "/api/cow/animals", 401, token=wrong) == {"error": "Authorization: Bearer <token> needed"}
    httpd.shutdown()


def test_without_a_token_only_this_computer_is_answered():
    base, service, httpd = start()
    assert get(base, "/api/status")["version"] == 2   # the test is this computer
    assert not allowed("", "", "192.168.1.20") and not allowed("", "Bearer x", "10.4.1.50")
    httpd.shutdown()


def test_unknown_path():
    base, service, httpd = start()
    assert get(base, "/api/nope", 404)["error"] == "unknown path"
    httpd.shutdown()


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn(); print("ok", name)
