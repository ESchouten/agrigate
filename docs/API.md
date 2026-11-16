# Web API

The buttons (M5Stack) and the desktop app answer the same small HTTP API on the local network,
so apps and other tools (Home Assistant, a script, Siri Shortcuts) can control a robot that is
out of their own Bluetooth range. The device that serves it holds the Bluetooth connection.

- **Buttons**: with a wifi network set they stay on it and serve on port 80, as
  `http://scraper-control.local` (a second one on the network: `scraper-control-2.local`). The
  address is also in Settings > Wifi.
- **Desktop app**: Settings > Web API, port 8080 by default.

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
| `GET /api/status` | The robot as last seen. `?fresh` reads it now. |
| `POST /api/route/<A-D>` | Start that route. While a route runs it stops it instead (a safety stop, like a route button), and the robot asks whether to finish the route. |
| `POST /api/planning/on`, `/off` | The schedule on or off. |
| `POST /api/answer/yes`, `/no` | The answer to "finish the route?" after a stop: yes drives on, no ends the route and leaves the schedule off. Never answered by itself. |

One call at a time: the device runs it to the end, connecting and disconnecting the robot, and
then answers.

## Answers

JSON, for every call:

```json
{"result": 0, "error": "", "age": 4, "status": {"planning": 1, "clock": "14:02", "nextRide": "15:00", "batteryMv": 25300, "busy": false, "driving": false, "stopQuestion": false, "...": "..."}}
```

| Field | What |
|---|---|
| `result` | `0` done, `1` no connection, `2` PIN rejected by the robot, `3` no screen, `4` busy (the robot is not on its home screen, nothing pressed), `5` unexpected screen (put back), `6` started without the expected answer (the same codes as `core/scraper_c.h`) |
| `error` | What went wrong, in words |
| `age` | Seconds since `status` was read, `null` before the first read |
| `status` | The robot's status as the core reports it (`scraper_status_json`), `null` before the first read. `planning`: 1 on, 0 off, -1 unknown; `driving`: a route runs; `stopQuestion`: the robot waits for `/api/answer` |

HTTP `200` when the robot did it, `502` when it did not (see `result`), `404` for an unknown
call.

## Examples

```bash
curl -H "Authorization: Bearer 1234" http://scraper-control.local/api/status?fresh
curl -X POST -H "Authorization: Bearer 1234" http://192.168.1.20/api/route/A
curl -X POST -H "Authorization: Bearer 1234" http://192.168.1.20/api/planning/off
```
