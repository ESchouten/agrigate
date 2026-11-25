# Farm service: what the farm knows, over HTTP

A small service for a computer on the farm network that tells other programs, such as a camera
system that wants to know which cow it sees, **which animal is where right now**, **what the
farm knows about every animal** and **how the milking robots are doing**. It only reads: nothing
in it changes anything on the farm.

It reads the farm's **sources** (a robot, the farm's management server: [README.md](README.md))
and answers per contract, the same whatever the sources are:

- **Identity** (`/api/identity`): an animal identified at a place, the moment the place reads her
  responder. A place is a milking robot, a feeding station, a gate.
- **Cow** (`/api/cow`): the herd: name, numbers, lactation, days in milk, reproduction,
  inseminations, pregnancy checks.
- **Milk** (`/api/milk`): the milking robots: state, alarms, figures.

Each identified animal carries the herd's fields next to what the place said, so a client that
follows cows usually needs only `/api/identity/events`.

## Start

```bash
cd python && pip install -e .
FARM_SERVER_TOKEN=<token> FARM_SERVER_PASSWORD=<licence key> python farm_server.py farm_server.json
```

`farm_server.json`:

```json
{
  "listen": "0.0.0.0:8099",
  "sources": [
    {"name": "Robot 1", "driver": "astronaut", "address": "10.4.1.101"},
    {"name": "Robot 2", "driver": "astronaut", "address": "10.4.1.102"},
    {"driver": "t4c", "address": "10.4.1.1:6969", "user": ""}
  ],
  "refreshSeconds": 300
}
```

- `sources`: one per robot or server; `python farm.py` lists the drivers and what each serves.
  The service reads every contract a source serves.
- `name` names the source's place where the source has no name for it (a robot does not know
  what the farmer calls it).
- `address` for `astronaut`: the robot's own address on the robot network, not the farm
  server's. For `t4c` leave `user` empty and give the farm's licence key as password, in the file
  (`"password"`) or, better, in the environment as `FARM_SERVER_PASSWORD`. A user name and
  password of the server's web pages work too.
- `refreshSeconds`: how often a source that has to be asked is asked again (30 at least). A
  source that tells by itself is always up to date.
- The token is a word of your own choosing that every request has to carry, in the file
  (`"token"`) or in the environment as `FARM_SERVER_TOKEN`. Without a token the service answers
  only the computer it runs on.

Plain HTTP: use it on the farm network only, never forward the port to the internet.

## The API

Every call carries the token:

```
Authorization: Bearer <token>
```

No token or a wrong one: `401`. Everything is `GET` and answers JSON (UTF-8). Times are
milliseconds since 1970-01-01 UTC; dates are `YYYY-MM-DD` on the farm's own day; a value that is
not known is `null` (numbers) or `""` (text).

### `GET /api/identity/events?after=<seq>&wait=<seconds>`

What happened at all places in one row, oldest first, with `seq` > `after`: an animal identified,
and at a milking robot that says so, a milking started and a visit ended. With `wait` (at most 60)
the answer waits until there is one, so a client sees a cow the moment she is identified with one
request open at a time:

```
after = 0
loop:  GET /api/identity/events?after=<after>&wait=30   ->  handle "events", after = "last"
```

```json
{"events": [
  {"seq": 1, "time": 1795437955042, "kind": "identified",
   "place": {"id": "10.4.1.102", "name": "Robot 2", "kind": "milkingRobot"},
   "animal": {"number": "73", "name": "JANNA 4", "responder": "1000073",
              "lifeNumber": "NL 100000073", "workNumber": "", "production": "milking",
              "reproduction": "pregnant", "lactation": 5, "daysInMilk": 232, "pregnantDays": 77,
              "expectedCalving": "2027-06-14", "daysSinceInsemination": 77, "daysSinceHeat": 98}},
  {"seq": 2, "time": 1795437994870, "kind": "milkingStarted",
   "place": {"id": "10.4.1.102", "name": "Robot 2", "kind": "milkingRobot"},
   "animal": {"number": "73", "name": "JANNA 4", "...": "the same fields"}},
  {"seq": 3, "time": 1795438351002, "kind": "visitEnded", "place": {"...": "..."}, "animal": {"number": "73", "...": "..."}}
 ],
 "last": 3}
```

- `kind`: `identified` (the place read the animal's responder), `milkingStarted` (the cups go on:
  the animal is the one identified closest to that moment, so not a cow walking past to the next
  robot), `visitEnded` (the robot is done and lets the animal out; she may still be in the box
  for some seconds). A milking robot that does not say when it milks only gives `identified`.

- `place.kind`: `milkingRobot`, `feedingStation`, `gate` or `unknown`. A client that wants only
  one kind filters on it; one that does not care never has to look.
- `animal.number`, `name` and `responder` come from the place; the rest from the herd (`null`
  when the herd does not know the animal or no source serves the herd).
- The service keeps the last 1000 events; `seq` starts again at 1 when the service restarts (a
  client that sees `last` < its `after` starts again from 0).

### `GET /api/identity/places`

Every place with the animal identified there last:

```json
{"places": [
  {"id": "10.4.1.102", "name": "Robot 2", "kind": "milkingRobot", "driver": "astronaut",
   "connected": true, "updated": 1795437955121,
   "animal": {"number": "73", "name": "JANNA 4", "responder": "1000073", "since": 1795437955042,
              "lifeNumber": "NL 100000073", "workNumber": "", "production": "milking",
              "reproduction": "pregnant", "lactation": 5, "daysInMilk": 232, "pregnantDays": 77,
              "expectedCalving": "2027-06-14", "daysSinceInsemination": 77, "daysSinceHeat": 98},
   "milking": true, "milkingSince": 1795437994870}
]}
```

- `connected`: the source answers (a robot that tells by itself said something in the last 10
  seconds); `updated`: when it last did.
- `animal` stays until the next one is identified: a place does not always say when an animal
  leaves (a cow a robot refuses walks on without a word). `since` tells how old it is.
- With a passage from one robot to the next, the first robot also identifies the cows that walk
  through, so such a cow appears at robot 1 and a minute later at robot 2. When robot 1 starts
  milking, its `animal` is the cow in its box again.
- `milking`: the animal is being milked right now (from `milkingStarted` to `visitEnded`), with
  `milkingSince`; `false` and `null` otherwise, and always at a place that does not say so.

### `GET /api/cow/animals` and `GET /api/cow/animals/<number | responder | life number>`

The herd (`core/cow_c.h` describes every field), plus `at`: `{"place", "since"}` when the animal
is the one a place identified last, else `null`.

```json
{"animals": [
  {"id": "4", "number": "15", "name": "DINA 1", "lifeNumber": "NL 100000015", "workNumber": "",
   "responder": "1000015", "birthDate": "2019-01-12", "group": "Melkvee 1",
   "production": "milking", "reproduction": "pregnant", "reproductionRaw": "Pregnant",
   "lactation": 6, "daysInMilk": 162, "calvingDate": "2026-06-14", "pregnantDays": 67,
   "expectedCalving": "2027-06-24", "dryOffDate": "2026-04-20", "daysSinceInsemination": 67,
   "daysSinceHeat": 88, "inseminationsThisLactation": 2, "doNotBreedDate": "",
   "inseminations": [{"date": "2026-09-17", "sireName": "GERRIT", "sireCode": "39943",
                      "sireLifeNumber": "NL 728457683", "type": "ai"}],
   "pregnancyChecks": [{"date": "2026-11-23", "result": "pregnant"}],
   "heats": [{"date": "2026-08-27", "method": "visual"}],
   "at": null}
]}
```

`/api/cow/animals/<key>` gives one animal, or 404. When a read of the herd fails, the animals of
the last good read stay (see `/api/status`).

### `GET /api/milk/robots`

The milking robots (`core/milk_c.h` describes every field), plus the `driver` they were read with
and when (`updated`):

```json
{"robots": [
  {"id": "101", "name": "Robot 1", "group": "Melkveestal", "state": "milking", "raw": "3",
   "since": "", "lastVisit": "2026-11-23T13:24:59", "alarms": [],
   "milkKg": 790.0, "milkings": 103, "failedMilkings": 1, "driver": "t4c", "updated": 1795437721591}
]}
```

### `GET /api/status`

Every source and, per contract it serves, when it was last read and what is wrong (`""`: nothing):

```json
{"service": "agrigate farm", "version": 2, "time": 1795437727482,
 "sources": [
   {"name": "Robot 1", "driver": "astronaut", "address": "10.4.1.101",
    "contracts": {"identity": {"updated": 1795437727301, "error": ""}}},
   {"name": "", "driver": "t4c", "address": "10.4.1.1:6969",
    "contracts": {"milk": {"updated": 1795437721591, "error": ""},
                  "cow": {"updated": 1795437721591, "error": ""}}}
 ],
 "lastEvent": 0}
```

## Changed since version 1

Version 1 knew robots and one server; version 2 knows sources and contracts, so a feeding station
or a gate that identifies animals appears without a change for its clients.

| Version 1 | Version 2 |
|---|---|
| `/api/events`, flat: `"robot": name`, `"kind": "arrival"`, the animal's fields beside them | `/api/identity/events`: `"place": {id, name, kind}`, `"kind": "identified"`, `"animal": {...}` |
| `/api/robots` (`lastMessage`) | `/api/identity/places` (`updated`); the milking robots' own state is `/api/milk/robots` |
| `/api/animals` (`atRobot`, `updated`, `error`) | `/api/cow/animals` (`at`); `updated` and `error` are in `/api/status` |
| Configuration `robots`, `server`, `cowRefreshSeconds` | `sources` (the server is one of them), `refreshSeconds` |

## What each driver gives

| Driver | Identity | Cow | Milk |
|---|---|---|---|
| `astronaut` | number, name, responder; one place per robot | | |
| `t4c` | | everything above; `workNumber` is empty (T4C has none: in the Netherlands it is usually the last four digits before the check digit of the life number) | everything above; `state` is `unknown` with the server's number in `raw` |
| `fullsense` | | | not checked on a real server |

The makes behind the drivers are in the [README](README.md). A new make is a new driver
([DRIVERS.md](../DRIVERS.md)); this API stays the same.
