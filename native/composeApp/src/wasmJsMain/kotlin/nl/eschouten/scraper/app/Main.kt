// The web page: the app in a browser, on a phone or computer in the barn's network. A computer
// there runs the desktop app with Settings > Web page on: it serves this page and does the robot
// work (WebServer.kt); the page shows the same screens on that computer's controller (WebLink.kt).
package nl.eschouten.scraper.app

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.ComposeViewport
import kotlinx.browser.document
import nl.eschouten.scraper.texts.tr
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
    val c = controller
    if (c != null) key(c) { App(c, host) }
    else AppTheme {
      Surface(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().padding(Space.gutter), Arrangement.spacedBy(Space.l, Alignment.CenterVertically), Alignment.CenterHorizontally) {
          AppLogo(72.dp)
          CircularProgressIndicator()
          Text(tr.connectingBarn, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        }
      }
    }
  }
}
