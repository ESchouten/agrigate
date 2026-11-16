// The serving side of the web API (docs/API.md), for any host: which call a request is, who
// may make it, and the answer, all through the app's RobotController (the robot in use). The
// desktop serves it over HTTP (ApiServer); the buttons have their own (firmware/src/Api.h).
// An app also answers the calls for a type's own screen (the panel): its connection to the robot
// stays open only while someone keeps asking for the screen, and a key stays down only while
// it is repeated, so a page or phone that goes away lets go by itself.
package nl.eschouten.scraper.api

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import nl.eschouten.scraper.app.Outcome
import nl.eschouten.scraper.app.Problem
import nl.eschouten.scraper.app.RobotController
import nl.eschouten.scraper.app.Settings
import nl.eschouten.scraper.model.ScraperFault
import nl.eschouten.scraper.texts.tr
import kotlin.concurrent.Volatile

class ApiCalls(private val c: RobotController, private val scope: CoroutineScope = CoroutineScope(Dispatchers.Default), private val now: () -> Long) {
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
        listOf("panel") -> ::watch
        else -> null   // a change never comes from a link opened in a browser
      }
      method != "POST" -> null
      part[0] == "panel" && part.size == 2 -> ({ c.custom { it.input(part[1]) } })
      part[0] == "panel" && part.drop(2) == listOf("down") -> ({ keyDown(part[1]) })
      part[0] == "panel" && part.drop(2) == listOf("up") -> ({ key?.up?.complete(Unit); null })
      route != null && part.size == 2 -> ({ c.startRoute(route, ask = false) })
      route != null && part.drop(2) == listOf("dryRun") -> ({ c.startRoute(route, dryRun = true, ask = false) })
      part == listOf("stop") -> ({ c.stopRoute() })
      part == listOf("planning", "on") || part == listOf("planning", "off") -> ({ c.setPlanning(part[1] == "on") })
      part == listOf("answer", "yes") || part == listOf("answer", "no") -> ({ c.answerStop(part[1] == "yes") })
      else -> null
    }
  }

  private var watching: Job? = null
  @Volatile private var watchedAt = 0L
  @Volatile private var key: Held? = null

  private class Held(val name: String, @Volatile var until: Long) {
    val up = CompletableDeferred<Unit>()
  }

  /** The connection to the robot stays open until [PANEL_FOR_MS] after the last ask. */
  private suspend fun watch(): Outcome? {
    watchedAt = now()
    if (watching?.isActive == true) return null
    val o = c.hold()
    if (o.ok) watching = scope.launch {
      while (now() - watchedAt < PANEL_FOR_MS) delay(250)
      key?.up?.complete(Unit)
      c.release()
    }
    return o
  }

  /** A key goes down, or stays down: until its "up", or [KEY_FOR_MS] after the last repeat. */
  private fun keyDown(name: String): Outcome? {
    if (c.session == null) return Outcome(false, tr.notConnected, problem = Problem.Busy)
    key?.takeIf { it.name == name && !it.up.isCompleted }?.let {
      it.until = now() + KEY_FOR_MS
      return null
    }
    key?.up?.complete(Unit)
    val k = Held(name, now() + KEY_FOR_MS).also { key = it }
    scope.launch {
      val guard = launch {
        while (now() < k.until) delay(50)
        k.up.complete(Unit)
      }
      c.custom { it.input(name, k.up) }
      guard.cancel()
    }
    return null
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
