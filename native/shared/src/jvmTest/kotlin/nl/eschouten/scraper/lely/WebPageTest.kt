// The web page's controller against a barn computer (WebServer with the demo robot), over a
// real connection on this machine.
package nl.eschouten.scraper.lely

import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.rpc.krpc.ktor.client.installKrpc
import kotlinx.rpc.krpc.ktor.client.rpc
import kotlinx.rpc.krpc.ktor.client.rpcConfig
import kotlinx.rpc.krpc.serialization.json.json
import kotlinx.rpc.withService
import nl.eschouten.scraper.app.AppStart
import nl.eschouten.scraper.app.Controller
import nl.eschouten.scraper.app.EventKind
import nl.eschouten.scraper.app.MemoryStore
import nl.eschouten.scraper.app.RobotApi
import nl.eschouten.scraper.app.RobotController
import nl.eschouten.scraper.app.WebServer
import nl.eschouten.scraper.app.flags
import nl.eschouten.scraper.app.remoteController
import nl.eschouten.scraper.core.RobotKey
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class WebPageTest {
  private val port = 18080 + (ProcessHandle.current().pid() % 1000).toInt()
  private val http = HttpClient(CIO) { installKrpc() }
  private lateinit var barn: RobotController
  private lateinit var server: WebServer
  private val url get() = server.state.value.address.replace("http://", "ws://").replace(Regex("//.*:"), "//localhost:").replace("/?", "/rpc/")

  @AfterTest fun stop() {
    server.stop()
    barn.dispose()
    http.close()
    Demo.uninstall()
  }

  /** A barn computer with the demo robot and the web page on. */
  private fun barn(words: String = "") {
    barn = AppStart.controller(MemoryStore(), "setup=done delay=0 scan=0 ui=nl $words")
    server = WebServer(barn, MemoryStore(mapOf("web.port" to "$port")))
    server.switch(true)
    assertNull(server.state.value.error)
  }

  /** A page connected to it, until [scope] ends. */
  private suspend fun page(scope: CoroutineScope): Controller =
    remoteController(http.rpc(url) { rpcConfig { serialization { json() } } }.withService<RobotApi>(), scope)

  private fun withPage(words: String = "", test: suspend CoroutineScope.(Controller) -> Unit) = runBlocking {
    barn(words)
    val scope = CoroutineScope(Dispatchers.Default + Job())
    try {
      withTimeout(30_000) { test(page(scope)) }
    } finally {
      scope.cancel()
    }
  }

  @Test fun onlyTheRightCodeGetsIn() = runBlocking {
    barn()
    assertTrue(Regex("ws://localhost:$port/rpc/\\d{6}").matches(url), url)
    val scope = CoroutineScope(Dispatchers.Default + Job())
    val wrong = runCatching { withTimeout(5_000) { remoteController(http.rpc("ws://localhost:$port/rpc/nope") { rpcConfig { serialization { json() } } }.withService<RobotApi>(), scope) } }
    assertTrue(wrong.isFailure)
    scope.cancel()
  }

  @Test fun theStateArrivesAndARouteRunsOnTheBarnComputer() = withPage { page ->
    assertEquals(barn.state.value.robot, page.state.value.robot)
    val o = page.startRoute("A", dryRun = true)
    assertTrue(o.ok, o.message)
    assertEquals(EventKind.DryRun, barn.state.value.history.first().kind)
    assertEquals(EventKind.DryRun, page.state.first { it.history.isNotEmpty() }.history.first().kind)
  }

  @Test fun settingsRobotsAndScanGoToTheBarnComputer() = withPage { page ->
    page.updateSettings(page.state.value.flags.copy(widgetTipSeen = true, language = "en"))
    assertTrue(barn.state.value.widgetTipSeen)
    assertEquals("en", page.state.first { it.language == "en" }.language)
    page.updateRobot(page.state.value.robot.copy(label = "Stal 1"))
    assertEquals("Stal 1", barn.state.value.robot.displayName)
    assertTrue(page.discover(page.state.value.model.id, null).isNotEmpty())
  }

  @Test fun thePanelWorksFromThePage() = withPage("mode=manual") { page ->
    val panel = PanelFlow(page)
    panel.connect()
    assertTrue(barn.isConnected)
    val v = panel.view(page.state.first { it.connected && it.live?.screen?.isNotBlank() == true })
    assertTrue(v.connected)
    assertNotNull(v.manual, "the panel shows the motors")
    assertNull(panel.press(RobotKey.Soft1))
    // Start/Stop held for a while, then let go
    val up = CompletableDeferred<Unit>()
    val began = System.currentTimeMillis()
    launch {
      delay(700)
      up.complete(Unit)
    }
    val o = page.input(RobotKey.StartStop.name, flow { up.await() })
    assertTrue(o.ok, o.message)
    assertTrue(System.currentTimeMillis() - began >= 650)
    panel.disconnect()
    assertFalse(barn.isConnected)
  }

  @Test fun aPageThatDropsLetsGoOfTheKeyAndTheRobot() = runBlocking {
    barn("mode=manual")
    val scope = CoroutineScope(Dispatchers.Default + Job())
    val rpc = http.rpc(url) { rpcConfig { serialization { json() } } }
    val page = remoteController(rpc.withService<RobotApi>(), scope)
    assertTrue(page.hold().ok)
    val held = scope.launch { page.input(RobotKey.StartStop.name, flow { delay(60_000) }) }
    delay(500)
    assertTrue(barn.isConnected)
    // the page is gone (the tab was closed) without letting go
    rpc.webSocketSession.await().cancel()
    withTimeout(10_000) { while (barn.isConnected) delay(20) }
    withTimeout(5_000) { held.join() }
    // the key is up and the connection free: the barn computer's own panel works again
    assertTrue(barn.hold().ok)
    assertTrue(barn.input(RobotKey.Soft1.name).ok)
    barn.release()
    scope.cancel()
  }
}
