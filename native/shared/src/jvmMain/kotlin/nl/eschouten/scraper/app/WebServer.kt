// The web page (Settings > Web page): this computer keeps the connection to the robot and serves
// the same app to browsers on the local network. The page (composeApp's wasmJs target, in the
// desktop app's resources) calls this computer's RobotController through kotlinx.rpc
// (Controller.kt; the page's end is WebLink.kt). The PIN of one of the robots lets a page in: no
// safer than the robot itself, which takes that PIN from anyone with the maker's app. The page
// never sees the PINs. Port 8080 unless chosen in Settings.
package nl.eschouten.scraper.app

import io.ktor.http.HttpStatusCode
import io.ktor.server.application.Application
import io.ktor.server.application.ApplicationCall
import io.ktor.server.application.ApplicationCallPipeline
import io.ktor.server.application.install
import io.ktor.server.cio.CIO
import io.ktor.server.engine.EmbeddedServer
import io.ktor.server.engine.embeddedServer
import io.ktor.server.http.content.staticFiles
import io.ktor.server.response.respond
import io.ktor.server.routing.get
import io.ktor.server.routing.route
import io.ktor.server.routing.routing
import io.ktor.server.websocket.WebSockets
import io.ktor.server.websocket.pingPeriod
import io.ktor.server.websocket.timeout
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.rpc.krpc.ktor.server.Krpc
import kotlinx.rpc.krpc.ktor.server.rpc
import kotlinx.rpc.krpc.serialization.json.json
import nl.eschouten.scraper.ui.DEFAULT_WEB_PORT
import nl.eschouten.scraper.ui.WebPageHost
import nl.eschouten.scraper.ui.WebPageState
import java.io.File
import java.net.Inet4Address
import java.net.NetworkInterface
import kotlin.time.Duration.Companion.seconds

/** [files]: the page, by default from the app's resources (compose.application.resources.dir). */
class WebServer(
  private val controller: RobotController,
  private val store: Store,
  private val files: File? = System.getProperty("compose.application.resources.dir")?.let { File(it, "web") },
) : WebPageHost {
  private val _state = MutableStateFlow(WebPageState(port = port()))
  override val state: StateFlow<WebPageState> = _state
  private var server: EmbeddedServer<*, *>? = null
  private val scope = CoroutineScope(Dispatchers.Default)

  init {
    if (store.getString(ON) == "1") switch(true)
  }

  override fun switch(on: Boolean) {
    stop()
    store.putString(ON, if (on) "1" else null)
    val port = port()
    if (!on) {
      _state.value = WebPageState(port = port)
      return
    }
    _state.value = try {
      server = embeddedServer(CIO, port = port) { page() }.start(wait = false)
      WebPageState(on = true, port = port, address = "http://${lanAddress()}:$port")
    } catch (e: Exception) {
      store.putString(ON, null)
      WebPageState(port = port, error = e.message ?: e.toString())
    }
  }

  override fun setPort(port: Int) {
    store.putString(PORT, if (port == DEFAULT_WEB_PORT) null else port.toString())
    switch(_state.value.on)
  }

  private fun port() = store.getString(PORT)?.toIntOrNull() ?: DEFAULT_WEB_PORT

  fun stop() {
    server?.stop(500, 1_000)
    server = null
  }

  private fun Application.page() {
    // a page that drops without closing (a phone off the network) is noticed within seconds:
    // its held key and its hold on the robot connection end
    install(WebSockets) {
      pingPeriod = 1.seconds
      timeout = 3.seconds
    }
    install(Krpc)
    routing {
      // so the page can tell a wrong PIN from a barn computer that is off
      get("/pin") { call.respond(pin(call)) }
      route("/rpc") {
        intercept(ApplicationCallPipeline.Plugins) {
          val p = pin(context)
          if (p != HttpStatusCode.OK) {
            context.respond(p)
            finish()
          }
        }
        rpc {
          rpcConfig { serialization { json() } }
          val page = Page(controller)
          registerService<RobotApi> { page }
          closeReason.invokeOnCompletion { scope.launch { page.close() } }
        }
      }
      files?.takeIf { it.isDirectory }?.let { staticFiles("/", it) }
    }
  }

  private var wrong = 0
  private var lockedUntil = 0L

  /** Whether `?pin=` is one of the robots' PINs (any, when none has one). After 5 wrong tries
   *  every try is refused for a minute, so the short PINs cannot be guessed. */
  @Synchronized private fun pin(call: ApplicationCall): HttpStatusCode {
    val now = System.currentTimeMillis()
    if (now < lockedUntil) return HttpStatusCode.TooManyRequests
    val pins = controller.state.value.robots.map { it.pin }.filter { it.isNotEmpty() }
    if (pins.isEmpty() || call.request.queryParameters["pin"] in pins) {
      wrong = 0
      return HttpStatusCode.OK
    }
    if (++wrong >= 5) {
      wrong = 0
      lockedUntil = now + 60_000
    }
    return HttpStatusCode.Forbidden
  }

  /** One page's calls. Its hold on the robot connection (the panel) ends with the page. It gets
   *  the robots without their PINs ([HIDDEN]), and a robot sent back with one keeps its PIN. */
  private class Page(private val c: RobotController) : RobotApi by c {
    private var holds = 0

    override fun states() = c.state.map { s -> s.copy(robots = s.robots.map(::hide), robot = hide(s.robot)) }

    override suspend fun updateRobot(p: RobotProfile) =
      c.updateRobot(if (p.pin == HIDDEN) p.copy(pin = c.state.value.robots.firstOrNull { it.uid == p.uid }?.pin ?: "") else p)

    override suspend fun hold() = c.hold().also { if (it.ok) holds++ }

    override suspend fun release() {
      if (holds > 0) {
        holds--
        c.release()
      }
    }

    suspend fun close() = repeat(holds) { c.release() }

    private fun hide(p: RobotProfile) = if (p.pin.isEmpty()) p else p.copy(pin = HIDDEN)
  }

  private companion object {
    const val ON = "web.on"
    const val PORT = "web.port"
    const val HIDDEN = "••••"

    /** This computer's address on the local network, as the other devices reach it. */
    fun lanAddress(): String =
      NetworkInterface.getNetworkInterfaces().asSequence()
        .filter { it.isUp && !it.isLoopback && !it.isVirtual }
        .flatMap { it.inetAddresses.asSequence() }
        .filterIsInstance<Inet4Address>()
        .firstOrNull { it.isSiteLocalAddress }?.hostAddress ?: "localhost"
  }
}
