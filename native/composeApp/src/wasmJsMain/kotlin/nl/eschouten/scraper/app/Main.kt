// The web page: the app in a browser, on a phone or computer in the barn's network. A computer
// there runs the desktop app with Settings > Web page on: it serves this page and does the robot
// work (WebServer.kt); the page shows the same screens on that computer's controller (WebLink.kt).
package nl.eschouten.scraper.app

import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.ComposeViewport
import kotlinx.browser.document
import nl.eschouten.scraper.app.WebLink.Ask
import nl.eschouten.scraper.texts.tr
import nl.eschouten.scraper.ui.Action
import nl.eschouten.scraper.ui.ActionKind
import nl.eschouten.scraper.ui.Platform

@OptIn(ExperimentalComposeUiApi::class)
fun main() {
  val link = WebLink()
  val host = object : AppHost {
    override val platform = Platform.Desktop
    override val foreground = link.visible
  }
  ComposeViewport(document.body!!) {
    val controller by link.controller.collectAsState()
    val ask by link.ask.collectAsState()
    val c = controller
    val a = ask
    if (c != null) key(c) { App(c, host) }
    else AppTheme {
      Surface(Modifier.fillMaxSize()) {
        Column(
          Modifier.fillMaxSize().padding(Space.gutter),
          Arrangement.spacedBy(Space.l, Alignment.CenterVertically),
          Alignment.CenterHorizontally,
        ) {
          AppLogo(72.dp)
          if (a != null) PinForm(a, link::enter)
          else {
            CircularProgressIndicator()
            Text(tr.connectingBarn, color = MaterialTheme.colorScheme.onSurfaceVariant)
          }
        }
      }
    }
  }
}

@Composable
private fun PinForm(ask: Ask, onPin: (String) -> Unit) = Column(Modifier.widthIn(max = 360.dp), Arrangement.spacedBy(Space.l), Alignment.CenterHorizontally) {
  var pin by remember { mutableStateOf("") }
  Text(tr.webPinHelp, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
  OutlinedTextField(
    pin, { pin = it.filter(Char::isDigit) }, Modifier.fillMaxWidth(),
    label = { Text(tr.pin) },
    singleLine = true,
    isError = ask != Ask.Pin,
    supportingText = when (ask) {
      Ask.Pin -> null
      Ask.WrongPin -> ({ Text(tr.pinWrong) })
      Ask.TooMany -> ({ Text(tr.tooManyTries) })
    },
    visualTransformation = PasswordVisualTransformation(),
    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
  )
  ActionButton(Action(tr.connect, ActionKind.Confirm, "web-connect", enabled = pin.isNotEmpty())) { onPin(pin) }
}
