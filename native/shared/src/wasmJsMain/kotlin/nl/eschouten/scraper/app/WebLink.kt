// The web page's connection to the barn computer that serves it (the desktop app's web page):
// a WebSocket with the access code, opened again when it drops. The page draws [controller].
package nl.eschouten.scraper.app

import kotlinx.browser.document
import kotlinx.browser.localStorage
import kotlinx.browser.window
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import org.w3c.dom.CloseEvent
import org.w3c.dom.MessageEvent
import org.w3c.dom.WebSocket
import org.w3c.dom.events.Event

class WebLink {
  enum class Status { Connecting, Open, NeedCode, WrongCode }

  private val scope = MainScope()
  private var socket: WebSocket? = null
  private val _status = MutableStateFlow(Status.NeedCode)
  val status: StateFlow<Status> = _status

  val controller = RemoteController({ socket?.send(it) }, scope)

  /** True while the page is in view (the panel lets go of the robot when it is not). */
  val visible: StateFlow<Boolean> = MutableStateFlow(isVisible()).also { f ->
    document.addEventListener("visibilitychange", { _: Event -> f.value = isVisible() })
  }

  init {
    localStorage.getItem(CODE)?.let { open(it) }
  }

  /** The code the user typed. */
  fun start(code: String) {
    localStorage.setItem(CODE, code.trim())
    open(code.trim())
  }

  private fun open(code: String) {
    _status.value = Status.Connecting
    val scheme = if (window.location.protocol == "https:") "wss" else "ws"
    val ws = WebSocket("$scheme://${window.location.host}/ws?code=$code")
    socket = ws
    ws.onopen = { _: Event -> _status.value = Status.Open }
    ws.onmessage = { e: MessageEvent -> e.data?.toString()?.let(controller::receive) }
    ws.onclose = { e: Event ->
      if (socket === ws) {
        socket = null
        controller.closed()
        if ((e as CloseEvent).code.toInt() == WRONG_CODE) {
          localStorage.removeItem(CODE)
          _status.value = Status.WrongCode
        } else {
          _status.value = Status.Connecting
          scope.launch {
            delay(2_000)
            open(code)
          }
        }
      }
    }
  }

  companion object {
    private const val CODE = "scraper.code"

    /** The barn computer closes the connection with this when the code is wrong (WebSocket "policy violation"). */
    const val WRONG_CODE = 1008
  }
}

private fun isVisible(): Boolean = js("document.visibilityState === 'visible'")
