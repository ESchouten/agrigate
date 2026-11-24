// The desktop app (macOS, Windows, Linux). Bluetooth through Kable (btleplug).
//   ./gradlew :composeApp:run                         the real robot
//   ./gradlew :composeApp:run --args="--demo"          the in-app demo robot
//   ./gradlew :composeApp:run --args="--demo 'planning=off setup=done'"
//   AGRIGATE_SIMULATOR=<simulator script> ./gradlew :composeApp:run   a simulated robot (see the driver)
package com.eschouten.agrigate.clean.app

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
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
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import com.eschouten.agrigate.clean.api.ApiServer
import com.eschouten.agrigate.clean.texts.tr
import com.eschouten.agrigate.clean.ui.Confirm
import com.eschouten.agrigate.clean.ui.Glyph
import com.eschouten.agrigate.clean.ui.Platform
import kotlin.time.Duration.Companion.hours

object DesktopHost : AppHost {
  override val platform = Platform.Desktop
}

fun demoWords(args: Array<String>): String? {
  val i = args.indexOf("--demo")
  if (i >= 0) return args.getOrNull(i + 1)?.takeIf { !it.startsWith("--") } ?: ""
  return System.getenv("AGRIGATE_DEMO")
}

// The window's icon (taskbar, window switcher; the Mac Dock uses the .app's icon.icns).
private fun appIcon(): BitmapPainter? =
  DesktopHost::class.java.getResourceAsStream("/icon.png")?.use {
    BitmapPainter(org.jetbrains.skia.Image.makeFromEncoded(it.readAllBytes()).toComposeImageBitmap())
  }

// A newer signed release (looked for at start, then daily): asked, never installed by itself.
// The app closes for it only when no robot session is running.
@Composable
private fun UpdatePrompt(controller: RobotController) {
  var update by remember { mutableStateOf<DesktopUpdates.Update?>(null) }
  var installing by remember { mutableStateOf(false) }
  var failed by remember { mutableStateOf<String?>(null) }
  val scope = rememberCoroutineScope()
  LaunchedEffect(Unit) {
    while (true) {
      DesktopUpdates.check()?.let { update = it }
      delay(24.hours)
    }
  }
  val u = update ?: return
  val done = { update = null; failed = null }
  AppTheme {
    val why = failed
    when {
      why != null -> AlertDialog(done, confirmButton = { TextButton(done) { Text(tr.close) } }, text = { Text(why) })
      installing -> AlertDialog(
        {}, confirmButton = {}, title = { Text(tr.updateInstalling(u.version)) },
        text = { LinearProgressIndicator(modifier = Modifier.fillMaxWidth()) },
      )
      else -> ConfirmDialog(Confirm(Glyph.Refresh, tr.updateAvailable(u.version), tr.updateBody, tr.updateInstall, tr.later), done) {
        installing = true
        scope.launch {
          failed = tr.updateFailed(DesktopUpdates.install(u, idle = { controller.state.first { !it.isBusy } }) { controller.dispose() })
          installing = false
        }
      }
    }
  }
}

fun main(args: Array<String>) {
  DesktopUpdates.appRun(args)   // first: the installer's and updater's hooks run (and exit) here
  val icon = appIcon()
  val demo = demoWords(args)
  val controller = AppStart.controller(FileStore(), demo)
  ApiServer(controller).follow(CoroutineScope(Dispatchers.IO))   // Settings > Web page and API
  val nav = Nav()
  application {
    Window(
      onCloseRequest = {
        controller.dispose()
        exitApplication()
      },
      title = "Agrigate Clean",
      icon = icon,
      state = rememberWindowState(width = 420.dp, height = 820.dp),
      onPreviewKeyEvent = { e -> e.type == KeyEventType.KeyDown && e.key == Key.Escape && nav.back() },
    ) {
      App(controller, DesktopHost, nav)
      UpdatePrompt(controller)
    }
  }
}
