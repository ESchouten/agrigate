"""Offline tests: the FULLSENSE driver through the Python API, against a server that answers as
the FULLSENSE API description says. Run: python3 drivers/fullsense/tests/test_fullsense.py"""
import math, os, sys, time
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from farm_test import FakeFarm, Session, SessionError, drivers, run

LOGIN = "POST /api/Authentication/Authenticate"
NONE = "00000000-0000-0000-0000-000000000000"


def location(n, status=NONE, alarm=NONE, group="Groep 1"):
    return {"id": f"0000000{n}-aaaa-bbbb-cccc-000000000000", "locationType": 1, "description": f"Robot n\u00b0 {n} \U0001F404",
            "locationIndex": n, "locationGroup": {"id": "g1", "name": group} if group else None,
            "currentStatusRecordingId": status, "currentMerlinAlarmId": alarm, "currentMilkRecordingId": None}


def status(display, start="2026-11-22T15:30:00+01:00"):
    return {"id": "s", "locationId": "l", "start": start, "end": None, "operationStatus": 1, "status": 7, "displayStatus": display}


def server() -> FakeFarm:
    farm = FakeFarm(LOGIN)
    farm.routes = {
        LOGIN: lambda body: {"token": farm.token, "refreshToken": "r", "expiration": "2026-11-22T17:00:00Z", "isApplication": False}
        if body == {"userName": "erik", "password": "geheim"} else (401, ""),
        "GET /api/Location/GetLocations?locationType=1": [
            location(1, status="s-1"), location(2, status="s-2", alarm="a-2"), location(3, group=None)],
        "GET /api/MerlinStatus/GetMerlinStatus?merlinStatusId=s-1": status(4),
        "GET /api/MerlinStatus/GetMerlinStatus?merlinStatusId=s-2": status(5, start="2026-11-22T15:12:00+01:00"),
        "GET /api/MerlinAlarm/GetMerlinAlarm?merlinAlarmId=a-2": {
            "timestamp": "2026-11-22T15:12:00+01:00", "locationId": "l", "locationDescription": "Robot 2",
            "alarmClass": 4, "alarmCode": 16, "animalNumber": 412},
    }
    return farm


FARM = "/ICAR/locations/nl.ubn/1234567/"
DAY = 86400


def iso(t: float) -> str:
    return datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def days_ago(n: int) -> str:   # a day's date, as the server gives it at 08:00 UTC
    return (datetime.now(timezone.utc) - timedelta(days=n)).strftime("%Y-%m-%dT08:00:00Z")


def cow(guid, number, name, life, lactation="Lactating", reproduction="Pregnant", status="Alive", born="2020-03-01T00:00:00"):
    return {"meta": {"modified": "2026-11-20T10:00:00Z"}, "identifier": {"id": guid, "scheme": "com.fullsense"},
            "alternativeIdentifiers": [{"id": life, "scheme": "eu.bovine"}] if life else [], "specie": "Cattle", "gender": "Female",
            "birthDate": born, "managementTag": number, "name": name, "status": status, "healthStatus": "Healthy",
            "lactationStatus": lactation, "reproductionStatus": reproduction}


def event(guid, when, **more):
    return {"meta": {}, "animal": {"id": guid, "scheme": "com.fullsense"}, "eventDateTime": when, **more}


def members(*items):
    return {"view": {"totalItems": len(items), "totalPages": 1, "pageSize": 100, "currentPage": 1}, "member": list(items)}


def visit(box, start, kg, complete=True, unit="KGM"):
    return {"meta": {}, "animal": {"id": "c-1", "scheme": "com.fullsense"}, "milkingStartingDateTime": start,
            "milkingDuration": {"unitCode": "MIN", "value": 7}, "milkingMilkWeight": {"unitCode": unit, "value": kg},
            "milkingComplete": complete, "milkingBoxNumber": box}


def with_icar(farm: FakeFarm) -> FakeFarm:
    farm.routes.update({
        "GET /ICAR/locations": members({"meta": {}, "identifier": {"id": "1234567", "scheme": "nl.ubn"}, "name": "Boerderij", "timeZoneId": "Europe/Amsterdam"}),
        "GET " + FARM + "animals": members(
            cow("c-1", "15", "DINA", "NL 100000001"),
            cow("c-2", "41", "JANNA", "NL 100000002", reproduction="Inseminated"),
            cow("c-3", "3055", "GLORIA", "", lactation="Dry", reproduction="Open", born="2025-05-18T00:00:00"),
            cow("c-4", "99", "WEG", "NL 100000004", status="OffFarm")),
        "GET " + FARM + "parturitions": members(event("c-1", days_ago(400)), event("c-1", days_ago(160)), event("c-9", days_ago(3))),
        "GET " + FARM + "inseminations": members(event("c-1", days_ago(90), id="i1"), event("c-1", days_ago(67), id="i2"), event("c-2", days_ago(10), id="i3")),
        "GET " + FARM + "pregnancy-checks": members(event("c-1", days_ago(30), result="Multiple"), event("c-2", days_ago(200), result="Empty")),
        "GET " + FARM + "heats": members(event("c-1", days_ago(88), heatDetectionMethod="Activity"), event("c-2", days_ago(10), heatDetectionMethod="Visual")),
        "GET " + FARM + "drying-offs": members(event("c-1", days_ago(220), id="l1")),
        "GET " + FARM + "do-not-breeds": members(event("c-2", days_ago(300), doNotBreed=False)),
        "GET " + FARM + "lactations": members(
            {"meta": {}, "id": "l1", "animal": {"id": "c-1", "scheme": "com.fullsense"}, "beginDate": days_ago(400), "endDate": days_ago(220), "parity": 5, "lactationLength": 180},
            {"meta": {}, "id": "l2", "animal": {"id": "c-1", "scheme": "com.fullsense"}, "beginDate": days_ago(160), "parity": 6, "lactationLength": 160},
            {"meta": {}, "id": "l3", "animal": {"id": "c-2", "scheme": "com.fullsense"}, "beginDate": days_ago(100), "parity": 2, "lactationLength": 100}),
    })
    return farm


def read(farm, password="geheim"):
    with Session("fullsense", farm.address, "erik", password) as f:
        return f.milk()["robots"]


def test_driver_is_listed():
    d = next(d for d in drivers() if d["id"] == "fullsense")
    assert d["contracts"] == ["milk", "cow", "identity"] and d["links"][0]["port"] == 12080


def test_robots_states_and_alarm():
    one, two, three = read(server())
    assert (one["name"], one["group"], one["state"], one["raw"], one["since"]) == \
        ("Robot n\u00b0 1 \U0001F404", "Groep 1", "milking", "Milking", "2026-11-22T15:30:00+01:00")
    assert one["alarms"] == [] and one["milkings"] is None
    assert (two["state"], two["raw"]) == ("assistance", "WaitingForAssistance")
    assert two["alarms"] == [{"time": "2026-11-22T15:12:00+01:00", "severity": "warning", "code": "ArmAssistRequest", "text": "Arm assist request"}]
    assert (three["group"], three["state"], three["raw"], three["since"]) == ("", "unknown", "", "")   # no status recording


def test_every_state_of_the_server_has_a_word():
    names = ["Emergency", "Alarm", "WaitingForCow", "Milking", "WaitingForAssistance", "Cleaning", "Paused", "Offline", "Idle", "Releasing"]
    states = ["emergency", "alarm", "idle", "milking", "assistance", "cleaning", "paused", "offline", "idle", "milking"]
    for number, (name, state) in enumerate(zip(names, states), 1):
        for given in (number, name):   # a server may give the number or the name
            farm = server()
            farm.routes["GET /api/MerlinStatus/GetMerlinStatus?merlinStatusId=s-1"] = status(given)
            assert (read(farm)[0]["state"], read(farm)[0]["raw"]) == (state, name)
    farm = server()
    farm.routes["GET /api/MerlinStatus/GetMerlinStatus?merlinStatusId=s-1"] = status(42)
    assert read(farm)[0]["state"] == "unknown"


def test_alarm_codes_and_no_alarm():
    farm = server()
    alarm = farm.routes["GET /api/MerlinAlarm/GetMerlinAlarm?merlinAlarmId=a-2"]
    for code, cls, expect in ((113, 3, ("error", "MerlinNotCleaning")), (99, 0, ("info", "MilkpumpSpeedLow")), (98, 1, ("warning", "98")),
                              ("VacuumPump", "Warning", ("warning", "VacuumPump")), (500, 5, ("unknown", "500"))):
        alarm.update(alarmCode=code, alarmClass=cls)
        got = read(farm)[1]["alarms"][0]
        assert (got["severity"], got["code"]) == expect
    alarm.update(alarmCode=0)
    assert read(farm)[1]["alarms"] == []


def test_only_reads_and_never_the_robots_own_calls():
    farm = with_icar(server())
    with Session("fullsense", farm.address, "erik", "geheim") as f:
        f.milk(), f.cow(), f.identity()
    assert [k for k, _ in farm.asked if not k.startswith("GET ")] == [LOGIN]
    assert not [k for k, _ in farm.asked if "/device/" in k]


def test_wrong_password():
    try:
        read(server(), password="fout")
    except SessionError as e:
        assert e.result == 2
    else:
        assert False


def test_a_new_login_when_it_ran_out():
    farm = server()
    with Session("fullsense", farm.address, "erik", "geheim") as f:
        f.milk()
        farm.token = "login-2"
        assert len(f.milk()["robots"]) == 3 and farm.count(LOGIN) == 2


def test_a_missing_status_is_left_out():
    farm = server()
    del farm.routes["GET /api/MerlinStatus/GetMerlinStatus?merlinStatusId=s-1"]
    with Session("fullsense", farm.address, "erik", "geheim") as f:
        robots = f.milk()["robots"]
        assert robots[0]["state"] == "unknown" and robots[1]["state"] == "assistance" and "HTTP 404" in f.notes


def test_the_figures_of_the_last_24_hours():
    farm, now = with_icar(server()), time.time()
    farm.routes["GET " + FARM + "milking-visits?pageSize=100&currentPage=1&*"] = {
        "view": {"totalItems": 6, "totalPages": 2, "pageSize": 3, "currentPage": 1},
        "member": [visit("1", iso(now - 3600), 10.5), visit("1", iso(now - 600), 0.4, complete=False), visit("2", iso(now - 7200), 12000, unit="GRM")]}
    farm.routes["GET " + FARM + "milking-visits?pageSize=100&currentPage=2&*"] = {
        "view": {"totalItems": 6, "totalPages": 2, "pageSize": 3, "currentPage": 2},
        "member": [visit("1", iso(now - 2 * DAY), 9.0),   # changed lately, milked two days ago
                   visit("9", iso(now - 60), 9.0),          # a box the server did not list
                   visit("01", iso(now - 300), 8.25)]}
    one, two, three = read(farm)
    assert (one["milkKg"], one["milkings"], one["failedMilkings"], one["lastVisit"]) == (19.15, 2, 1, iso(now - 300))
    assert (two["milkKg"], two["milkings"], two["failedMilkings"]) == (12.0, 1, 0)
    assert (three["milkKg"], three["milkings"], three["failedMilkings"], three["lastVisit"]) == (0, 0, 0, "")
    asked = [k for k, _ in farm.asked if "milking-visits" in k][0]
    since = datetime.strptime(asked.split("meta-modified-from=")[1], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc).timestamp()
    assert abs(since - (now - DAY)) < 5


def test_the_herd_from_icar():
    farm = with_icar(server())
    with Session("fullsense", farm.address, "erik", "geheim") as f:
        herd = f.cow()["animals"]
    assert [a["number"] for a in herd] == ["15", "41", "3055"]   # not the one that left the farm
    dina, janna, gloria = herd
    assert (dina["id"], dina["name"], dina["lifeNumber"], dina["birthDate"]) == ("c-1", "DINA", "NL 100000001", "2020-03-01")
    assert (dina["production"], dina["reproduction"], dina["reproductionRaw"], dina["lactation"]) == ("milking", "pregnant", "Pregnant", 6)
    assert (dina["calvingDate"], dina["dryOffDate"]) == (days_ago(160)[:10], days_ago(220)[:10])
    assert (dina["daysInMilk"], dina["pregnantDays"], dina["daysSinceInsemination"], dina["daysSinceHeat"], dina["inseminationsThisLactation"]) == (160, 67, 67, 88, 2)
    assert [i["date"] for i in dina["inseminations"]] == [days_ago(90)[:10], days_ago(67)[:10]]
    assert dina["pregnancyChecks"] == [{"date": days_ago(30)[:10], "result": "pregnant"}]
    assert dina["heats"] == [{"date": days_ago(88)[:10], "method": "activity"}]
    assert (janna["reproduction"], janna["doNotBreedDate"], janna["lactation"], janna["daysInMilk"]) == ("inseminated", "", 2, 100)
    assert (gloria["production"], gloria["lactation"], gloria["lifeNumber"], gloria["daysInMilk"]) == ("young", 0, "", None)


def test_no_icar_for_this_user():
    farm = server()   # the server lists no farm in ICAR for this login
    with Session("fullsense", farm.address, "erik", "geheim") as f:
        try:
            f.cow()
        except SessionError as e:
            assert e.result == 3 and "ICAR" in str(e)
        else:
            assert False
        assert f.milk()["robots"][0]["state"] == "milking"   # the robots still are


def recording(number, name, tag, start, decision=0, finished=False):
    return {"id": "m", "animalId": "c", "locationId": "l", "milkingStartTime": iso(start), "isFinished": finished, "merlinDecision": decision,
            "destination": 0, "endOfMilkingStatus": 3, "animal": {"id": "c", "animalNumber": number, "name": name, "tags": [{"id": "t", "tagType": 2, "tagNumber": tag}]}}


def test_which_cow_is_in_a_robot():
    farm, now = server(), time.time()
    robots = farm.routes["GET /api/Location/GetLocations?locationType=1"]
    robots[0]["currentMilkRecordingId"] = "m-1"
    farm.routes["GET /api/Milking/GetMilkRecording?id=m-1"] = recording(15, "DINA", 1000015, now - 60)
    with Session("fullsense", farm.address, "erik", "geheim") as f:
        places = f.identity()["places"]
        assert [(p["name"], p["kind"]) for p in places] == [(f"Robot n° {n} 🐄", "milkingRobot") for n in (1, 2, 3)]
        start = int(now - 60) * 1000
        assert places[0]["animal"] == {"number": "15", "name": "DINA", "responder": "1000015", "since": start} and places[0]["visitSince"] == start
        assert places[1]["animal"] is None
        kinds = lambda: [(e["kind"], e["animal"]["number"]) for e in f.events(0)["events"]]
        assert kinds() == [("identified", "15"), ("visitStarted", "15")] and f.events(0)["events"][1]["time"] == start
        f.identity()   # nothing new
        assert len(kinds()) == 2
        farm.routes["GET /api/Milking/GetMilkRecording?id=m-1"] = recording(15, "DINA", 1000015, now - 60, finished=True)
        f.identity()
        assert kinds()[2:] == [("visitEnded", "15")] and f.identity()["places"][0]["visitSince"] is None
        robots[0]["currentMilkRecordingId"] = "m-2"   # the next cow, sent away: not her time yet
        farm.routes["GET /api/Milking/GetMilkRecording?id=m-2"] = recording(41, "JANNA", 1000041, math.ceil(time.time()) + 1, decision="NotAllowed")
        f.identity()
        robots[0]["currentMilkRecordingId"] = None
        f.identity()
        assert kinds()[3:] == [("identified", "41"), ("refused", "41")]
        assert f.identity()["places"][0]["animal"]["number"] == "41"


def test_a_milking_done_between_two_reads():
    farm, now = server(), time.time()
    farm.routes["GET /api/Location/GetLocations?locationType=1"][1]["currentMilkRecordingId"] = "m-3"
    farm.routes["GET /api/Milking/GetMilkRecording?id=m-3"] = recording(7, "ROOS", 1000007, now - 600, finished=True)
    with Session("fullsense", farm.address, "erik", "geheim") as f:
        f.identity()
        f.identity()
        assert [(e["kind"], e["place"][:8]) for e in f.events(0)["events"]] == [("identified", "00000002"), ("visitStarted", "00000002"), ("visitEnded", "00000002")]


if __name__ == "__main__":
    run(globals())
