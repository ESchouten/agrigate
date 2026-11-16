// The web API (docs/API.md) end to end: the desktop's server with the simulated robot behind it,
// and an app whose robot goes "via" that server (api/ApiSession), as from a phone.
package nl.eschouten.scraper.lely

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import nl.eschouten.scraper.api.ApiServer
import nl.eschouten.scraper.app.EventKind
import nl.eschouten.scraper.app.MemoryStore
import nl.eschouten.scraper.app.Problem
import nl.eschouten.scraper.app.RobotController
import nl.eschouten.scraper.app.RobotProfile
import nl.eschouten.scraper.app.Settings
import nl.eschouten.scraper.texts.useLanguage
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
  private val server = ApiServer(barn)
  private var port = 0

  private fun simulate(vararg args: String) {
    LelyDiscovery.linkProvider = LinkProvider(
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
    LelyDiscovery.linkProvider = null
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

  @Test fun theScheduleAndTheStatus() = runBlocking {
    simulate("--planning", "on")
    val p = phone()
    assertTrue(p.setPlanning(false).ok)
    assertEquals(false, p.state.value.snapshot?.planning)
    val (code, body) = raw("GET", "/api/status")
    assertEquals(200, code)
    assertTrue("\"planning\":false" in body && "\"age\":" in body, body)
    assertTrue(raw("GET", "/api/status?fresh").second.contains("\"ok\":true"))
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
    assertEquals(401, raw("GET", "/api/status", pin = null).first)   // no PIN: not counted
    repeat(4) { assertEquals(401, raw("POST", "/api/planning/off", pin = "1").first) }
    assertEquals(429, raw("GET", "/api/status").first)   // 5 wrong: a minute closed, also for the right one
    assertTrue(links.isEmpty())   // the robot was never touched
  }

  @Test fun unknownCallsAndChangesFromABrowserLink() {
    assertEquals(404, raw("GET", "/").first)
    assertEquals(404, raw("GET", "/api/route/A").first)   // a link opened in a browser starts nothing
    assertEquals(404, raw("POST", "/api/route/E").first)
    assertEquals(404, raw("POST", "/api/planning/maybe").first)
  }

  @Test fun busyWhenThereIsNothingToStopOrAnswer() = runBlocking {
    simulate()
    val (code, body) = raw("POST", "/api/stop")
    assertEquals(502, code)
    assertTrue("\"fault\":\"Busy\"" in body, body)
    assertEquals(502, raw("POST", "/api/answer/no").first)
  }

  @Test fun aDeviceThatIsNotThere() = runBlocking {
    val o = phone(via = "127.0.0.1:1").refresh()
    assertFalse(o.ok)
    assertEquals(Problem.NotFound, o.problem)
    assertTrue("127.0.0.1:1" in o.message, o.message)
  }
}
