# Agrigate Milk and Cow: reading the farm

What the farm already knows, read in one way whatever it comes from. Everything here only
reads: nothing changes anything on the farm.

| Contract | What | In `core/` |
|---|---|---|
| **Milk** | The milking robots: state, alarms, figures | `milk_c.h` |
| **Cow** | The herd: numbers, lactation, reproduction, inseminations, pregnancy checks | `cow_c.h` |
| **Identity** | Which animal is where: an animal identified at a place (a milking robot, a feeding station, a gate), now and as it happens | `identity_c.h` |

A **source** is anything on the farm network that knows some of this: the farm's management
server, a robot. A program that reads a contract does not see which source it is or how it is
reached: a robot that tells by itself and a server that has to be asked give the same answers.

| Source | Driver | Milk | Cow | Identity |
|---|---|---|---|---|
| Lely T4C server | [`t4c`](../../drivers/t4c/API.md) | yes | yes | |
| Lely Astronaut A4 (the robot itself, experimental) | [`astronaut`](../../drivers/astronaut/API.md) | | | yes: the robot's box |
| Fullwood FULLSENSE server | [`fullsense`](../../drivers/fullsense/API.md) | yes | yes | yes: the robots, asked |

Not yet in the apps or on the buttons.

## Try it

```bash
cd python && pip install -e .
python farm.py                                   # the drivers in this build
python farm.py t4c 10.4.1.1:6969                 # robots and herd (asks the farm's licence key)
python farm.py t4c 192.168.1.10 --user erik      # or with a user of the server (asks the password)
python farm.py astronaut 10.4.1.101 --follow     # which cow is at the robot, as it happens
python farm.py fullsense 192.168.1.20 --user beheer --setup Agrigate   # make the server's user for Agrigate (once)
python farm.py t4c 10.4.1.1:6969 --record answers.json   # also save what the server said (logins left out)
```

In Python (`agrigate.session`):

```python
from agrigate.session import Session

with Session("t4c", "10.4.1.1:6969", "", licence_key) as farm:
    farm.milk()["robots"]        # core/milk_c.h describes every field
    farm.cow()["animals"]        # core/cow_c.h
with Session("astronaut", "10.4.1.101", on_event=print) as robot:
    robot.identity()["places"]   # core/identity_c.h
```

## The farm service

`python farm_server.py` puts every source of a farm behind one HTTP API, for other programs on
the farm network, such as a camera system that wants to know which cow it sees: [API.md](API.md).

## A new source

A new make of robot, feeding station or server software is one driver file; the contracts and
every program that reads them stay the same: [../DRIVERS.md](../DRIVERS.md).
