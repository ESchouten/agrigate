// The desktop app serves the web API (docs/API.md) for the robot in use, when Settings > Web API
// is on: the JDK's own HTTP server, and http://scraper-control-1a2b.local:<port> by mDNS (JmDNS),
// because many networks do not resolve a computer's own name. The name is fixed per computer
// (the end of its network address), so two devices never swap names (Erik 2026-11-16).
package nl.eschouten.scraper.api

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import nl.eschouten.scraper.app.RobotController
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import javax.jmdns.JmDNS
import javax.jmdns.ServiceInfo

class ApiServer(private val controller: RobotController) {
  private val calls = ApiCalls(controller) { System.currentTimeMillis() }
  private var server: HttpServer? = null
  private var mdns: JmDNS? = null

  /** Runs and stops with the setting, for as long as [scope] lives. */
  fun follow(scope: CoroutineScope) = scope.launch {
    controller.state.map { it.apiOn to it.apiPort }.distinctUntilChanged().collect { (on, port) ->
      stop()
      if (on) start(port)
      else WebApiHost.mutable.value = null
    }
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
        val (code, body) = runBlocking {
          calls.handle(ex.requestMethod, ex.requestURI.path, ex.requestURI.rawQuery ?: "", ex.requestHeaders.getFirst("Authorization"))
        }
        val bytes = body.toByteArray()
        ex.responseHeaders.add("Content-Type", "application/json")
        ex.sendResponseHeaders(code, bytes.size.toLong())
        ex.responseBody.write(bytes)
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
    return got
  }

  fun stop() {
    mdns?.let { m -> runCatching { m.unregisterAllServices(); m.close() } }
    mdns = null
    server?.stop(0)
    server = null
  }

  companion object {
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
