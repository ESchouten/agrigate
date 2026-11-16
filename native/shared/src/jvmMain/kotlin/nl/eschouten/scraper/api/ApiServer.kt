// The desktop app serves the web API (docs/API.md) for the robot in use, when Settings > Web page
// and API is on: the JDK's own HTTP server, and http://scraper-control-1a2b.local:<port> by mDNS
// (JmDNS), because many networks do not resolve a computer's own name. The name is fixed per
// computer (the end of its network address), so two devices never swap names (Erik 2026-11-16).
// The same address in a browser is the web page: the app itself (composeApp's wasmJs build, in
// the desktop app's resources), whose robot goes through this computer's web API like any app's.
package nl.eschouten.scraper.api

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import nl.eschouten.scraper.app.RobotController
import java.io.File
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import javax.jmdns.JmDNS
import javax.jmdns.ServiceInfo

/** [page]: the web page's files, by default from the app's resources (compose.application.resources.dir). */
class ApiServer(
  private val controller: RobotController,
  private val page: File? = System.getProperty("compose.application.resources.dir")?.let { File(it, "web") },
) {
  private val calls = ApiCalls(controller) { System.currentTimeMillis() }
  private var server: HttpServer? = null
  private var mdns: JmDNS? = null
  private var via = ""   // how a phone reaches this computer, e.g. "scraper-control-1a2b.local:8080"

  /** Runs and stops with the setting, for as long as [scope] lives. */
  fun follow(scope: CoroutineScope) {
    scope.launch {
      controller.state.map { it.apiOn to it.apiPort }.distinctUntilChanged().collect { (on, port) ->
        stop()
        if (on) start(port)
        else WebApiHost.mutable.value = null
      }
    }
    scope.launch {   // the QR code carries the PIN of the robot in use
      controller.state.map { it.robot.pin }.distinctUntilChanged().collect { showQr() }
    }
  }

  @Synchronized private fun showQr() {
    val st = WebApiHost.mutable.value ?: return
    if (via.isEmpty()) return
    val link = "scraper-control://add?via=$via&pin=${controller.state.value.robot.pin}"
    val m = QRCodeWriter().encode(link, BarcodeFormat.QR_CODE, 0, 0, mapOf(EncodeHintType.MARGIN to 0))
    WebApiHost.mutable.value = st.copy(qr = (0 until m.height).map { y -> BooleanArray(m.width) { x -> m.get(x, y) } })
  }

  /** Serves on [port] (0: any free one); the port it got. */
  fun start(port: Int): Int {
    val s = try {
      HttpServer.create(InetSocketAddress(port), 0)
    } catch (e: Exception) {
      WebApiHost.mutable.value = WebApiState(error = e.message ?: e.toString())
      return 0
    }
    s.createContext("/") { ex ->
      ex.use {
        val file = pageFile(ex.requestURI.path).takeIf { ex.requestMethod == "GET" }
        val (code, body) = if (file != null) 200 to file.readBytes() else runBlocking {
          calls.handle(ex.requestMethod, ex.requestURI.path, ex.requestHeaders.getFirst("Authorization"))
        }.let { it.first to it.second.toByteArray() }
        ex.responseHeaders.add("Content-Type", file?.let { types[it.extension] ?: "application/octet-stream" } ?: "application/json")
        ex.sendResponseHeaders(code, body.size.toLong())
        ex.responseBody.write(body)
      }
    }
    s.start()   // one call at a time (the default executor): like the buttons
    server = s
    val got = s.address.port
    val lan = lan()
    val name = hostName(lan?.first)
    mdns = runCatching {
      JmDNS.create(lan?.second, name).also { it.registerService(ServiceInfo.create("_http._tcp.local.", name, got, "path=/api")) }
    }.getOrNull()
    val names = listOfNotNull("$name.local".takeIf { mdns != null }, lan?.second?.hostAddress)
    WebApiHost.mutable.value = WebApiState(names.joinToString(" / ") { "http://$it:$got" })
    via = names.firstOrNull()?.let { "$it:$got" } ?: ""
    showQr()
    return got
  }

  /** A file of the web page ("/" is index.html), never one outside its folder. */
  private fun pageFile(path: String): File? {
    val root = page?.canonicalFile ?: return null
    return File(root, path.trimStart('/').ifEmpty { "index.html" }).canonicalFile.takeIf { it.isFile && it.startsWith(root) }
  }

  fun stop() {
    mdns?.let { m -> runCatching { m.unregisterAllServices(); m.close() } }
    mdns = null
    server?.stop(0)
    server = null
    via = ""
  }

  companion object {
    private val types = mapOf("html" to "text/html; charset=utf-8", "js" to "text/javascript", "mjs" to "text/javascript", "wasm" to "application/wasm", "png" to "image/png")

    /** This computer on the local network: the interface and its first private IPv4 address. */
    fun lan(): Pair<NetworkInterface, InetAddress>? = NetworkInterface.networkInterfaces().toList()
      .filter { it.isUp && !it.isLoopback && !it.isVirtual }
      .flatMap { n -> n.inetAddresses.toList().map { n to it } }
      .firstOrNull { (_, a) -> a is Inet4Address && a.isSiteLocalAddress }

    /** "scraper-control-1a2b": the end of the network address, like the buttons. */
    fun hostName(n: NetworkInterface?): String {
      val mac = runCatching { n?.hardwareAddress }.getOrNull()?.takeIf { it.size >= 2 } ?: return "scraper-control"
      return "scraper-control-%02x%02x".format(mac[mac.size - 2], mac[mac.size - 1])
    }
  }
}
