# Agrigate

Open software for the robots in the barn: one gate to all of them, whatever their make.

| | For | State |
|---|---|---|
| **Agrigate Clean** | Manure robots: routes, schedule, manual driving | Apps, buttons, Python |
| **Agrigate Milk** | Milking robots: state, alarms, figures (read only) | Python |
| **Agrigate Cow** | The herd from the farm's server, and which cow is at which robot (read only) | Python, farm service |
| **Agrigate Feed** | Feeding robots | Not yet |

## Agrigate Clean

Control your manure robot with three physical buttons, a phone or computer app, or a Python
script.

- **Buttons** (M5Stack): one route per button; hold for the schedule, manual driving and
  settings. Wifi is set up on the device itself, and it updates itself from the signed releases.
- **Apps** for Android, iOS and desktop: routes, schedule, live status, logbook, the robot's own
  panel, home-screen widgets, Siri/Shortcuts. Dutch and English.
- **Python**: the same robot API for scripts.
- **Web API**: the buttons and the desktop app answer a small HTTP API on the local network, for
  apps and tools out of Bluetooth range ([docs/API.md](docs/API.md)).
- **Web page**: the desktop app serves the same app to browsers on the local network, for a
  phone or computer without the app (Settings > Web page and API).
- **Safety**: a route button during a route stops it; a "finish the route?" question after a
  stop is always answered by a person; the schedule is left the way it was found.

<p>
  <img src="docs/screenshots/screenshots.png" width="620" alt="The app with the routes, the home-screen widget and the M5Stack buttons">
</p>

## Supported robots

| Robot | Kind | Connection | Driver |
|---|---|---|---|
| Lely Discovery | Manure scraper | Bluetooth LE, or classic Bluetooth on older robots (not on iOS) | [`drivers/lely`](drivers/lely) |
| Lely Astronaut | Milking robot and herd, read only | The farm's T4C server on the local network | [`drivers/t4c`](drivers/t4c/API.md) |
| Lely Astronaut A4 | Which cow is in the box, live (experimental) | The robot itself on the robot network (UDP) | [`drivers/lfw`](drivers/lfw/API.md) |
| Fullwood M²erlin | Milking robot, read only | The farm's FULLSENSE server on the local network | [`drivers/fullsense`](drivers/fullsense/API.md) |

Milking robots are new: their state, alarms and figures, the herd (numbers, lactation,
reproduction, inseminations) and which cow is at which robot right now can be read with Python
(`farm.py`), not yet in the apps or on the buttons. The **farm service** (`python/farm_server.py`)
offers it over HTTP to other programs on the farm, such as a camera system that wants to know which
cow it sees: [docs/FARM_API.md](docs/FARM_API.md). The `t4c` and `lfw` drivers were checked on a real
farm; the FULLSENSE driver has not met a real server yet (see each driver's page).

## Install

Everything is in the [latest release](https://github.com/ESchouten/scraper-control/releases/latest).

| Device | File | How |
|---|---|---|
| Android | `.apk` | Open it on the phone and allow installing from your browser or files app. A newer version installs over the old one; [Obtainium](https://github.com/ImranR98/Obtainium) with this repository keeps it up to date. |
| Windows | `.exe` | Not signed: at the blue warning choose More info > Run anyway. Installs for your user, no administrator needed. |
| macOS | `.pkg` | Not signed: right-click > Open the first time, then allow Bluetooth. |
| Linux | `.deb` | Add the apt repository once (below): `sudo apt upgrade` then keeps it up to date. Or just the file: `sudo apt install ./agrigate-clean_*.deb`. |
| iOS | none yet | Build it with Xcode ([native/README.md](native/README.md)). |
| Buttons | M5Stack Basic v2.7 (16 MB) | The first time with the [browser installer](https://eschouten.github.io/scraper-control/) (Chrome or Edge on a computer, USB-C data cable), or `esptool write-flash 0x0 scraper-control-m5stack-core-<version>-full.bin`. That erases the M5Stack; after that it updates itself over wifi. |

The apt repository (Debian, Ubuntu; amd64):

```bash
curl -fsSL https://eschouten.github.io/scraper-control/apt/scraper-control.gpg | sudo tee /usr/share/keyrings/scraper-control.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/scraper-control.gpg] https://eschouten.github.io/scraper-control/apt stable main" | sudo tee /etc/apt/sources.list.d/scraper-control.list
sudo apt update && sudo apt install agrigate-clean
```

On Windows and macOS the app checks for a new version at start and once a day and asks before
it installs one; it only takes a release signed with the release key, like the buttons. An app
that is still called Scraper Control does not update itself to Agrigate Clean: install Agrigate
Clean once and remove Scraper Control (on Linux: `sudo apt install agrigate-clean && sudo apt
remove scraper-control`). On Android and iOS it is a new app as well; add the robot again.

### First connection

1. The robot takes one Bluetooth connection at a time: close any other app that is connected
   to it.
2. **App**: it searches for the robot by itself and asks for the robot's PIN. The very first
   connection syncs with the robot, about 1.5 minutes, once.
3. **Buttons**: hold the right button for the settings: PIN, Robot (pick it from a search) and
   Wifi (a network or WPS). With wifi the buttons look for updates by themselves and serve the
   [web API](docs/API.md).

### The buttons

| | Tap | Hold |
|---|---|---|
| Left | Route A | Schedule on / off |
| Middle | Route B | Drive by hand |
| Right | Route C | Settings |

Driving by hand: tap left / middle / right = left motor, direction, right motor; hold middle =
drive; hold right = onto the charger (choose its side with left / right, hold middle = Start);
hold left = done.

## How it fits together

| Folder | What |
|---|---|
| `core/` | The robot APIs, one per kind of robot and the same for every make: `clean_c.h` (manure scrapers), `robot_c.h` with `milk_c.h` and `cow_c.h` (read through the farm's server), `live_c.h` (what a robot says on the farm network) |
| `drivers/` | One folder per robot type or server software: everything specific to it |
| `firmware/` | The M5Stack buttons (PlatformIO) |
| `native/` | The apps: shared Kotlin, Compose for Android and desktop, SwiftUI for iOS ([README](native/README.md)) |
| `python/` | The Python library `agrigate` and the command lines `clean.py`, `farm.py` and `farm_server.py` |
| `tools/` | Build secrets |

Every client only provides the link to the robot (bytes in, bytes out, or a request and its
answer); the driver says which kind of link it needs. A new robot type is a new folder in
`drivers/`, and a new kind of robot one more API in `core/`: [docs/DRIVERS.md](docs/DRIVERS.md).

## Build

```bash
python3 tools/secrets.py                          # once: build secrets from ~/.scraper-control/secrets
cd firmware && pio run -e m5stack-core -t upload  # the buttons (USB the first time)
cd native && ./gradlew :composeApp:run            # the desktop app (see native/README.md)
cd python && pip install -e . && python clean.py --scan
```

Without build secrets everything builds and the tests pass, but a real robot may refuse to
connect.

## Tests

```bash
python3 tests/test_firmware.py                    # the buttons' firmware on a computer
for t in drivers/*/tests/test_*.py; do python3 $t; done  # each driver against its simulated robot or server
python3 tests/test_farm_server.py                 # the farm service with made-up robots and herd
cd native && ./gradlew :shared:jvmTest :composeApp:jvmTest
```

GitHub Actions runs them on every push. A release (Actions > release) puts the signed firmware,
its first-install image, the Android APK and the desktop apps in one GitHub release, and the
buttons' browser installer (`docs/install`) on GitHub Pages; the buttons install it themselves.

## Licence and disclaimer

GNU AGPL v3 (`LICENSE`). Copyright (C) 2026 Erik Schouten.

Unofficial software: not made, endorsed or supported by the makers of the robots it controls.
Brand and product names belong to their owners. Use at your own risk and keep the robot in sight.
