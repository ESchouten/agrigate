// The web page: the app in a browser, on a phone or computer in the barn's network. A computer
// there runs the desktop app with Settings > Web page and API on: it serves this page next to
// its web API (api/ApiServer) and holds the Bluetooth connection. The page's one robot goes
// through that computer (api/ApiSession), like a robot "via" another device in the apps.
package nl.eschouten.scraper.app

import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.window.ComposeViewport
import kotlinx.browser.document
import kotlinx.browser.localStorage
import kotlinx.browser.window
import kotlinx.coroutines.flow.MutableStateFlow
import nl.eschouten.scraper.ui.Platform
import org.w3c.dom.events.Event

/** The settings and the logbook stay in this browser. */
private object BrowserStore : Store {
  override fun getString(key: String): String? = localStorage.getItem("scraper.$key")
  override fun putString(key: String, value: String?) {
    if (value == null) localStorage.removeItem("scraper.$key") else localStorage.setItem("scraper.$key", value)
  }
}

@OptIn(ExperimentalComposeUiApi::class)
fun main() {
  val settings = Settings.load(BrowserStore)
  settings.updateRobot { it.copy(via = window.location.host) }   // where this page came from
  val controller = RobotController(settings, BrowserStore)
  val host = object : AppHost {
    override val platform = Platform.Web
    // a tab in the background lets the panel's connection go, like an app in the background
    override val foreground = MutableStateFlow(isVisible()).also { f ->
      document.addEventListener("visibilitychange", { _: Event -> f.value = isVisible() })
    }
  }
  ComposeViewport(document.body!!) { App(controller, host) }
}

private fun isVisible(): Boolean = js("document.visibilityState === 'visible'")
