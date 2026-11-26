# FULLSENSE driver

Reads what a FULLSENSE farm management server knows: its milking robots and their figures, which
cow is in which robot, and the herd. Link: `http`, port 12080. The driver is `src/Fullsense.cpp`;
it only asks, with `GET`, after one login (and once writes, to make its own user). It never uses the server's `/device/` calls (how the
robots themselves report to the server) and never writes to the ICAR lists.

The server has two APIs, both behind the same login: its own (`/api`, the robots as they are now)
and the ICAR Animal Data Exchange lists (`/ICAR`, the herd and the milkings, with filters). The
ICAR lists are read with the same code as T4C's (`core/session/Icar.h`).

## The login and the setup

| Call | For |
|---|---|
| `POST /api/Authentication/Authenticate` | `{"userName", "password"}`; the answer's `token` is sent on as `Authorization: Bearer` |

Agrigate reads with a user of its own: of the type "Programma" in the FULLSENSE program's user
management (Gebruikersbeheer), named `Agrigate`, with a password of its own. Never a farmer's login
or another program's user. The driver makes it (`session_setup`), logged in once as the farm's
administrator, whose password is not kept:

```bash
cd python && python farm.py fullsense <address> --user <administrator> --setup Agrigate
```

| Call | For |
|---|---|
| `GET /api/Settings/GetRoles` | The program role: the first whose name has "program" or "applicat" in it; none, and the setup stops |
| `GET /api/Settings/GetUsers` | A user `Agrigate` already there: the setup gives it the new password |
| `POST /api/Settings/SaveUser` | The user: `userName`, `role`, `isActive`, a password used once (`oneTimePassword`) |
| `POST /api/Authentication/ChangeUserPassword` | From that one to the user's own, as a new user does on first login |
| `POST /api/Authentication/Authenticate` | The new user logs in |

That is the only time the driver writes to the server. Making the user by hand in the program
works the same.

## Milk

| Call | For |
|---|---|
| `GET /api/Location/GetLocations?locationType=1` | The robots: `id`, `description` (name), `locationIndex` (box number), `locationGroup.name` (group) and the ids of their current status, alarm and milking |
| `GET /api/MerlinStatus/GetMerlinStatus` | A robot's state (`displayStatus`) and since when (`start`) |
| `GET /api/MerlinAlarm/GetMerlinAlarm` | Its current alarm: `timestamp`, `alarmClass` (severity), `alarmCode` |
| `GET /ICAR/locations` | The farm the ICAR lists are for: the first one's `identifier` (`scheme`, `id`) |
| `GET /ICAR/locations/{scheme}/{id}/milking-visits?pageSize=100&currentPage=N&meta-modified-from=` | The milkings of the last 24 hours, page by page: per robot (`milkingBoxNumber` = its `locationIndex`) `milkings` (`milkingComplete`), `failedMilkings` (not complete), `milkKg` (`milkingMilkWeight`, `KGM` or `GRM`) and `lastVisit` (the last `milkingStartingDateTime`) |

`meta-modified-from` is when a milking was last changed (on a real server: when it ended), so the
driver also leaves out a milking that started before those 24 hours. Without the host's clock, or
when the user may not read ICAR, the figures stay `null` and the states are still read.

| `displayStatus` | State |
|---|---|
| Idle, WaitingForCow | `idle` |
| Milking, Releasing | `milking` |
| Cleaning | `cleaning` |
| Alarm | `alarm` |
| Emergency | `emergency` |
| WaitingForAssistance | `assistance` |
| Paused | `paused` |
| Offline | `offline` |

## Identity

| Call | For |
|---|---|
| `GET /api/Location/GetLocations?locationType=1` | The robots (one place each) and their current milking (`currentMilkRecordingId`) |
| `GET /api/Milking/GetMilkRecording?id=` | That milking: the cow (`animal.animalNumber`, `name`, the first tag's `tagNumber` as responder), `milkingStartTime`, `merlinDecision`, `isFinished` |

The server has to be asked, so a read gives what happened since the last one: a new milking is
`identified` at its start; `merlinDecision` `Milking` starts the visit, `NotAllowed` is `refused`;
`isFinished`, or a robot without a current milking, ends it. A milking that came and went between
two reads still gives all three events. Read it every few seconds (`farm.py --follow` asks every
10 s). Feeding stations and separation gates are not places yet: the ICAR feed intakes name no
station, and no farm with gates has been seen.

## Cow

All from the ICAR lists of the farm (`/ICAR/locations/{scheme}/{id}/...`):

| List | For |
|---|---|
| `animals` | The animals (not `Dead` or `OffFarm`): `identifier.id` (the id), `managementTag` (number), `name`, `alternativeIdentifiers` (`eu.bovine`: life number), `birthDate`, `lactationStatus` (`Lactating`, `Fresh`: milking; `Dry`), `reproductionStatus` |
| `parturitions`, `inseminations`, `pregnancy-checks`, `heats`, `drying-offs`, `do-not-breeds` | The events, by `animal.id` |
| `lactations` | `parity` (the lactation number; none: young stock) and `beginDate` (calving) |

| `reproductionStatus` | Reproduction |
|---|---|
| Open, NotPregnant, Birthed | `open` |
| Inseminated | `inseminated` |
| Pregnant, PregnantMultipleFoetus | `pregnant` |
| DoNotBreed | `doNotBreed` |

The server gives no day counts, so the driver counts them from the events on the day of the read
(UTC): `daysInMilk` and `inseminationsThisLactation` from the last calving, `daysSinceInsemination`,
`pregnantDays` (from the last insemination), `daysSinceHeat`. No responder, group, sire or expected
calving: the ICAR lists of this server do not have them.

## Not checked on a real server yet

All of this was written from the server's API description and tested against a server that
answers as that description says (`tests/test_fullsense.py`). Seen on a real server so far: a
"Programma" user, and `milking-visits` with `meta-modified-from` (123 of 90,299 milkings in a day).
A real server still has to show:

- that the login is taken as `Bearer`, for the `/api` and the `/ICAR` calls alike;
- the setup: the program role's name, whether `SaveUser` takes `oneTimePassword` so, and which
  passwords the server accepts;
- the ICAR farm's `scheme`, the animals' identifier schemes (the life number), and whether
  `milkingBoxNumber` is the robot's `locationIndex`;
- that a robot's `currentMilkRecordingId` is the milking now, and what `merlinDecision` and the
  tags hold;
- older alarms: only the current one per robot is read.

To record what a server says, best while a robot milks (logins and passwords are left out of the
file; it holds the farm's own animals, so share it only with whom the farmer agrees to):

```bash
cd python && python farm.py fullsense <address> --user Agrigate --record answers.json
cd python && python farm.py fullsense <address> --user Agrigate --follow --record answers.json   # and keep following the robots (Ctrl+C stops)
```
