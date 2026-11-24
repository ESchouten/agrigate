// The web API (docs/clean/API.md) end to end: the desktop's server with the simulated robot behind it,
// and an app whose robot goes "via" that server (api/ApiSession), as from a phone.
package com.eschouten.agrigate.clean.discovery

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import com.eschouten.agrigate.clean.api.ApiServer
import com.eschouten.agrigate.clean.app.EventKind
import com.eschouten.agrigate.clean.app.MemoryStore
import com.eschouten.agrigate.clean.app.Problem
import com.eschouten.agrigate.clean.app.RobotController
import com.eschouten.agrigate.clean.app.RobotProfile
import com.eschouten.agrigate.clean.app.Settings
import com.eschouten.agrigate.clean.texts.useLanguage
import com.eschouten.agrigate.clean.ui.Actions
import java.net.HttpURLConnection
import java.net.URI
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class WebApiTest {
  private val links = mutableListOf<SimulatorLink>()
  private val barn = RobotController(Settings.single(robotId = "00:12:FF:51:4D:00", pin = "4321"), MemoryStore())
  private val page = java.nio.file.Files.createTempDirectory("page").toFile().also { java.io.File(it, "index.html").writeText("<html>the app</html>") }
  private val server = ApiServer(barn, page)
  private var port = 0

  private fun simulate(vararg args: String) {
    Discovery.linkProvider = LinkProvider(
      link = { id -> SimulatorLink(args = args.toList(), id = id).also { links += it } },
      robots = Simulator.robots,
      scanDelayMs = 10,
    )
  }

  /** The app on a phone: its robot is reached through the barn computer. */
  private fun phone(pin: String = "4321", via: String = "127.0.0.1:$port") =
    RobotController(Settings(listOf(RobotProfile(pin = pin, via = via))), MemoryStore())

  private fun raw(method: String, path: String, pin: String? = "4321"): Pair<Int, String> {
    val c = URI("http://127.0.0.1:$port$path").toURL().openConnection() as HttpURLConnection
    c.requestMethod = method
    if (pin != null) c.setRequestProperty("Authorization", "Bearer $pin")
    val body = (if (c.responseCode < 400) c.inputStream else c.errorStream)?.bufferedReader()?.readText() ?: ""
    return c.responseCode to body
  }

  @BeforeTest fun start() {
    useLanguage("nl")
    port = server.start(0)
  }

  @AfterTest fun stop() {
    server.stop()
    Discovery.linkProvider = null
  }

  @Test fun aPhoneStartsARouteThroughTheBarnComputer() = runBlocking {
    simulate("--planning", "on")
    val p = phone()
    val o = p.startRoute("C")
    assertTrue(o.ok, o.message)
    assertTrue(p.state.value.planningResumes)   // it read the robot first: the schedule was on
    assertEquals(EventKind.Route, p.state.value.history.first().kind)
    assertEquals(EventKind.Route, barn.state.value.history.first().kind)   // the barn computer did it
    assertTrue("start_stop" in links.last().report())
    assertFalse(barn.state.value.connected)   // and let the robot go
  }

  @Test fun aDryRunStartsNothing() = runBlocking {
    simulate()
    val o = phone().startRoute("B", dryRun = true)
    assertTrue(o.ok, o.message)
    assertEquals(EventKind.DryRun, barn.state.value.history.first().kind)
    assertTrue("\"mode\": \"home\"" in links.last().report(), links.last().report())   // not driving
  }

  @Test fun theScheduleAndTheStatus() = runBlocking {
    simulate("--planning", "on")
    val p = phone()
    assertTrue(p.setPlanning(false).ok)
    assertEquals(false, p.state.value.snapshot?.planning)
    val (code, body) = raw("GET", "/api/clean/status")
    assertEquals(200, code)
    assertTrue("\"planning\":false" in body && "\"age\":" in body, body)
    assertTrue(raw("GET", "/api/clean/status/fresh").second.contains("\"ok\":true"))
    assertEquals(200, raw("GET", "/api/status").first)   // where an app from before 1.6.224 asks
  }

  @Test fun aStopFromThePhoneAndTheAnswerOnThePhone() = runBlocking {
    simulate("--mode", "driving")
    val p = phone()
    val run = async(Dispatchers.Default) { p.startRoute("A") }
    withTimeout(20_000) { p.state.first { it.finishQuestion } }   // the barn computer left the question open
    assertTrue("start_stop" in links.last().report())
    simulate("--mode", "abort")   // the robot still asks
    p.answerFinish(true)
    val o = withTimeout(20_000) { run.await() }
    assertTrue(o.ok && o.stopped, o.message)
    assertEquals("De robot rijdt de route verder.", o.message)
    assertTrue("soft1" in links.last().report())
  }

  @Test fun aWrongPinIsRefusedAndManyCloseTheDoor() = runBlocking {
    simulate()
    val o = phone(pin = "0000").refresh()
    assertFalse(o.ok)
    assertEquals(Problem.PinRejected, o.problem)
    assertEquals(401, raw("GET", "/api/clean/status", pin = null).first)   // no PIN: not counted
    repeat(4) { assertEquals(401, raw("POST", "/api/clean/planning/off", pin = "1").first) }
    assertEquals(429, raw("GET", "/api/clean/status").first)   // 5 wrong: a minute closed, also for the right one
    assertTrue(links.isEmpty())   // the robot was never touched
  }

  @Test fun unknownCallsAndChangesFromABrowserLink() {
    assertEquals(404, raw("GET", "/nothing").first)
    assertEquals(404, raw("GET", "/api/clean/route/A").first)   // a link opened in a browser starts nothing
    assertEquals(404, raw("POST", "/api/clean/route/E").first)
    assertEquals(404, raw("POST", "/api/clean/planning/maybe").first)
    assertEquals(404, raw("POST", "/api/clean/route/A/dry").first)   // a mistyped dry run starts nothing
    assertEquals(404, raw("POST", "/api/clean/status/fresh").first)
  }

  @Test fun thePanelThroughTheBarnComputer() = runBlocking {
    simulate()
    val p = phone()
    assertTrue(p.hold().ok)
    assertTrue(barn.state.value.connected)   // the barn computer keeps the robot for the phone
    withTimeout(10_000) { p.state.first { it.live?.screen?.isNotBlank() == true } }   // the robot's own display
    assertTrue(p.custom { it.input("Down") }.ok)
    p.release()
    withTimeout(15_000) { barn.state.first { !it.connected } }   // nobody asks any more: it lets the robot go
    assertTrue("down" in links.last().report())
  }

  @Test fun aHeldKeyIsLetGoWhenItsRepeatsStop() = runBlocking {
    simulate("--mode", "manual")
    assertEquals(502, raw("POST", "/api/clean/panel/StartStop/down").first)   // not connected: nothing held
    assertEquals(200, raw("GET", "/api/clean/panel").first)
    assertEquals(200, raw("POST", "/api/clean/panel/StartStop/down").first)   // a finger on the key, and then the page is gone
    val t0 = System.currentTimeMillis()
    assertEquals(200, raw("POST", "/api/clean/panel/Soft1").first)   // the next key waits until that one is up
    val took = System.currentTimeMillis() - t0
    assertTrue(took in 400..3_000, "$took ms")
    assertEquals(502, raw("POST", "/api/clean/panel/Nothing").first)   // not a key of this robot
  }

  @Test fun aPhoneThroughAComputerThroughAnotherDevice() = runBlocking {
    simulate("--planning", "on")
    val office = RobotController(Settings(listOf(RobotProfile(pin = "4321", via = "127.0.0.1:$port"))), MemoryStore())
    val second = ApiServer(office, null)
    try {
      val o = phone(via = "127.0.0.1:${second.start(0)}").startRoute("B")
      assertTrue(o.ok, o.message)
      assertEquals(EventKind.Route, office.state.value.history.first().kind)
      assertEquals(EventKind.Route, barn.state.value.history.first().kind)   // the one with Bluetooth did it
    } finally {
      second.stop()
    }
  }

  @Test fun theSameAddressInABrowserIsTheWebPage() {
    assertEquals(200 to "<html>the app</html>", raw("GET", "/", pin = null))   // the page asks for the PIN itself
    assertEquals(200, raw("GET", "/index.html", pin = null).first)
    java.io.File(page.parentFile, "outside.txt").writeText("x")
    assertEquals(404, raw("GET", "/../outside.txt", pin = null).first)
    assertEquals(404, raw("POST", "/index.html").first)
  }

  @Test fun busyWhenThereIsNothingToStopOrAnswer() = runBlocking {
    simulate()
    val (code, body) = raw("POST", "/api/clean/stop")
    assertEquals(502, code)
    assertTrue("\"fault\":\"Busy\"" in body, body)
    assertEquals(502, raw("POST", "/api/clean/answer/no").first)
  }

  @Test fun aDeviceThatIsNotThere() = runBlocking {
    val o = phone(via = "127.0.0.1:1").refresh()
    assertFalse(o.ok)
    assertEquals(Problem.NotFound, o.problem)
    assertTrue("127.0.0.1:1" in o.message, o.message)
  }

  @Test fun theQrCodeAddsTheRobotThroughThatDevice() = runBlocking {
    val p = RobotController(Settings(), MemoryStore())   // a new app, before its first setup
    assertFalse(Actions.openLink(p, "https://example.com"))
    assertTrue(Actions.openLink(p, "agrigate-clean://add?via=127.0.0.1:$port&pin=4321"))
    val s = p.state.value
    assertTrue(s.onboarded)
    assertEquals(1, s.robots.size)   // the empty first robot became this one
    assertEquals("127.0.0.1:$port", s.robot.via)
    assertEquals("4321", s.robot.pin)
    assertTrue(Actions.openLink(p, "agrigate-clean://add?via=127.0.0.1:$port&pin=1234"))   // scanned again: the new PIN
    assertEquals(1, p.state.value.robots.size)
    assertEquals("1234", p.state.value.robot.pin)
  }
}
