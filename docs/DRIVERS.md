# Kinds of robots, drivers and links

Three things are kept apart, so that a new robot only adds what is new about it.

- **A kind of robot** is what you can do with it, the same for every make: a manure scraper
  drives routes and has a schedule, a milking robot has a state, alarms and figures. Each kind
  has one API in `core/`, and every app, the buttons and Python use only that.
- **A driver** (`drivers/<name>/`) is everything about one make: its protocol, its screens or
  calls, its safety rules. Nothing outside its folder knows the make.
- **A link** is how the driver's messages travel: Bluetooth, HTTP. The driver names the link
  it needs; each host (app, buttons, Python) implements a link once, for all drivers.

A driver never opens a connection itself. It is handed what came in and says what to send, so
the same C++ runs on a phone, a computer and a microcontroller.

## Two ways to reach a robot

| | Reached directly | Read through the farm's server | Listened to (live) |
|---|---|---|---|
| For | A robot with its own radio | What the farm's management software knows | What a robot says on the farm network as it happens |
| API in `core/` | `clean_c.h` | `robot_c.h`, and per kind a header on top: `milk_c.h`, `cow_c.h` | `live_c.h` |
| Links | `ble-serial`, `bt-serial`: bytes both ways | `http`: a request and its answer | `udp`: datagrams to and from the robot |
| Drivers | `lely` | `t4c`, `fullsense` | `lfw` |
| In one build | One driver | Every driver (`core/robot/Drivers.cpp`) | Every driver (`core/robot/Drivers.cpp`, `live::kDrivers`) |
| Used by | Buttons, apps, Python | Python (`agrigate.farm`, `farm.py`, `farm_server.py`) | Python (`agrigate.live`, `farm_server.py`) |

The farm service (`python/farm_server.py`, [FARM_API.md](FARM_API.md)) puts the last two together:
one live session per robot and the herd of the farm's server, over HTTP.

## A new driver for a farm's server

One file, `drivers/<name>/src/<Name>.cpp`, with a `robot::Driver` (`core/robot/Robot.h`):

1. `login`: ask for a login and put what the server wants to see again in the session's headers.
2. Per kind of robot the server knows, one function that asks (`get`) and fills that kind's
   data from the answers. Leave a kind the server does not know empty.
3. Add the driver to `core/robot/Drivers.cpp`, and its make to the table in the README.

Its tests go in `drivers/<name>/tests/test_*.py`: a server on the test computer that answers as
the real one does (`drivers/farm_test.py`). What the driver could not be checked against on a
real server goes in `drivers/<name>/API.md`.

## A new kind of robot

For example a feeding robot that the farm's server knows:

1. `core/feed_c.h`: what every feeding robot has in common, as `feed_read` and
   `feed_json` on a session of `robot_c.h`, like `milk_c.h`.
2. `core/robot/Feed.h` and `.cpp`: that data and its JSON.
3. One line each in `core/robot/Robot.h` (the driver's read for this kind, the session's data)
   and in the list of kinds in `core/robot/Robot.cpp`.
4. In each driver whose server knows such robots: the function that reads them.

The session, the login, the HTTP exchange and the hosts' side of it stay as they are. A kind
that also takes commands adds them to its own header; whether a command is safe to give from a
distance is decided per kind and per command, in the driver.

A robot of a new kind that is reached directly gets its own API in `core/` in the way of
`clean_c.h`, on the links that are there.

## A new live driver

For a make whose robots say on the farm network which animal they identified: one file,
`drivers/<name>/src/<Name>.cpp`, with a `robot::live::Driver` (`core/robot/Live.h`):

1. `tick`: queue what has to go to the robot now (`Session::send`), for example a registration it
   has to renew. Called about every 100 ms.
2. `incoming`: a datagram from the robot; `Session::heard` when it is one of the robot's own,
   `Session::arrival` when it says which animal is there.
3. `stop`: the goodbye, if the robot wants one.
4. Add it to `live::kDrivers` in `core/robot/Drivers.cpp`, and its make to the table in the README.

A live driver only listens: it sends no more than the robot needs to hand its messages on, never
a command. Its tests feed it datagrams through the C API (`drivers/lfw/tests`).
