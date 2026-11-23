# Agrigate Clean: native apps

The apps. One shared Kotlin module with all behaviour, and
a native UI per platform that only draws what the shared module says:

| | UI | Links | Robot driver (C++) |
|---|---|---|---|
| Android | Jetpack Compose (`composeApp`, `androidApp`) | Kable | JNI (`../core`, built by the NDK) |
| Desktop (macOS, Windows, Linux) | Compose Desktop (`composeApp`) | Kable (btleplug) | JNI (built by CMake) |
| iOS | SwiftUI (`iosApp`) | Kable (CoreBluetooth) | cinterop (`libcore.a`) |
| Web page | Compose for Web, wasm (`composeApp`) | none: the web API of the desktop app that serves it | none |

The driver is chosen in `gradle.properties` (`scraper.driver`), its Kotlin side is listed in
`shared/.../model/Registry.kt`.

```
shared/      Kotlin Multiplatform: the robot API (core/), links (link/), robot controller,
             settings, logbook, texts (NL/EN), screen models (ui/), widget data, and the
             drivers' Kotlin side (model, own screens, demo robot)
composeApp/  Compose screens (Android, desktop, web page) and the scenario tests
androidApp/  Android app: activity, widgets, the NDK build of the core
iosApp/      SwiftUI app, WidgetKit widgets, Siri/Shortcuts intent, XCUITests (XcodeGen)
scenarios/   the user scenarios both apps must pass, and the checks around them
```

## How both apps stay the same

- **Screens decide nothing.** `shared/.../ui/` has a model per screen (what it shows, which
  buttons, which test id) and a flow per multi-step screen (`RunFlow`, `PanelFlow`,
  `OnboardingFlow`, `AddRobotFlow`, `ScanFlow`). Taps that change something go through
  `ui/Actions.kt`. Compose and SwiftUI only lay these out.
- **One set of texts**, `shared/.../texts/Texts.kt` (Dutch and English). Both apps read
  `tr` (Swift: `TextsKt.tr`).
- **Shared tests** in `shared/src/jvmTest`: the driver, the controller, the screen models and
  the widgets, against the demo robot and the driver's simulator.
- **One scenario list**, `scenarios/scenarios.json`. The Compose app runs it in
  `composeApp/src/jvmTest/.../ScenarioTest.kt`, the iOS app in
  `iosApp/UITests/ScenarioTests.swift`, with the same test ids on the same elements and the
  same demo robot. `ScenarioCoverageTest` (composeApp) fails when either app misses one.
- **Screenshots** of the same screens in both languages from both test suites.
- A feature is done when it is in both apps and in the scenario list.

## Build and run

Needs JDK 17; Android needs the Android SDK (`ANDROID_HOME`), iOS needs Xcode and
`brew install xcodegen`. Without an Android SDK the Android parts are skipped.

```sh
./gradlew :shared:jvmTest                 # shared tests (needs python3 with cryptography for the simulator)
python3 ../tools/secrets.py               # optional: the build secrets (a real robot needs them)
./gradlew :composeApp:run                 # desktop app, real robot over Bluetooth
./gradlew :composeApp:run --args="--demo" # desktop app with the demo robot
./gradlew :composeApp:jvmTest             # scenarios + screenshots (build/screenshots/<lang>)
./gradlew :androidApp:assembleDebug       # Android APK
cd iosApp && xcodegen && open ScraperControl.xcodeproj   # iOS app
iosApp/run-ui-tests.sh                    # iOS scenarios + screenshots (/tmp/shots/<lang>)
```

## The web page

The desktop app with **Settings > Web page and API** on serves the app itself to browsers on the
local network, next to its [web API](../docs/API.md) (`shared/.../api/ApiServer.kt`). The page
is the same Compose app built for wasm, with its own `RobotController` in the browser; its one
robot goes through the computer the page came from (`api/ApiSession.kt`), exactly like a robot
"through another device" in the apps. Its settings and logbook stay in that browser.

```sh
./gradlew :composeApp:run --args="--demo 'setup=done api=on'"   # the desktop app serving the page on :8080
./gradlew :composeApp:wasmJsBrowserDistribution  # only the page (build/dist/wasmJs/productionExecutable)
cd scenarios && node web.mjs                      # the web API and the page in a browser, against that app
```

## The demo robot

Each driver can bring an in-app robot (`ScraperModel.installDemo`): the real driver talks to
it as to a robot, so the whole app works without Bluetooth. Start it with words
(`app/AppStart.kt`):

```
planning=on|off  mode=home|menu|manual|driving|question|abort  lang=nl|en (robot)
delay=<ms per answer>  scan=<ms>  setup=done (skip onboarding)  ui=nl|en (app)
routes=ABC  names=A:Voergang,B:Stal+2
```

Desktop: `--demo "<words>"` or `SCRAPER_DEMO`. Android: `adb shell am start -n
nl.eschouten.scraper/.MainActivity --es demo "setup=done"`. iOS: environment `SCRAPER_DEMO`
or launch argument `-demo "<words>"`. The demo PIN is 4321.

## Widgets

`shared/.../app/WidgetBridge.kt` holds what the widgets show and what a tap does (connect,
start the route with every safety rule, disconnect). Android: `RoutesWidget` (A-D) and
`RouteTileWidget` (one route), tapped in the background. iOS 17+: the same two widgets in
WidgetKit and `RouteIntent` for Siri and Shortcuts. The app and the widgets share the App
Group `group.nl.eschouten.scraper`.

## CI

`.github/workflows/ci.yml`: on every push that changes the apps the scenario check, the shared tests, the
desktop app, the Compose scenarios with screenshots, and the Android APK. iOS (macOS
runner, minutes count 10x) once a day when `native/`, `core/` or `drivers/` changed, and by hand.
