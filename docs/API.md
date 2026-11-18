# Web API

The buttons (M5Stack) and the desktop app answer the same small HTTP API on the local network,
so apps and other tools (Home Assistant, a script, Siri Shortcuts) can control a robot that is
out of their own Bluetooth range. The device that serves it holds the Bluetooth connection.

- **Buttons**: with a wifi network set they stay on it and serve on port 80.
- **Desktop app**: Settings > Web page and API, port 8080 by default. The same address in a
  browser is the **web page**: the app itself, for a phone or computer without the app. Its robot
  goes through that computer's web API like any app's.

Each device has a fixed name of its own on the network, from its network address:
`http://scraper-control-1a2b.local` (buttons: Settings > Wifi; desktop: Settings > Web page and API).

**In the apps**, a robot can go through such a device instead of Bluetooth: in the robot's
settings, Connection, enter the device's address. Routes, the schedule, the status and the stop
question work the same; the robot's own panel and manual driving work through a desktop app
(see The panel), not through the buttons. The device itself may reach the robot through yet
another device.

Plain HTTP: use it on your own network only, never forward the port to the internet (use a
VPN for remote access).

## Access

Every call carries the robot's PIN:

```
Authorization: Bearer <PIN>
```

No PIN or a wrong one: `401`. Five wrong PINs in a row close the API for a minute (`429`).
A device without any PIN set is open.

## Calls

| Call | What |
|---|---|
| `GET /api/status` | The robot as last seen. |
| `GET /api/status/fresh` | Reads the robot now. |
| `POST /api/route/<A-D>` | Start that route. While a route runs it stops it instead (a safety stop, like a route button). |
| `POST /api/route/<A-D>/dryRun` | Only the checks of a start: the robot is put back and nothing drives. |
| `POST /api/stop` | Stop the running route (a safety stop). `Busy` when no route runs. |
| `POST /api/planning/on`, `/off` | The schedule on or off. |
| `POST /api/answer/yes`, `/no` | After a stop the robot asks whether to finish the route (`finishQuestion`). Yes drives on, no ends the route and leaves the schedule off. Never answered by itself. |

One call at a time: the device runs it to the end, connecting and disconnecting the robot, and
then answers.

## The panel

Only the desktop app answers these (the buttons: `404`), for the robot's own panel and manual
driving in the apps and on the web page.

| Call | What |
|---|---|
| `GET /api/panel` | Keeps the connection to the robot open and answers with the robot's display in `status` (`screen`, `highlighted`, `extra`). Ask again a few times a second: 5 seconds after the last ask the robot is let go. |
| `POST /api/panel/<key>` | A tap on a key, by the robot type's name for it (`Up`, `Down`, `Enter`, `Back`, `StartStop`, `Prog`, `Soft1`..`Soft3`). |
| `POST /api/panel/<key>/down` | The key goes down and stays down. Repeat it a few times a second while it is held: a second after the last repeat the key comes up by itself, so a page that is closed or a phone that leaves the network never keeps the robot driving. |
| `POST /api/panel/<key>/up` | The key comes up. |

## The log

Only the buttons answer these (the desktop app: `404`), to see what happened without a USB
cable.

| Call | What |
|---|---|
| `GET /api/log` | The last 4 KB of the log, as text: what the buttons did with the robot, the wifi and updates, the free memory. A restart or crash keeps it, a power cut does not. |
| `GET /api/log/live` | The log so far, and then each new line as it happens, until the caller closes it (also while a robot command runs). One listener at a time: a new one takes over. |
| `GET /api/crash` | The last crash (a core dump), as a file; `404` when there is none. It stays until the next crash. After a crash the log names where it happened. Each release carries the firmware's `.elf`, which turns the addresses into code (`esp-coredump` from `pip install esp-coredump`, version and board as on the buttons). |

```bash
curl -N -H "Authorization: Bearer 1234" http://scraper-control-1a2b.local/api/log/live
curl -o crash.bin -H "Authorization: Bearer 1234" http://scraper-control-1a2b.local/api/crash
esp-coredump info_corefile -t raw -c crash.bin scraper-control-m5stack-core-1.6.211.elf
```

## Answers

JSON, the same for every call but the log, in the apps' generic terms (any robot type):

```json
{"ok": true, "fault": null, "message": "", "age": 4,
 "status": {"planning": true, "nextRun": "15:00", "batteryMv": 25300, "activity": "",
            "route": "", "busy": false, "driving": false, "finishQuestion": false}}
```

| Field | What |
|---|---|
| `ok` | The robot did it |
| `fault` | Why not: `NotFound` (robot not reachable), `AuthRejected` (the robot refused the PIN), `Busy` (not ready, nothing was pressed), `NoConfirmation` (sent, but the robot's confirmation was not seen), `Failed` |
| `message` | What happened, in words |
| `age` | Seconds since `status` was read, `null` before the first read |
| `status` | The robot as last seen, `null` before the first read: `planning` on/off (`null` unknown), `nextRun` as the robot shows it, `batteryMv` (`null` unknown), `activity` and `route` while driving, `busy` (not ready for a command), `driving` (a route runs), `finishQuestion` (waiting for `/api/answer`) |

HTTP `200` when the robot did it, `502` when it did not, `401`/`429` see Access, `404` for an
unknown call.

## Examples

```bash
curl -H "Authorization: Bearer 1234" http://scraper-control-1a2b.local/api/status/fresh
curl -X POST -H "Authorization: Bearer 1234" http://scraper-control-1a2b.local/api/route/A
curl -X POST -H "Authorization: Bearer 1234" http://scraper-control-1a2b.local/api/planning/off
```
