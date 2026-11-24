"""Offline tests: the T4C driver through the Python API, against a server that answers as the
T4C API description says. Run: python3 drivers/t4c/tests/test_t4c.py"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from farm_test import FakeFarm, Session, SessionError, closed_address, drivers, run

LOGIN = "POST /t4capi/login/secure"


def answer(data):
    return {"data": data, "serverEror": None, "executionTime": 3, "apiVersion": "v1"}


def server() -> FakeFarm:
    farm = FakeFarm(LOGIN)

    def login(body):
        if body != {"userName": "erik", "password": "geheim"}:
            return {"data": None, "serverEror": {"code": 401, "message": "Invalid user name or password"}}
        return answer({"userId": 7, "defaultHerdId": 1, "languageId": 2, "languageCode": "nl", "countryId": 31, "cultureId": 19,
                       "farmName": "De Hoeve", "accessToken": {"token": farm.token, "expiresIn": 3600.0}, "refreshToken": "r"})

    farm.routes = {
        LOGIN: login,
        "GET /t4capi/astronaut/overviews": answer([
            {"id": 101, "name": "Robot 1", "barnName": "Stal één", "lastProcessTime": "2026-11-22T15:01:10", "visitResult": 1, "aniUserNumber": 412, "robotStatus": 3},
            {"id": 102, "name": "Robot 2", "barnName": None, "lastProcessTime": "2026-11-22T14:58:00", "visitResult": 2, "aniUserNumber": 88, "robotStatus": 1}]),
        "GET /t4capi/alarms/astronaut/101": answer([
            {"devId": "101", "devName": "Robot 1", "devAddress": "1", "indicationId": "5012", "type": "2",
             "creationDate": "2026-11-22T14:40:00", "description": "Melkbeker \"LV\" niet aangesloten"}]),
        "GET /t4capi/alarms/astronaut/102": answer([]),
        "GET /t4capi/astronaut/101/detail": answer({
            "id": "101", "astronautKpis": [{"totalMilkProduced": 1834.5, "separeted": 12.0, "numberOfMilking": 162, "freeTime": 11.5}],
            "failedMilkingKpi": {"totalFailedMilkingCount": 4, "isAttention": False, "cowCount": 3}}),
        "GET /t4capi/astronaut/102/detail": answer({"id": "102", "astronautKpis": [], "failedMilkingKpi": None}),
    }
    return farm


def read(farm, password="geheim"):
    with Session("t4c", farm.address, "erik", password) as f:
        return f.milk()["robots"]


def test_driver_is_listed():
    d = next(d for d in drivers() if d["id"] == "t4c")
    assert d["contracts"] == ["milk", "cow"] and d["links"] == [{"kind": "http", "scheme": "http", "port": 6969}]


def test_robots_alarms_and_figures():
    one, two = read(server())
    assert (one["id"], one["name"], one["group"], one["lastVisit"]) == ("101", "Robot 1", "Stal één", "2026-11-22T15:01:10")
    assert one["alarms"] == [{"time": "2026-11-22T14:40:00", "severity": "unknown", "code": "5012", "text": "Melkbeker \"LV\" niet aangesloten"}]
    assert (one["milkKg"], one["milkings"], one["failedMilkings"]) == (1834.5, 162, 4)
    assert (two["group"], two["alarms"], two["milkKg"], two["milkings"], two["failedMilkings"]) == ("", [], None, None, None)


def test_a_read_says_when_it_was():
    import time
    with Session("t4c", server().address, "erik", "geheim") as f:
        assert f.latest("milk")[0] == {"updated": None, "robots": []}
        assert abs(f.milk()["updated"] - time.time() * 1000) < 60_000


def test_state_is_unknown_with_the_servers_number():
    # what the server's status numbers mean is not known yet
    assert [(r["state"], r["raw"]) for r in read(server())] == [("unknown", "3"), ("unknown", "1")]


def test_only_reads():
    farm = server()
    read(farm)
    assert [k for k, _ in farm.asked if not k.startswith("GET ")] == [LOGIN]


def test_requests_carry_the_login():
    farm = server()
    read(farm)
    headers = {k.lower(): v for k, v in farm.asked[-1][1].items()}
    assert headers["authorization"] == "Bearer login-1" and (headers["userid"], headers["herdid"], headers["languagecode"]) == ("7", "1", "nl")


def test_wrong_password():
    try:
        read(server(), password="fout")
    except SessionError as e:
        assert e.result == 2 and "Invalid user name" in str(e)
    else:
        assert False


def test_one_login_for_many_reads_and_a_new_one_when_it_ran_out():
    farm = server()
    with Session("t4c", farm.address, "erik", "geheim") as f:
        f.milk(); f.milk()
        assert farm.count(LOGIN) == 1
        farm.token = "login-2"   # the server forgot the first
        assert len(f.milk()["robots"]) == 2 and farm.count(LOGIN) == 2


def test_a_login_the_server_does_not_take_ends_the_read():
    farm = server()
    login = farm.routes[LOGIN]

    def not_taken(body):
        answer = login(body)
        answer["data"]["accessToken"]["token"] = "not-taken"
        return answer

    farm.routes[LOGIN] = not_taken
    try:
        read(farm)
    except SessionError as e:
        assert e.result == 2 and farm.count(LOGIN) == 2   # one new login, then it gives up: rejected
    else:
        assert False


def test_no_server():
    try:
        with Session("t4c", closed_address(), "erik", "geheim", timeout=2) as f:
            f.milk()
    except SessionError as e:
        assert e.result == 1
    else:
        assert False


def test_a_missing_detail_is_left_out():
    farm = server()
    farm.routes["GET /t4capi/astronaut/101/detail"] = (500, "boom")
    with Session("t4c", farm.address, "erik", "geheim") as f:
        one = f.milk()["robots"][0]
        assert one["milkings"] is None and len(one["alarms"]) == 1 and "astronaut/101/detail: HTTP 500" in f.notes


def test_a_recording_leaves_the_login_out():
    farm, recording = server(), []
    with Session("t4c", farm.address, "erik", "geheim", recording=recording) as f:
        f.milk()
    assert len(recording) == 6 and recording[1]["answer"]["data"][0]["name"] == "Robot 1"
    assert "login-1" not in str(recording) and "geheim" not in str(recording)


def test_no_robot_list_fails():
    for wrong in ((500, ""), (200, "<html>login</html>")):
        farm = server()
        farm.routes["GET /t4capi/astronaut/overviews"] = wrong
        try:
            read(farm)
        except SessionError as e:
            assert e.result == 3
        else:
            assert False


if __name__ == "__main__":
    run(globals())
