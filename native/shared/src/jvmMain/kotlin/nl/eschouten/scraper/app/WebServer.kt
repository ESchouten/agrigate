// The web page (Settings > Web page): this computer keeps the connection to the robot and serves
// the same app to browsers on the local network. The page (composeApp's wasmJs target, in the
// desktop app's resources) calls this computer's RobotController through kotlinx.rpc
// (Controller.kt; the page's end is WebLink.kt). A code in the address keeps other people out.
// Port 8080 unless chosen in Settings.
package nl.eschouten.scraper.app

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
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
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
import kotlin.random.Random
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
    val code = store.getString(CODE) ?: (1..6).joinToString("") { Random.nextInt(10).toString() }.also { store.putString(CODE, it) }
    _state.value = try {
      server = embeddedServer(CIO, port = port) { page(code) }.start(wait = false)
      WebPageState(on = true, port = port, address = "http://${lanAddress()}:$port/?$code")
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

  private fun Application.page(code: String) {
    // a page that drops without closing (a phone off the network) is noticed within seconds:
    // its held key and its hold on the robot connection end
    install(WebSockets) {
      pingPeriod = 1.seconds
      timeout = 3.seconds
    }
    install(Krpc)
    routing {
      rpc("/rpc/$code") {
        rpcConfig { serialization { json() } }
        val page = Page(controller)
        registerService<RobotApi> { page }
        closeReason.invokeOnCompletion { scope.launch { page.close() } }
      }
      files?.takeIf { it.isDirectory }?.let { staticFiles("/", it) }
    }
  }

  /** One page's calls. Its hold on the robot connection (the panel) ends with the page. */
  private class Page(private val c: RobotController) : RobotApi by c {
    private var holds = 0

    override suspend fun hold() = c.hold().also { if (it.ok) holds++ }

    override suspend fun release() {
      if (holds > 0) {
        holds--
        c.release()
      }
    }

    suspend fun close() = repeat(holds) { c.release() }
  }

  private companion object {
    const val ON = "web.on"
    const val PORT = "web.port"
    const val CODE = "web.code"

    /** This computer's address on the local network, as the other devices reach it. */
    fun lanAddress(): String =
      NetworkInterface.getNetworkInterfaces().asSequence()
        .filter { it.isUp && !it.isLoopback && !it.isVirtual }
        .flatMap { it.inetAddresses.asSequence() }
        .filterIsInstance<Inet4Address>()
        .firstOrNull { it.isSiteLocalAddress }?.hostAddress ?: "localhost"
  }
}
