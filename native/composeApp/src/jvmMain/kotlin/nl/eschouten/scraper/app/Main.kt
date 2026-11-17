// The desktop app (macOS, Windows, Linux). Bluetooth through Kable (btleplug).
//   ./gradlew :composeApp:run                         the real robot
//   ./gradlew :composeApp:run --args="--demo"          the in-app demo robot
//   ./gradlew :composeApp:run --args="--demo 'planning=off setup=done'"
//   SCRAPER_SIMULATOR=<simulator script> ./gradlew :composeApp:run   a simulated robot (see the driver)
package nl.eschouten.scraper.app

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.graphics.toComposeImageBitmap
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.type
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import nl.eschouten.scraper.api.ApiServer
import nl.eschouten.scraper.texts.tr
import nl.eschouten.scraper.ui.Confirm
import nl.eschouten.scraper.ui.Glyph
import nl.eschouten.scraper.ui.Platform
import kotlin.time.Duration.Companion.hours

object DesktopHost : AppHost {
  override val platform = Platform.Desktop
}

fun demoWords(args: Array<String>): String? {
  val i = args.indexOf("--demo")
  if (i >= 0) return args.getOrNull(i + 1)?.takeIf { !it.startsWith("--") } ?: ""
  return System.getenv("SCRAPER_DEMO")
}

// The window's icon (taskbar, window switcher; the Mac Dock uses the .app's icon.icns).
private fun appIcon(): BitmapPainter? =
  DesktopHost::class.java.getResourceAsStream("/icon.png")?.use {
    BitmapPainter(org.jetbrains.skia.Image.makeFromEncoded(it.readAllBytes()).toComposeImageBitmap())
  }

fun main(args: Array<String>) {
  DesktopUpdates.appRun(args)   // first: the installer's and updater's hooks run (and exit) here
  val icon = appIcon()
  val demo = demoWords(args)
  val controller = AppStart.controller(FileStore(), demo)
  ApiServer(controller).follow(CoroutineScope(Dispatchers.IO))   // Settings > Web page and API
  val nav = Nav()
  application {
    // A newer signed release (at start, then daily): asked, never installed by itself.
    var update by remember { mutableStateOf<DesktopUpdates.Update?>(null) }
    var failed by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()
    LaunchedEffect(Unit) {
      while (true) {
        DesktopUpdates.check()?.let { update = it }
        delay(24.hours)
      }
    }
    Window(
      onCloseRequest = {
        controller.dispose()
        exitApplication()
      },
      title = "Scraper Control",
      icon = icon,
      state = rememberWindowState(width = 420.dp, height = 820.dp),
      onPreviewKeyEvent = { e -> e.type == KeyEventType.KeyDown && e.key == Key.Escape && nav.back() },
    ) {
      App(controller, DesktopHost, nav)
      update?.let { u ->
        ConfirmDialog(Confirm(Glyph.Refresh, tr.updateAvailable(u.version), tr.updateBody, tr.updateInstall, tr.later), { update = null }) {
          update = null
          scope.launch {
            while (controller.state.value.isBusy) delay(500)   // never in the middle of a robot session
            failed = tr.updateFailed(DesktopUpdates.install(u) { controller.dispose() })
          }
        }
      }
      failed?.let { why ->
        AlertDialog({ failed = null }, confirmButton = { TextButton({ failed = null }) { Text(tr.close) } }, text = { Text(why) })
      }
    }
  }
}
