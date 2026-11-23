# FULLSENSE driver

Reads the milking robots a FULLSENSE farm management server knows. Link: `http`, port 12080.
The driver is `src/Fullsense.cpp`; it only asks, with `GET`, after one login. It never uses the
server's `/device/` calls: those are how the robots themselves report to the server.

| Call | For |
|---|---|
| `POST /api/Authentication/Authenticate` | The login: `token`, sent on as `Authorization: Bearer` |
| `GET /api/Location/GetLocations?locationType=1` | The robots: `id`, `description` (name), `locationGroup.name` (group) and the ids of their current status and alarm |
| `GET /api/MerlinStatus/GetMerlinStatus` | A robot's state (`displayStatus`) and since when (`start`) |
| `GET /api/MerlinAlarm/GetMerlinAlarm` | Its current alarm: `timestamp`, `alarmClass` (severity), `alarmCode` |

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

## Not checked on a real server yet

The driver was written from the server's API description and tested against a server that
answers as that description says (`tests/test_fullsense.py`). A real server has to show:

- that the login is taken as `Bearer`;
- the figures (milk, milkings, failed milkings): the server has them per milking and per
  period, and which of those match what the farmer sees is not known, so they are left out;
- older alarms: only the current one per robot is read.

To record what a server says (logins and passwords are left out of the file):

```bash
cd python && python farm.py fullsense <address> --user <name> --record answers.json
```
