"""Offline tests: the Python API (python/, on top of this driver) against the simulated robot.
Run: python3 drivers/discovery/tests/test_api.py   (builds the core library first)"""
import asyncio, os, sys, tempfile, types
from pathlib import Path

os.environ["XDG_CONFIG_HOME"] = tempfile.mkdtemp()
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "python"))
sys.path.insert(0, os.path.dirname(__file__))
try:
    import bleak  # noqa
except ImportError:
    m = types.ModuleType("bleak"); m.BleakClient = m.BleakScanner = object; sys.modules["bleak"] = m

from agrigate import Robot, RobotBusy, api
from fake_robot import FakeRobot

from agrigate.build_core import LIBNAME, build
os.environ["AGRIGATE_CORE_LIB"] = str(build(Path(tempfile.mkdtemp()) / LIBNAME))

ADDR = "00:12:FF:A0:80:01"


def run(coro):
    return asyncio.run(coro)


def make(**kw):
    holder = {}
    def factory(address, on_data):
        holder["r"] = FakeRobot(address, on_data, **kw)
        return holder["r"]
    api.BleTransport = factory
    return holder


def test_status():
    h = make()
    async def go():
        async with Robot(pin="4321", address=ADDR) as robot:
            return robot.status()
    st = run(go())
    assert st.planning is True and st.next_ride == "21:00:00" and st.route == "01"
    assert st.ride_no == "10" and st.battery_mv == 13500 and not st.busy


def test_wrong_pin():
    make()
    async def go():
        async with Robot(pin="0000", address=ADDR):
            pass
    try:
        run(go()); assert False
    except PermissionError:
        pass


def test_route_with_planning_on_answers_ja():
    h = make(planning=True)
    async def go():
        async with Robot(pin="4321", address=ADDR) as robot:
            await robot.start_route("C")
    run(go()); r = h["r"]
    assert r.mode == "driving" and r.sel == 2 and r.answer == "ja"


def test_route_with_planning_off_answers_nee_and_keeps_off():
    h = make(planning=False)
    async def go():
        async with Robot(pin="4321", address=ADDR) as robot:
            await robot.start_route("B")
    run(go()); r = h["r"]
    assert r.mode == "driving" and r.sel == 1 and r.answer == "nee" and "start_stop" not in r.pressed[:1]


def test_dry_run_restores():
    h = make(planning=True)
    async def go():
        async with Robot(pin="4321", address=ADDR) as robot:
            await robot.start_route("D", dry_run=True)
    run(go()); r = h["r"]
    assert r.mode == "home" and r.planning is True and r.answer is None


def test_busy_presses_nothing():
    h = make(mode="driving")
    async def go():
        async with Robot(pin="4321", address=ADDR) as robot:
            await robot.start_route("A")
    try:
        run(go()); assert False
    except RobotBusy:
        pass
    assert h["r"].pressed == [] and h["r"].mode == "driving"


def test_stop_route_while_driving():
    h = make(mode="driving")
    async def go():
        async with Robot(pin="4321", address=ADDR) as robot:
            await robot.stop_route()
    run(go())
    assert h["r"].pressed == ["start_stop"] and h["r"].stopped and h["r"].mode == "abort"   # never Ja: that drives on


def test_image_version_is_remembered():
    from pathlib import Path
    from agrigate import discover
    discover.CACHE = Path(tempfile.mkdtemp()) / "last.json"
    h = make()
    async def go():
        async with Robot(pin="4321", address=ADDR):
            pass
    run(go()); assert h["r"].images_sent == 52      # first time: robot sends images
    run(go()); assert h["r"].images_sent == 0       # second time: skipped



def session(robot, memory=""):
    """A session of the C API itself (core/clean_c.h) with a simulated robot on its link."""
    from agrigate import core
    def write(frame):
        try:
            robot.write(frame).send(None)
        except StopIteration:
            pass
    s = core.CoreSession(write, "4321", memory)
    robot.on = s.feed
    return s


def steps(s):
    from agrigate import core
    out = []
    while step := core.text(core.lib().clean_next_step, s._s):
        out.append(step)
    return out


def test_the_driver_its_links_and_its_steps():
    from agrigate import core
    d = next(d for d in core.drivers() if d["id"] == "discovery")
    assert d["contracts"] == ["clean"] and [l["kind"] for l in d["links"]] == ["ble-serial", "bt-serial"]
    assert d["links"][0]["rx"] and d["links"][1]["addressPrefix"] == "00:12:ff"
    s = session(FakeRobot(planning=True), '{"image":"444953303031385434"}')
    try:
        assert s.connect() == core.OK and steps(s) == ["Linked", "Granted", "ScreenReady"]
        assert s.start_route("C") == core.OK
        assert steps(s) == ["PlanningOff", "Menu", "Selected", "StartPressed", "Answered"]
        st = s.status()
        assert st["driving"] and st["planning"] is None and "own" in st and st["progress"] is None
        assert s.start_route("A") == core.BUSY and "busy" in s.last_error()
        assert core.lib().clean_input(s._s, b"Nothing", 0) == core.UNEXPECTED   # not one of this robot's keys
    finally:
        s.close()


def test_what_the_robot_is_owed_is_kept_and_paid_back():
    import json, time
    from agrigate import core
    robot = FakeRobot(planning=True)
    s = session(robot, '{"image":"444953303031385434"}')
    assert s.connect() == core.OK and s.press("StartStop") == core.OK   # the schedule off by hand on the panel
    owed = json.loads(s.memory())
    assert owed["owedPlanning"] is True and abs(owed["owedAt"] - time.time() * 1000) < 60_000 and not robot.planning
    s.link_down(); s.close()   # the link drops before the session could put it back
    for memory, paid in ((json.dumps({**owed, "owedAt": owed["owedAt"] - 7 * 3600 * 1000}), False), (json.dumps(owed), True)):
        s = session(robot, memory)   # 7 hours later it is forgotten: someone may have meant it
        assert s.connect() == core.OK and robot.planning is paid
        assert json.loads(s.memory()) == {"image": "444953303031385434"}
        s.link_down(); s.close()


def test_a_held_key_and_a_cancelled_route():
    import threading, time
    from agrigate import core
    robot = FakeRobot(mode="manual", planning=False)
    s = session(robot)
    try:
        assert s.connect() == core.OK
        threading.Timer(0.5, lambda: core.lib().clean_release(s._s)).start()
        t = time.monotonic()
        assert s._do(core.lib().clean_input(s._s, b"StartStop", 1)) == core.OK and 0.4 < time.monotonic() - t < 3
    finally:
        s.close()
    robot = FakeRobot(planning=True, slow=True)
    s = session(robot, '{"image":"444953303031385434"}')
    try:
        assert s.connect() == core.OK
        threading.Timer(0.3, s.cancel).start()
        assert s.start_route("B") == core.CANCELLED and "cancel" in s.last_error()
        assert robot.mode == "home" and robot.planning is True
    finally:
        s.close()


def test_bluetooth_that_fails_twice_is_tried_again():
    tries = []
    def factory(address, on_data):
        robot = FakeRobot(address, on_data)
        if len(tries) < 2:
            async def fail(): raise OSError("connection failed")
            robot.connect = fail
        tries.append(robot)
        return robot
    api.BleTransport = factory
    async def go():
        async with Robot(pin="4321", address=ADDR) as robot:
            return robot.status()
    assert run(go()).planning is True and len(tries) == 3

if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn(); print("ok", name)
