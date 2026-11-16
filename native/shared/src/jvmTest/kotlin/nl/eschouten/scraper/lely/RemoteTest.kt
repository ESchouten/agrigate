// The web page (RemoteController) against a barn computer (RemoteHost + RobotController with the
// demo robot), connected by a pipe instead of a WebSocket. The page runs on one thread, as in a browser.
package nl.eschouten.scraper.lely

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.newSingleThreadContext
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import nl.eschouten.scraper.app.AppStart
import nl.eschouten.scraper.app.Call
import nl.eschouten.scraper.app.EventKind
import nl.eschouten.scraper.app.MemoryStore
import nl.eschouten.scraper.app.RemoteController
import nl.eschouten.scraper.app.RemoteHost
import nl.eschouten.scraper.app.Reply
import nl.eschouten.scraper.app.Request
import nl.eschouten.scraper.app.RobotController
import nl.eschouten.scraper.core.RobotKey
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

@OptIn(ExperimentalCoroutinesApi::class)
class RemoteTest {
  private val page = newSingleThreadContext("page")
  private val barn = CoroutineScope(SupervisorJob())

  @AfterTest fun reset() {
    Demo.uninstall()
    page.close()
  }

  private class Pair(val local: RobotController, val remote: RemoteController, val host: RemoteHost)

  /** A barn computer with the demo robot, and a page connected to it. */
  private suspend fun connected(words: String = ""): Pair {
    val local = AppStart.controller(MemoryStore(), "setup=done delay=0 scan=0 ui=nl $words")
    lateinit var host: RemoteHost
    val remote = RemoteController({ text -> host.receive(text) }, CoroutineScope(page))
    host = RemoteHost(local, { text -> withContext(page) { remote.receive(text) } }, barn)
    withTimeout(5_000) { while (!remote.ready.value) delay(10) }
    return Pair(local, remote, host)
  }

  @Test fun theStateArrivesAndARouteRunsOnTheBarnComputer() = runBlocking(page) {
    val p = connected()
    assertEquals(p.local.state.value.robot, p.remote.state.value.robot)
    val o = p.remote.startRoute("A", dryRun = true)
    assertTrue(o.ok, o.message)
    assertEquals(EventKind.DryRun, p.local.state.value.history.first().kind)
    // the page already has the state the call left behind
    assertEquals(EventKind.DryRun, p.remote.state.value.history.first().kind)
    p.local.dispose()
  }

  @Test fun settingsAndRobotsChangeOnTheBarnComputer() = runBlocking(page) {
    val p = connected()
    p.remote.updateSettings { it.copy(widgetTipSeen = true, language = "en") }
    assertTrue(p.local.state.value.widgetTipSeen)
    assertEquals("en", p.remote.state.value.language)
    p.remote.updateRobot(p.remote.state.value.robot.copy(label = "Stal 1"))
    assertEquals("Stal 1", p.local.state.value.robot.displayName)
    val found = p.remote.discover(p.remote.state.value.model, null)
    assertTrue(found.isNotEmpty())
    p.local.dispose()
  }

  @Test fun thePanelWorksFromThePageAndLetsGoWhenThePageDrops() = runBlocking(page) {
    val p = connected()
    val panel = PanelFlow(p.remote)
    panel.connect()
    assertTrue(p.local.isConnected)
    var v = panel.view(p.remote.state.value)
    assertTrue(v.connected)
    assertTrue(v.lines.any { it.isNotBlank() }, v.lines.toString())
    assertNull(panel.press(RobotKey.Enter))
    v = panel.view(p.remote.state.value)
    assertTrue(v.lines.any { it.isNotBlank() })
    // the page goes away without saying so: its hold on the connection ends
    p.host.close()
    p.remote.closed()
    withTimeout(5_000) { while (p.local.isConnected) delay(10) }
    assertFalse(p.local.state.value.connected)
    p.local.dispose()
  }

  @Test fun aHeldKeyEndsWhenThePageGoesQuiet() = runBlocking {
    val local = AppStart.controller(MemoryStore(), "setup=done delay=0 scan=0 ui=nl mode=manual")
    val replies = Channel<Reply>(Channel.UNLIMITED)
    val wire = Json { ignoreUnknownKeys = true }
    val host = RemoteHost(local, { replies.send(wire.decodeFromString(Reply.serializer(), it)) }, barn)
    suspend fun done(id: Int): Reply.Done = withTimeout(5_000) {
      var r: Reply
      do r = replies.receive() while (r !is Reply.Done || r.id != id)
      r as Reply.Done
    }
    fun send(id: Int, c: Call) = host.receive(wire.encodeToString(Request.serializer(), Request(id, c)))
    send(1, Call.Hold)
    assertTrue(done(1).outcome!!.ok)
    // Start/Stop held, then nothing: no "still", no let-go (the phone lost the network)
    val began = System.currentTimeMillis()
    send(2, Call.Input(RobotKey.StartStop.name, held = true))
    val o = done(2).outcome!!
    assertTrue(o.ok, o.message)
    val ms = System.currentTimeMillis() - began
    assertTrue(ms in 900..2_500, "held for $ms ms")
    host.close()
    local.dispose()
  }

  @Test fun aKeyHeldWithStillGoesOnUntilLetGo() = runBlocking(page) {
    val p = connected("mode=manual")
    assertTrue(p.remote.hold().ok)
    assertTrue(Manual.read(p.remote.state.value.live!!.extra) != null, "the panel shows the motors")
    val up = CompletableDeferred<Unit>()
    val began = System.currentTimeMillis()
    launch {
      delay(1_800) // longer than the quiet limit: the page says "still" meanwhile
      up.complete(Unit)
    }
    val o = p.remote.input(RobotKey.StartStop.name, up)
    assertTrue(o.ok, o.message)
    assertTrue(System.currentTimeMillis() - began >= 1_700)
    p.remote.release()
    assertFalse(p.local.isConnected)
    p.local.dispose()
  }
}
