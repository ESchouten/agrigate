// The web page's connection to the barn computer that serves it (WebServer.kt): kotlinx.rpc over
// a WebSocket, opened again when it drops. The PIN of one of the robots lets it in; the browser
// remembers it.
package nl.eschouten.scraper.app

import io.ktor.client.HttpClient
import io.ktor.client.request.get
import io.ktor.http.HttpStatusCode
import kotlinx.browser.document
import kotlinx.browser.localStorage
import kotlinx.browser.window
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
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
  /** Why the page asks for a PIN; null while it does not. */
  enum class Ask { Pin, WrongPin, TooMany }

  /** The barn computer's controller; null while there is no connection. */
  val controller: StateFlow<Controller?>
  val ask: StateFlow<Ask?>

  /** True while the page is in view (the panel lets go of the robot when it is not). */
  val visible: StateFlow<Boolean> = MutableStateFlow(isVisible()).also { f ->
    document.addEventListener("visibilitychange", { _: Event -> f.value = isVisible() })
  }

  private val typed = Channel<String>(Channel.CONFLATED)

  /** The PIN the user typed. */
  fun enter(pin: String) {
    typed.trySend(pin.trim())
  }

  init {
    val c = MutableStateFlow<Controller?>(null)
    val a = MutableStateFlow<Ask?>(null)
    controller = c
    ask = a
    val http = HttpClient { installKrpc() }
    val ws = with(window.location) { "${if (protocol == "https:") "wss" else "ws"}://$host/rpc" }
    MainScope().launch {
      var pin = localStorage.getItem(PIN) ?: ""
      while (true) {
        when (val st = runCatching { http.get("/pin?pin=$pin").status }.getOrNull()) {
          HttpStatusCode.OK -> {
            localStorage.setItem(PIN, pin)
            runCatching {
              coroutineScope {
                val rpc = http.rpc("$ws?pin=$pin") { rpcConfig { serialization { json() } } }
                c.value = remoteController(rpc.withService<RobotApi>(), this)
                rpc.webSocketSession.await().coroutineContext.job.join()
                cancel()
              }
            }
            c.value = null
            delay(2_000)
          }
          HttpStatusCode.Forbidden, HttpStatusCode.TooManyRequests -> {
            localStorage.removeItem(PIN)
            a.value = when {
              st == HttpStatusCode.TooManyRequests -> Ask.TooMany
              pin.isEmpty() -> Ask.Pin
              else -> Ask.WrongPin
            }
            pin = typed.receive()
            a.value = null
          }
          else -> delay(2_000) // the barn computer cannot be reached
        }
      }
    }
  }

  private companion object {
    const val PIN = "scraper.pin"
  }
}

private fun isVisible(): Boolean = js("document.visibilityState === 'visible'")
