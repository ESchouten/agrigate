// The web page (Settings > Web page): this computer keeps the connection to the robot and serves
// the same app to browsers on the local network (the page's half: app/Remote.kt, WebLink.kt).
// The page comes with the desktop app (build.gradle.kts); a code keeps other people out.
package nl.eschouten.scraper.app

import io.ktor.http.ContentType
import io.ktor.server.application.Application
import io.ktor.server.application.install
import io.ktor.server.cio.CIO
import io.ktor.server.engine.EmbeddedServer
import io.ktor.server.engine.embeddedServer
import io.ktor.server.http.content.staticFiles
import io.ktor.server.routing.routing
import io.ktor.server.websocket.WebSockets
import io.ktor.server.websocket.pingPeriod
import io.ktor.server.websocket.timeout
import io.ktor.server.websocket.webSocket
import io.ktor.websocket.CloseReason
import io.ktor.websocket.Frame
import io.ktor.websocket.close
import io.ktor.websocket.readText
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import nl.eschouten.scraper.ui.WebPageState
import java.io.File
import java.net.Inet4Address
import java.net.NetworkInterface
import kotlin.random.Random
import kotlin.time.Duration.Companion.seconds

class WebServer(private val controller: RobotController, private val store: Store) : WebPageHost {
  private val _state = MutableStateFlow(WebPageState())
  override val state: StateFlow<WebPageState> = _state
  private var server: EmbeddedServer<*, *>? = null

  /** Pages' commands run here, so one that is under way finishes even when its page goes away. */
  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

  init {
    if (store.getString(ON) == "1") switch(true)
  }

  override fun switch(on: Boolean) {
    stop()
    store.putString(ON, if (on) "1" else null)
    if (!on) {
      _state.value = WebPageState()
      return
    }
    val code = store.getString(CODE) ?: (1..6).joinToString("") { Random.nextInt(10).toString() }.also { store.putString(CODE, it) }
    _state.value = try {
      server = embeddedServer(CIO, port = PORT) { page(code) }.start(wait = false)
      WebPageState(on = true, address = "http://${lanAddress()}:$PORT", code = code)
    } catch (e: Exception) {
      store.putString(ON, null)
      WebPageState(error = e.message ?: e.toString())
    }
  }

  fun stop() {
    server?.stop(500, 1_000)
    server = null
  }

  private fun Application.page(code: String) {
    // a page that drops without closing (a phone off the network) is noticed within seconds
    install(WebSockets) {
      pingPeriod = 2.seconds
      timeout = 5.seconds
    }
    routing {
      webSocket("/ws") {
        if (call.request.queryParameters["code"] != code) {
          close(CloseReason(CloseReason.Codes.VIOLATED_POLICY, "code"))
          return@webSocket
        }
        val host = RemoteHost(controller, { send(Frame.Text(it)) }, scope)
        try {
          for (f in incoming) if (f is Frame.Text) host.receive(f.readText())
        } finally {
          withContext(NonCancellable) { host.close() }
        }
      }
      pageFiles()?.let { dir ->
        staticFiles("/", dir) {
          contentType { f ->
            when (f.extension) {
              "wasm" -> ContentType("application", "wasm")
              "mjs" -> ContentType.Text.JavaScript
              else -> null
            }
          }
        }
      }
    }
  }

  private companion object {
    const val PORT = 8080
    const val ON = "web.on"
    const val CODE = "web.code"

    /** The page's files, in the app's resources (compose.application.resources.dir). */
    fun pageFiles(): File? =
      System.getProperty("compose.application.resources.dir")?.let { File(it, "web") }?.takeIf { it.isDirectory }

    /** This computer's address on the local network, as the other devices reach it. */
    fun lanAddress(): String =
      NetworkInterface.getNetworkInterfaces().asSequence()
        .filter { it.isUp && !it.isLoopback && !it.isVirtual }
        .flatMap { it.inetAddresses.asSequence() }
        .filterIsInstance<Inet4Address>()
        .firstOrNull { it.isSiteLocalAddress }?.hostAddress ?: "localhost"
  }
}
