"""Offline tests of the farm service (python/scraper_control/farm_server.py): its HTTP API with
robots and a herd made up here, no robots or server needed. Run: python3 tests/test_farm_server.py"""
import json, sys, threading, time, types, urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "python"))
try:
    import bleak  # noqa: the package also holds the Bluetooth robots
except ImportError:
    m = types.ModuleType("bleak"); m.BleakClient = m.BleakScanner = object; sys.modules["bleak"] = m

from scraper_control.farm import FarmError
from scraper_control.farm_server import allowed, serve

HERD = [
    {"id": "4", "number": "15", "name": "DINA 1", "lifeNumber": "NL 100000001", "workNumber": "", "responder": "1000015",
     "production": "milking", "reproduction": "pregnant", "lactation": 6, "daysInMilk": 162, "pregnantDays": 67, "expectedCalving": "2027-06-24",
     "inseminations": [{"date": "2026-09-17", "sireName": "GERRIT"}]},
    {"id": "7", "number": "41", "name": "JANNA 2", "lifeNumber": "NL 100000002", "workNumber": "", "responder": "1000041",
     "production": "milking", "reproduction": "inseminated", "lactation": 2, "daysInMilk": 100, "pregnantDays": None, "expectedCalving": ""},
]


class FakeListener:
    made = []

    def __init__(self, driver, address, on_event=None):
        self.driver, self.address, self.on_event, self.animal, self.seq = driver, address, on_event, None, 0
        FakeListener.made.append(self)

    def arrive(self, number, name, responder):
        self.seq += 1
        t = int(time.time() * 1000)
        self.animal = {"number": number, "name": name, "responder": responder, "since": t}
        self.on_event(self, {"seq": self.seq, "time": t, "kind": "arrival", "number": number, "name": name, "responder": responder})

    def state(self):
        return {"address": self.address, "connected": True, "lastMessage": 1, "animal": self.animal}

    def close(self):
        pass


class FakeFarm:
    fail = False

    def __init__(self, driver, address, user, password):
        self.password = password

    def __enter__(self):
        return self

    def __exit__(self, *a):
        pass

    def herd(self):
        if FakeFarm.fail:
            raise FarmError(1, "")
        return {"animals": HERD}


def start(server=True, token=""):
    FakeListener.made = []
    config = {"listen": "127.0.0.1:0", "token": token,
              "robots": [{"name": "Robot 1", "driver": "lfw", "address": "10.4.1.101"}, {"name": "Robot 2", "driver": "lfw", "address": "10.4.1.102"}]}
    if server:
        config["server"] = {"driver": "t4c", "address": "10.4.1.1:6969", "user": "", "password": "key"}
    httpd, service = serve(config, listener=FakeListener, farm=FakeFarm)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    if server:
        service.refresh_herd()
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
    assert (st["robots"], st["robotsConnected"], st["herd"]["animals"], st["herd"]["error"], st["lastEvent"]) == (2, 2, 2, "", 0)
    httpd.shutdown()


def test_robots_with_the_herd_added():
    base, service, httpd = start()
    assert [r["animal"] for r in get(base, "/api/robots")["robots"]] == [None, None]
    FakeListener.made[1].arrive("15", "DINA 1", "1000015")
    robot = get(base, "/api/robots")["robots"][1]
    assert (robot["name"], robot["address"], robot["driver"]) == ("Robot 2", "10.4.1.102", "lfw")
    a = robot["animal"]
    assert (a["number"], a["name"], a["responder"], a["lifeNumber"], a["reproduction"], a["daysInMilk"], a["expectedCalving"]) == (
        "15", "DINA 1", "1000015", "NL 100000001", "pregnant", 162, "2027-06-24")
    httpd.shutdown()


def test_animals_and_where_they_are():
    base, service, httpd = start()
    FakeListener.made[0].arrive("41", "JANNA 2", "1000041")
    animals = get(base, "/api/animals")
    assert [a["number"] for a in animals["animals"]] == ["15", "41"] and animals["updated"]
    assert animals["animals"][0]["atRobot"] is None and animals["animals"][1]["atRobot"]["robot"] == "Robot 1"
    for key in ("15", "1000015", "NL%20100000001"):
        assert get(base, "/api/animals/" + key)["name"] == "DINA 1"
    assert get(base, "/api/animals/999", 404)["error"]
    httpd.shutdown()


def test_events_of_all_robots_in_one_row():
    base, service, httpd = start()
    FakeListener.made[0].arrive("15", "DINA 1", "1000015")
    FakeListener.made[1].arrive("41", "JANNA 2", "1000041")
    ev = get(base, "/api/events?after=0")
    assert [(e["seq"], e["robot"], e["number"], e["lifeNumber"]) for e in ev["events"]] == [(1, "Robot 1", "15", "NL 100000001"), (2, "Robot 2", "41", "NL 100000002")]
    assert ev["last"] == 2 and get(base, "/api/events?after=2") == {"events": [], "last": 2}
    httpd.shutdown()


def test_waiting_for_the_next_cow():
    base, service, httpd = start()
    threading.Timer(0.5, lambda: FakeListener.made[0].arrive("15", "DINA 1", "1000015")).start()
    t = time.time()
    ev = get(base, "/api/events?after=0&wait=20")
    assert ev["last"] == 1 and ev["events"][0]["number"] == "15" and time.time() - t < 5
    t = time.time()
    assert get(base, "/api/events?after=1&wait=1")["events"] == [] and time.time() - t >= 0.9
    httpd.shutdown()


def test_a_cow_the_herd_does_not_know():
    base, service, httpd = start()
    FakeListener.made[0].arrive("999", "NIEUW", "1234")
    a = get(base, "/api/robots")["robots"][0]["animal"]
    assert (a["number"], a["name"], a["lifeNumber"], a["reproduction"]) == ("999", "NIEUW", None, None)
    httpd.shutdown()


def test_without_a_server_only_live():
    base, service, httpd = start(server=False)
    FakeListener.made[0].arrive("15", "DINA 1", "1000015")
    assert get(base, "/api/status")["herd"] is None
    assert get(base, "/api/robots")["robots"][0]["animal"]["name"] == "DINA 1"
    httpd.shutdown()


def test_the_server_not_answering_keeps_the_live_part():
    FakeFarm.fail = True
    try:
        base, service, httpd = start()
        assert get(base, "/api/status")["herd"]["error"] == "no answer from the server"
        FakeListener.made[0].arrive("15", "DINA 1", "1000015")
        assert get(base, "/api/events")["events"][0]["name"] == "DINA 1"
        httpd.shutdown()
    finally:
        FakeFarm.fail = False


def test_with_a_token_every_request_carries_it():
    base, service, httpd = start(token="geheim")
    assert get(base, "/api/status", token="geheim")["robots"] == 2
    for wrong in (None, "fout", "geheim2"):
        assert get(base, "/api/animals", 401, token=wrong) == {"error": "Authorization: Bearer <token> needed"}
    httpd.shutdown()


def test_without_a_token_only_this_computer_is_answered():
    base, service, httpd = start()
    assert get(base, "/api/status")["robots"] == 2   # the test is this computer
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
