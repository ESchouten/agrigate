# T4C driver

Reads the milking robots and the herd a T4C farm management server knows. Link:
`http`, port 6969 (a real server: `10.4.1.1:6969` on the robot network). The driver is
`src/T4c.cpp`; it only asks, with `GET` (and the login's `POST` when a user name is given).

## Login

Two ways, by what the user gives:

- **No user name, the farm's licence key as password** (what most farms have): every request
  carries the header `licensekey: <key>`, no login request. The header name must go out exactly
  so, in lower case: a real server answers `500 "Object reference not set to an instance of an
  object."` to `Licensekey` (what Python's urllib makes of it; `scraper_control.farm` sends the
  driver's headers as written).
- **A user name and password** of the server's web pages: `POST /t4capi/login/secure`,
  `data.accessToken.token` as `Authorization: Bearer`, with the user's `userid`, `herdid`, language
  and country headers. Not yet tried on a real server.

A real server answers with field names in PascalCase (`Data`, `Id`, `ServerEror`, sic) where the
API description says camelCase; the driver reads both (`Json` ignores case when a key is not found
as written). An error comes as HTTP 200 with `ServerEror.Code` other than 200: the read ends.

## Milking robots

| Call | For |
|---|---|
| `GET /t4capi/astronaut/overviews` | The robots: `Id`, `Name`, `BarnName` (group), `RobotStatus` (raw), `LastProcessTime` (last visit); also `AniUserNumber`, the last cow's number |
| `GET /t4capi/alarms/astronaut/{id}` | Its alarms: `creationDate`, `indicationId` (code), `description` (text) |
| `GET /t4capi/astronaut/{id}/detail` | Its figures: the first of `AstronautKpis` (`TotalMilkProduced`, `NumberOfMilking`) and `FailedMilkingKpi.TotalFailedMilkingCount` |

## The herd

Seven requests and one more per 150 animals:

| Call | For |
|---|---|
| `GET /t4capi/Animals/active` | The animals: `Id`, `UserNumber` (the farm's number), `Name`, `LifeNumber`, `ResponderNumber`, `BirthDate`, `Location` (group) |
| `GET /t4capi/Animal/status?animalIds=..&animalIds=..` | Per 150 animals: `ReproductionStatusId`/`ReproductionStatus`, `ProductionStatus`, `LactationNumber`, `DaysInLactation`, `PregnancyDays` |
| `GET /t4capi/v2/herds` | `UCNNumber`, the farm's number for the ICAR calls below |
| `GET /t4capi/v2/sires` | Sire names by AI code (`SireCode`) and life number |
| `GET /t4capi/integrationglobal/parturitions?ucnNumber=` | Calvings (the last one per animal) |
| `GET /t4capi/integrationglobal/inseminations?ucnNumber=` | Inseminations: `EventDateTime`, `InseminationType`, `SireIdentifiers` (`eu.bovine`, `nl.aicode`) |
| `GET /t4capi/integrationglobal/pregnancy-checks?ucnNumber=` | `EventDateTime`, `Result` (`Pregnant`, ...) |
| `GET /t4capi/integrationglobal/gestations?ucnNumber=` | `ExpectedCalvingDate` |

The `integrationglobal` lists are the ICAR Animal Data Exchange format; without dates they give the
whole history (on the server checked: 983 inseminations since 2013 in 0.8 s). Their times are a
day's start on the farm, given in UTC (`2025-11-24T23:00:00Z` = 25 November in the Netherlands): the
driver takes a UTC time from noon on as the next day.

`ReproductionStatusId`: 1 open, 2 never inseminated, 3 inseminated, 4 pregnant, 5 do not breed,
6 pregnant (automatic), 7 open (cycling), 8 "pregnant ?", 9 probably pregnant (from
`/t4capi/reproductionstatustypes`). `ProductionStatus`: `In Lactation`, `Dry Off`, `Young Stock`.
T4C keeps no work number.

## Checked on a real server

T4C 4.22, two Astronaut A4s, 92 animals (2026-11-23), with the licence key: the robots (0.6 s) and
the herd (1.8 s) read as the tests expect (`tests/test_t4c_herd.py` answers in the same shape).

Still open:

- what the numbers of `RobotStatus` mean: both robots, milking normally, gave `0`; every robot's
  state stays `unknown`, with the number in `raw`;
- what an alarm's `type` says about its severity (now `unknown`);
- over which time the figures run (they looked like the last 24 hours);
- the login with a user name and password;
- what a real server answers to a licence key it does not know (a `401` ends as "login rejected");
- an insemination by the farmer himself (`InseminationType` gave only `Insemination` here).

To see what a server says:

```bash
cd python
python farm.py t4c 10.4.1.1:6969 --herd                              # asks the licence key
python farm.py t4c <address> --user <name> --record answers.json     # logins and keys left out
```
