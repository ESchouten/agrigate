// The web page: the app in a browser, on a phone or computer in the barn's network. A computer
// there runs the desktop app with Settings > Web page on: it serves this page and does the robot
// work (WebServer.kt); the page shows the same screens (WebLink.kt, app/Remote.kt).
package nl.eschouten.scraper.app

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.ComposeViewport
import kotlinx.browser.document
import kotlinx.coroutines.flow.StateFlow
import nl.eschouten.scraper.app.WebLink.Status
import nl.eschouten.scraper.texts.tr
import nl.eschouten.scraper.ui.Action
import nl.eschouten.scraper.ui.ActionKind
import nl.eschouten.scraper.ui.Platform

private class WebHost(link: WebLink) : AppHost {
  override val platform = Platform.Desktop
  override val foreground: StateFlow<Boolean> = link.visible
}

@OptIn(ExperimentalComposeUiApi::class)
fun main() {
  val link = WebLink()
  val host = WebHost(link)
  ComposeViewport(document.body!!) {
    val status by link.status.collectAsState()
    val ready by link.controller.ready.collectAsState()
    when {
      status == Status.NeedCode || status == Status.WrongCode -> Page { CodeForm(status == Status.WrongCode, link::start) }
      !ready -> Page { Waiting() }
      else -> Box(Modifier.fillMaxSize()) {
        App(link.controller, host)
        // the barn computer is gone for a moment: the screens stay, a bar says so
        if (status == Status.Connecting) {
          Surface(Modifier.fillMaxWidth().align(Alignment.TopCenter), color = MaterialTheme.colorScheme.inverseSurface) {
            Text(tr.barnComputerGone, Modifier.padding(12.dp), color = MaterialTheme.colorScheme.inverseOnSurface, textAlign = TextAlign.Center)
          }
        }
      }
    }
  }
}

@Composable
private fun Page(content: @Composable () -> Unit) = AppTheme {
  Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.surface) {
    Box(Modifier.fillMaxSize().padding(Space.gutter), contentAlignment = Alignment.Center) {
      Column(Modifier.widthIn(max = 360.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(Space.l)) {
        AppLogo(72.dp)
        content()
      }
    }
  }
}

@Composable
private fun Waiting() {
  CircularProgressIndicator()
  Text(tr.connectingBarn, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
private fun CodeForm(wrong: Boolean, onCode: (String) -> Unit) {
  var code by remember { mutableStateOf("") }
  Text(tr.accessCode, fontSize = 22.sp, fontWeight = FontWeight.ExtraBold)
  Text(tr.accessCodeHelp, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
  OutlinedTextField(
    code, { code = it.filter(Char::isDigit) }, Modifier.fillMaxWidth(),
    singleLine = true,
    isError = wrong,
    supportingText = if (wrong) ({ Text(tr.wrongCode) }) else null,
    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
  )
  ActionButton(Action(tr.connect, ActionKind.Confirm, "web-connect", enabled = code.isNotEmpty())) { onCode(code) }
}
