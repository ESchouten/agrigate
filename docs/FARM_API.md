# Farm service: which cow is where, over HTTP

A small service for a computer on the farm network that tells other programs,
such as a camera system that wants to know which cow it sees, **which animal is at which robot
right now** and **what the farm's management server knows about every animal**. It only reads:
nothing in it changes anything on the farm.

- **Live**, from the robots themselves (`core/live_c.h`): the robot says which cow it identified
  the moment it reads her responder. One listener per robot, so a farm with ten robots works the
  same as one with one. Driver: `lfw`.
- **The herd**, from the farm's server (`core/cow_c.h`): name, numbers, lactation, days in milk,
  reproduction, inseminations, pregnancy checks. Read again every 5 minutes. Driver: `t4c`.

Each live event and each robot's animal carries the herd's fields next to what the robot said, so
a client usually needs only `/api/events`.

## Start

```bash
cd python && pip install -e .
FARM_SERVER_TOKEN=<token> FARM_SERVER_PASSWORD=<licence key> python farm_server.py farm_server.json
```

`farm_server.json`:

```json
{
  "listen": "0.0.0.0:8099",
  "robots": [
    {"name": "Robot 1", "driver": "lfw", "address": "10.4.1.101"},
    {"name": "Robot 2", "driver": "lfw", "address": "10.4.1.102"}
  ],
  "server": {"driver": "t4c", "address": "10.4.1.1:6969", "user": ""},
  "herdRefreshSeconds": 300
}
```

- `robots[].address`: the robot's own address on the robot network (where its proxy listens on
  UDP 6000), not the farm server's.
- `server` is optional: without it only the live part. For `t4c` leave `user` empty and give the
  farm's licence key as password, in the file (`"password"`) or, better, in the environment as
  `FARM_SERVER_PASSWORD`. A user name and password of the server's web pages work too.
- The token is a word of your own choosing that every request has to carry, in the file
  (`"token"`) or in the environment as `FARM_SERVER_TOKEN`. Without a token the service answers
  only the computer it runs on.

Plain HTTP: use it on the farm network only, never forward the port to the internet.

## The API

Every call carries the token:

```
Authorization: Bearer <token>
```

No token or a wrong one: `401`. Everything is `GET` and answers JSON (UTF-8). Times are milliseconds since 1970-01-01 UTC; dates are `YYYY-MM-DD` on the farm's own day; a value
that is not known is `null` (numbers) or `""` (text).

### `GET /api/events?after=<seq>&wait=<seconds>`

The arrivals of all robots in one row, oldest first, with `seq` > `after`. With `wait` (at most 60)
the answer waits until there is one, so a client sees a cow the moment she is identified with one
request open at a time:

```
after = 0
loop:  GET /api/events?after=<after>&wait=30   ->  handle "events", after = "last"
```

```json
{"events": [
  {"seq": 1, "time": 1795437955042, "robot": "Robot 2", "kind": "arrival",
   "number": "73", "name": "JANNA 4", "responder": "1000073",
   "lifeNumber": "NL 100000073", "workNumber": "", "production": "milking", "reproduction": "pregnant",
   "lactation": 5, "daysInMilk": 232, "pregnantDays": 77, "expectedCalving": "2027-06-14"}
 ],
 "last": 1}
```

`number`, `name` and `responder` come from the robot (live); the rest from the herd (`null` when
the herd does not know the animal or no server is set). The service keeps the last 1000 events;
`seq` starts again at 1 when the service restarts (a client that sees `last` < its `after` starts
again from 0).

### `GET /api/robots`

Every robot with the animal it identified last:

```json
{"robots": [
  {"name": "Robot 2", "driver": "lfw", "address": "10.4.1.102", "connected": true,
   "lastMessage": 1795437955121,
   "animal": {"number": "73", "name": "JANNA 4", "responder": "1000073", "since": 1795437955042,
              "lifeNumber": "NL 100000073", "workNumber": "", "production": "milking",
              "reproduction": "pregnant", "lactation": 5, "daysInMilk": 232, "pregnantDays": 77,
              "expectedCalving": "2027-06-14"}}
]}
```

- `connected`: the robot said something in the last 10 seconds (a milking robot talks all the time).
- `animal` stays until the next one is identified: the robot does not always say when a cow
  leaves (a cow it refuses walks on without a word). `since` tells how old it is.
- With a TCS passage (robot 1 leads to robot 2) robot 1 also identifies the cows that walk
  through to robot 2, so such a cow appears at robot 1 and a minute later at robot 2.

### `GET /api/animals` and `GET /api/animals/<number | responder | life number>`

The herd as the server knows it (`core/cow_c.h` describes every field), plus `atRobot`:
`{"robot", "since"}` when the animal is a robot's current animal, else `null`.

```json
{"updated": 1795437721591, "error": "", "animals": [
  {"id": "4", "number": "15", "name": "DINA 1", "lifeNumber": "NL 100000015", "workNumber": "",
   "responder": "1000015", "birthDate": "2019-01-12", "group": "Melkvee 1",
   "production": "milking", "reproduction": "pregnant", "reproductionRaw": "Pregnant",
   "lactation": 6, "daysInMilk": 162, "calvingDate": "2026-06-14", "pregnantDays": 67,
   "expectedCalving": "2027-06-24",
   "inseminations": [{"date": "2026-09-17", "sireName": "GERRIT", "sireCode": "39943",
                      "sireLifeNumber": "NL 728457683", "type": "ai"}],
   "pregnancyChecks": [{"date": "2026-11-23", "result": "pregnant"}],
   "atRobot": null}
]}
```

`error` is the last problem reading the herd ("" when the last read went well); the animals of
the last good read stay. `/api/animals/<key>` gives one animal, or 404.

### `GET /api/status`

```json
{"service": "agrigate farm", "version": 1, "time": 1795437727482, "robots": 2,
 "robotsConnected": 2, "herd": {"animals": 92, "updated": 1795437721591, "error": ""}, "lastEvent": 0}
```

`herd` is `null` without a server.

## What each driver gives

| Drivers | Live (robot) | Herd (server) |
|---|---|---|
| `lfw` + `t4c` | number, name, responder | everything above; `workNumber` is empty (T4C has none: in the Netherlands it is usually the last four digits before the check digit of the life number) |

The makes behind the drivers are in the README's table. A new make adds a live driver (`drivers/<name>/src`, [DRIVERS.md](DRIVERS.md)) and/or a herd read
in its server driver; this API stays the same.
