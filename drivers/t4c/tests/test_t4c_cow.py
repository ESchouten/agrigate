"""Offline tests: the T4C driver with the farm's licence key, as a real T4C server answers
(checked on T4C 4.22: field names in PascalCase, "ServerEror", ICAR lists in integrationglobal).
The animals here are made up. Run: python3 drivers/t4c/tests/test_t4c_cow.py"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from farm_test import FakeFarm, Farm, FarmError, drivers, run

KEY = "AAAAA-BBBBB-CCCCC-DDDDD-EEEEE"


def answer(data):
    return {"Data": data, "ServerEror": None, "ExecutionTime": 2, "ApiVersion": "v1"}


def icar(life):
    return {"Id": life, "Scheme": "eu.bovine"}


def server() -> FakeFarm:
    farm = FakeFarm("none", key=KEY)
    farm.routes = {
        "GET /t4capi/astronaut/overviews": answer([
            {"Id": 101, "Name": "Melkrobot 1", "BarnName": "Melkveestal", "LastProcessTime": "2026-11-23T13:24:59", "VisitResult": 2, "AniUserNumber": 119, "RobotStatus": 0}]),
        "GET /t4capi/alarms/astronaut/101": answer([]),
        "GET /t4capi/astronaut/101/detail": answer({"Id": "101", "AstronautKpis": [{"TotalMilkProduced": 790.0, "NumberOfMilking": 103}],
                                                    "FailedMilkingKpi": {"TotalFailedMilkingCount": 1}}),
        "GET /t4capi/Animals/active": answer([
            {"Id": 4, "LifeNumber": "NL 100000001", "Name": "DINA 1", "UserNumber": 15, "ResponderNumber": "1000015", "BirthDate": "2019-01-12T00:00:00", "Location": "Melkvee 1"},
            {"Id": 7, "LifeNumber": "NL 100000002", "Name": "JANNA 2", "UserNumber": 41, "ResponderNumber": "1000041", "BirthDate": "2023-05-15T00:00:00", "Location": "Melkvee 1"},
            {"Id": 9, "LifeNumber": "NL 100000003", "Name": "GLORIA 3", "UserNumber": 3055, "ResponderNumber": "", "BirthDate": "2025-05-18T00:00:00", "Location": "Jongvee"}]),
        "GET /t4capi/Animal/status?animalIds=4&animalIds=7&animalIds=9": answer([
            {"AnimalId": 4, "ReproductionStatus": "Pregnant", "ReproductionStatusId": 4, "ProductionStatus": "In Lactation", "LactationNumber": 6, "DaysInLactation": 162, "PregnancyDays": 67},
            {"AnimalId": 7, "ReproductionStatus": "Inseminated", "ReproductionStatusId": 3, "ProductionStatus": "In Lactation", "LactationNumber": 2, "DaysInLactation": 100, "PregnancyDays": 0},
            {"AnimalId": 9, "ReproductionStatus": "Never Inseminated", "ReproductionStatusId": 2, "ProductionStatus": "Young Stock", "LactationNumber": 0, "DaysInLactation": 0, "PregnancyDays": 0}]),
        "GET /t4capi/v2/herds": answer([{"HerdName": "Kudde", "UCNNumber": "12345"}]),
        "GET /t4capi/v2/sires": answer([{"SireCode": "39943", "SireName": "GERRIT", "LifeNumber": "NL 728457683", "SireActive": True}]),
        "GET /t4capi/integrationglobal/parturitions?ucnNumber=12345": answer([
            {"Animal": icar("NL 100000001"), "EventDateTime": "2025-06-30T22:00:00Z"},
            {"Animal": icar("NL 100000001"), "EventDateTime": "2026-06-13T22:00:00Z"},
            {"Animal": icar("NL 999999999"), "EventDateTime": "2026-01-01T23:00:00Z"}]),   # an animal no longer there
        "GET /t4capi/integrationglobal/inseminations?ucnNumber=12345": answer([
            {"InseminationType": "Insemination", "SireIdentifiers": [{"Id": "NL 728457683", "Scheme": "eu.bovine"}, {"Id": "39943", "Scheme": "nl.aicode"}],
             "Animal": icar("NL 100000001"), "EventDateTime": "2026-09-16T22:00:00Z"},
            {"InseminationType": "Insemination", "SireIdentifiers": [{"Id": "39943", "Scheme": "nl.aicode"}],
             "Animal": icar("NL 100000001"), "EventDateTime": "2026-08-27T22:00:00Z"},
            {"InseminationType": "NaturalService", "SireIdentifiers": [], "Animal": icar("NL 100000002"), "EventDateTime": "2026-11-16T23:00:00Z"}]),
        "GET /t4capi/integrationglobal/pregnancy-checks?ucnNumber=12345": answer([
            {"Method": "Visual", "Result": "Pregnant", "Animal": icar("NL 100000001"), "EventDateTime": "2026-11-22T23:00:00Z"},
            {"Method": "Visual", "Result": "Empty", "Animal": icar("NL 100000002"), "EventDateTime": "2026-01-01T23:00:00Z"}]),
        "GET /t4capi/integrationglobal/gestations?ucnNumber=12345": answer([
            {"Animal": icar("NL 100000001"), "ExpectedCalvingDate": "2027-06-23T22:00:00Z"},
            {"Animal": icar("NL 100000002"), "ExpectedCalvingDate": "2026-12-01T23:00:00Z"}]),   # no longer pregnant
    }
    return farm


def herd(farm, key=KEY):
    with Farm("t4c", farm.address, "", key) as f:
        return {a["number"]: a for a in f.cow()["animals"]}, f.notes


def test_the_driver_knows_a_herd():
    assert next(d for d in drivers() if d["id"] == "t4c")["kinds"] == ["milk", "cow"]


def test_the_herd():
    animals, notes = herd(server())
    assert notes == "" and list(animals) == ["15", "41", "3055"]
    cow = animals["15"]
    assert (cow["name"], cow["lifeNumber"], cow["responder"], cow["birthDate"], cow["group"]) == ("DINA 1", "NL 100000001", "1000015", "2019-01-12", "Melkvee 1")
    assert (cow["production"], cow["reproduction"], cow["reproductionRaw"], cow["lactation"], cow["daysInMilk"]) == ("milking", "pregnant", "Pregnant", 6, 162)
    assert (cow["pregnantDays"], cow["calvingDate"], cow["expectedCalving"], cow["workNumber"]) == (67, "2026-06-14", "2027-06-24", "")


def test_events_in_date_order_on_the_farms_day():
    cow = herd(server())[0]["15"]
    # a UTC evening is the next day on the farm; the second insemination came first in the answer
    assert [(n["date"], n["sireName"], n["sireCode"], n["type"]) for n in cow["inseminations"]] == [
        ("2026-08-28", "GERRIT", "39943", "ai"), ("2026-09-17", "GERRIT", "39943", "ai")]
    assert cow["inseminations"][1]["sireLifeNumber"] == "NL 728457683"
    assert cow["pregnancyChecks"] == [{"date": "2026-11-23", "result": "pregnant"}]


def test_open_and_young_stock():
    animals = herd(server())[0]
    two, young = animals["41"], animals["3055"]
    assert two["reproduction"] == "inseminated" and two["expectedCalving"] == "" and two["pregnantDays"] is None
    assert two["inseminations"][0]["type"] == "bull" and two["pregnancyChecks"] == [{"date": "2026-01-02", "result": "notPregnant"}]
    assert (young["production"], young["reproduction"], young["daysInMilk"], young["responder"]) == ("young", "open", None, "")


def test_licence_key_instead_of_a_login():
    farm = server()
    herd(farm)
    assert [k for k, _ in farm.asked if not k.startswith("GET ")] == []   # no login request, only reads
    assert all(h.get("licensekey") == KEY for _, h in farm.asked)


def test_robots_as_a_real_server_answers():
    with Farm("t4c", server().address, "", KEY) as f:
        robot = f.milk()["robots"][0]
    assert (robot["id"], robot["name"], robot["group"], robot["raw"], robot["lastVisit"]) == ("101", "Melkrobot 1", "Melkveestal", "0", "2026-11-23T13:24:59")
    assert (robot["milkKg"], robot["milkings"], robot["failedMilkings"]) == (790, 103, 1)


def test_a_wrong_key_ends_the_read():
    try:
        herd(server(), key="wrong")
    except FarmError as e:
        assert e.result == 3 and "HTTP 500" in str(e)
    else:
        assert False


def test_no_key_and_no_user():
    try:
        herd(server(), key="")
    except FarmError as e:
        assert e.result == 2
    else:
        assert False


def test_a_missing_event_list_is_left_out():
    farm = server()
    farm.routes["GET /t4capi/integrationglobal/gestations?ucnNumber=12345"] = (500, "")
    animals, notes = herd(farm)
    assert animals["15"]["expectedCalving"] == "" and len(animals["15"]["inseminations"]) == 2 and "gestations" in notes


if __name__ == "__main__":
    run(globals())
