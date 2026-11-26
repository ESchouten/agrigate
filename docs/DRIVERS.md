# Contracts, drivers and links

Three things are kept apart, so that a new robot only adds what is new about it.

- **A contract** is what you can read or do, in the farm's own words and the same for every
  make: a manure robot drives routes and has a schedule; milking robots have a state, alarms and
  figures; an animal is identified at a place. Each contract is one C header in `core/`, and
  every app, the buttons and Python use only that. A contract says nothing about how its data
  travels: whether a source tells by itself or has to be asked, over Bluetooth, HTTP or UDP, the
  calls and the JSON are the same.
- **A driver** (`drivers/<name>/`) is everything about one make: its protocol, its screens or
  calls, its safety rules. Nothing outside its folder knows the make.
- **A link** is how the driver's messages travel. The driver names the link it needs; each host
  (app, buttons, Python) implements a link once, for all drivers.

A driver never opens a connection itself. It is handed what came in and says what to send, so
the same C++ runs on a phone, a computer and a microcontroller.

## The contracts

| Domain | Contract | In `core/` | Drivers |
|---|---|---|---|
| Clean | Manure robots: routes, schedule, status, and each make's own panel | `clean_c.h` | `discovery` |
| Milk | Milking robots: state, alarms, figures | `milk_c.h` | `t4c`, `fullsense` |
| Cow | The herd | `cow_c.h` | `t4c`, `fullsense` |
| Cow | Identity: which animal is where | `identity_c.h` | `astronaut`, `fullsense`, `t4c` |

Every contract works on a **session** (`session_c.h`): one source, one driver, one link.
`session_c.h` is the host's side (how to keep the session going, per link); the contracts on top
never mention it. The links: `http` (a request and its answer), `udp` (datagrams, for a source
that tells by itself) and `ble-serial` / `bt-serial` (bytes both ways over Bluetooth, for a
robot that is spoken with key by key). Every driver is in every build
(`core/session/Drivers.cpp`): the apps (`native/shared/.../core`), Python (`agrigate`,
`agrigate.session`, `farm.py`, `farm_server.py`). The buttons use the Clean driver's C++
directly, on a microcontroller's one thread.

Milk, Cow and identity are read. Clean is commands: a call starts one and the session carries
it out. What every manure robot has is in the contract (routes, the schedule, the safety stop
and its question, the status); what only one make has is the driver's own and goes by name:
its panel's keys (`clean_input`), its steps while a command runs, and the `own` part of the
status (its display, its manual control). `clean_memory` is what the driver wants kept with a
robot between sessions; a host stores it without reading it.

## A new driver on a session

One file, `drivers/<name>/src/<Name>.cpp`, with an `agrigate::Driver` (`core/session/Session.h`):

1. Its links, the preferred first: `{{"http", "http", <port>}}` or `{{"udp", nullptr, <port>}}`.
2. Per contract the source serves, one function that brings that contract's data in the session
   up to date (`Milk.h`, `Cow.h`, `Identity.h`). Leave a contract the source does not know empty.
   - Over `http`: `login` asks for a login and puts what the server wants to see again in the
     session's headers; a contract's function asks (`get`, `post`) and fills the data from the
     answers. A server that wants a user of Agrigate's own gets `setup`: it makes that user
     (`Session::setupUser`, `setupPassword`), logged in as one who may (`session_setup`). It is
     the one thing a driver may write, once.
   - Over `udp`: `tick` queues what has to go to the source now (`Session::send`), for example a
     registration it has to renew; `incoming` is handed each datagram and fills the data
     (`Session::heard` when it is one of the source's own); `stop` queues the goodbye. The
     contract's function is empty: there is nothing to ask.
3. Add the driver to `core/session/Drivers.cpp`, and its make to the tables in the README and
   [farm/README.md](farm/README.md).

A device that identifies animals (a robot, a feeding station, a gate) reports each one with
`identity.identified(place, animal, now)`; that is all it takes to appear in every program that
follows identity.

These drivers only read. What a `udp` driver sends is no more than the source needs to keep
telling, never a command.

A manure robot's driver serves `clean`: it makes the session's robot (`core/session/Clean.h`),
which takes the commands and the link's bytes. `drivers/discovery/src/Discovery.cpp` is the
example: a robot spoken with key by key, so its conversation runs on a thread of its own behind
the session.

Its tests go in `drivers/<name>/tests/test_*.py`: a server on the test computer that answers as
the real one does (`drivers/farm_test.py`), or datagrams fed through the C API
(`drivers/astronaut/tests`). What the driver could not be checked against on a real source goes
in `drivers/<name>/API.md`.

## A new contract

For example feeding robots:

1. `core/feed_c.h`: what every feeding robot has in common, as `feed_read` and `feed_json` on a
   session, like `milk_c.h`. In the farm's words, nothing about how it is reached.
2. `core/session/Feed.h` and `.cpp`: that data and its JSON.
3. One line each in `core/session/Session.h` (the driver's function for this contract, the
   session's data) and in the list of contracts in `core/session/Session.cpp`.
4. In each driver whose source knows such robots: the function that reads them.

The session, the login, the links and the hosts' side of them stay as they are. A contract that
also takes commands adds them to its own header; whether a command is safe to give from a
distance is decided per contract and per command, in the driver.

## A new link

A link is added once in `session_c.h` (what the host moves) and once in each host
(`python/agrigate/session.py`, the apps' `link/`). Drivers and contracts on other links do not
change.
