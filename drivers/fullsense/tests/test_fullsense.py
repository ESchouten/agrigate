"""Offline tests: the FULLSENSE driver through the Python API, against a server that answers as
the FULLSENSE API description says. Run: python3 drivers/fullsense/tests/test_fullsense.py"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from farm_test import FakeFarm, Farm, FarmError, drivers, run

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


def read(farm, password="geheim"):
    with Farm("fullsense", farm.address, "erik", password) as f:
        return f.milk()["robots"]


def test_driver_is_listed():
    d = next(d for d in drivers() if d["id"] == "fullsense")
    assert d["kinds"] == ["milk"] and d["link"]["port"] == 12080


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
    farm = server()
    read(farm)
    assert [k for k, _ in farm.asked if not k.startswith("GET ")] == [LOGIN]
    assert not [k for k, _ in farm.asked if "/device/" in k]


def test_wrong_password():
    try:
        read(server(), password="fout")
    except FarmError as e:
        assert e.result == 2
    else:
        assert False


def test_a_new_login_when_it_ran_out():
    farm = server()
    with Farm("fullsense", farm.address, "erik", "geheim") as f:
        f.milk()
        farm.token = "login-2"
        assert len(f.milk()["robots"]) == 3 and farm.count(LOGIN) == 2


def test_a_missing_status_is_left_out():
    farm = server()
    del farm.routes["GET /api/MerlinStatus/GetMerlinStatus?merlinStatusId=s-1"]
    with Farm("fullsense", farm.address, "erik", "geheim") as f:
        robots = f.milk()["robots"]
        assert robots[0]["state"] == "unknown" and robots[1]["state"] == "assistance" and "HTTP 404" in f.notes


if __name__ == "__main__":
    run(globals())
