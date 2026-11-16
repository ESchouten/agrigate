// The web page's connection to the barn computer that serves it (WebServer.kt): kotlinx.rpc over
// a WebSocket, opened again when it drops. The access code is in the page's address.
package nl.eschouten.scraper.app

import io.ktor.client.HttpClient
import kotlinx.browser.document
import kotlinx.browser.window
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.job
import kotlinx.coroutines.launch
import kotlinx.rpc.krpc.ktor.client.installKrpc
import kotlinx.rpc.krpc.ktor.client.rpc
import kotlinx.rpc.krpc.ktor.client.rpcConfig
import kotlinx.rpc.krpc.serialization.json.json
import kotlinx.rpc.withService
import org.w3c.dom.events.Event

class WebLink {
  /** The barn computer's controller; null while there is no connection. */
  val controller: StateFlow<Controller?>

  /** True while the page is in view (the panel lets go of the robot when it is not). */
  val visible: StateFlow<Boolean> = MutableStateFlow(isVisible()).also { f ->
    document.addEventListener("visibilitychange", { _: Event -> f.value = isVisible() })
  }

  init {
    val c = MutableStateFlow<Controller?>(null)
    controller = c
    val http = HttpClient { installKrpc() }
    val url = with(window.location) { "${if (protocol == "https:") "wss" else "ws"}://$host/rpc/${search.drop(1)}" }
    MainScope().launch {
      while (true) {
        runCatching {
          coroutineScope {
            val rpc = http.rpc(url) { rpcConfig { serialization { json() } } }
            c.value = remoteController(rpc.withService<RobotApi>(), this)
            rpc.webSocketSession.await().coroutineContext.job.join()
            cancel()
          }
        }
        c.value = null
        delay(2_000)
      }
    }
  }
}

private fun isVisible(): Boolean = js("document.visibilityState === 'visible'")
