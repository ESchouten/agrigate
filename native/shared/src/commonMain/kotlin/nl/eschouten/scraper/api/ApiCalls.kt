// The serving side of the web API (docs/API.md), for any host: which call a request is, who
// may make it, and the answer, all through the app's RobotController (the robot in use). The
// desktop serves it over HTTP (ApiServer); the buttons have their own (firmware/src/Api.h).
package nl.eschouten.scraper.api

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import nl.eschouten.scraper.app.Outcome
import nl.eschouten.scraper.app.Problem
import nl.eschouten.scraper.app.RobotController
import nl.eschouten.scraper.app.Settings
import nl.eschouten.scraper.model.ScraperFault

class ApiCalls(private val c: RobotController, private val now: () -> Long) {
  private val one = Mutex()   // one call at a time, like the buttons
  private var wrong = 0
  private var lockedUntil = 0L

  /** HTTP status and JSON body. [authorization]: that header. */
  suspend fun handle(method: String, path: String, authorization: String?): Pair<Int, String> = one.withLock {
    val call = callFor(method, path) ?: return@withLock 404 to "{}"
    val gate = check(authorization?.removePrefix("Bearer ")?.takeIf { authorization.startsWith("Bearer ") } ?: "")
    if (gate != 200) return@withLock gate to "{}"
    val o = call()
    val seen = c.lastSeen
    val answer = ApiAnswer(
      ok = o?.ok ?: true,
      fault = o?.takeIf { !it.ok }?.problem?.let(::faultOf),
      message = o?.takeIf { !it.ok }?.message ?: "",
      age = seen?.let { (now() - it.second) / 1000 },
      status = seen?.first,
    )
    (if (answer.ok) 200 else 502) to apiJson.encodeToString(ApiAnswer.serializer(), answer)
  }

  /** null: not a call of the API. The call itself returns null for nothing done (a plain status). */
  private fun callFor(method: String, path: String): (suspend () -> Outcome?)? {
    val part = path.removePrefix("/api/").split('/')
    val route = part.getOrNull(1)?.uppercase()?.takeIf { part[0] == "route" && it in Settings.allRoutes }
    return when {
      method == "GET" -> when (part) {
        listOf("status") -> ({ null })
        listOf("status", "fresh") -> ({ c.refresh() })
        else -> null   // a change never comes from a link opened in a browser
      }
      method != "POST" -> null
      route != null && part.size == 2 -> ({ c.startRoute(route, ask = false) })
      route != null && part.drop(2) == listOf("dryRun") -> ({ c.startRoute(route, dryRun = true, ask = false) })
      part == listOf("stop") -> ({ c.stopRoute() })
      part == listOf("planning", "on") || part == listOf("planning", "off") -> ({ c.setPlanning(part[1] == "on") })
      part == listOf("answer", "yes") || part == listOf("answer", "no") -> ({ c.answerStop(part[1] == "yes") })
      else -> null
    }
  }

  /** The PIN of the robot in use (none set: open). 5 wrong in a row: a minute closed (429);
   *  no PIN at all is not counted. */
  private fun check(given: String): Int {
    val t = now()
    if (t < lockedUntil) return 429
    val pin = c.state.value.robot.pin
    if (pin.isEmpty() || given == pin) {
      wrong = 0
      return 200
    }
    if (given.isNotEmpty() && ++wrong >= 5) {
      wrong = 0
      lockedUntil = t + 60_000
    }
    return 401
  }

  private fun faultOf(p: Problem) = when (p) {
    Problem.BluetoothOff -> ScraperFault.BluetoothOff
    Problem.NotFound -> ScraperFault.NotFound
    Problem.PinRejected -> ScraperFault.AuthRejected
    Problem.Busy -> ScraperFault.Busy
    Problem.NoAnswer -> ScraperFault.NoConfirmation
    Problem.None, Problem.Failed -> ScraperFault.Failed
  }
}

/** Where a host serves the web API, for its settings screen: the address, or why it does not
 *  run, and a QR code (rows of dark modules) that adds the robot in use to the app on a phone. */
data class WebApiState(val address: String = "", val error: String? = null, val qr: List<BooleanArray>? = null)

/** Set by a host that serves the web API (the desktop); null where none does. */
object WebApiHost {
  val state: StateFlow<WebApiState?> get() = mutable
  internal val mutable = MutableStateFlow<WebApiState?>(null)
}
