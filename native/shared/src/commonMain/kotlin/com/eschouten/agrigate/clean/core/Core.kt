// A manure robot's session in the core (core/session_c.h, core/clean_c.h), seen from Kotlin. All
// robot logic lives in the driver; this file moves the session's bytes and turns its commands
// into suspend functions.
package com.eschouten.agrigate.clean.core

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlin.concurrent.Volatile

/** How a command ended (session_c.h, clean_c.h). */
enum class CoreResult(val code: Int) {
  Ok(0), ConnectFailed(1), PinRejected(2), Unexpected(3), Busy(10), StartedNoAnswer(11), Cancelled(12), InProgress(100);

  companion object {
    fun of(code: Int): CoreResult = entries.firstOrNull { it.code == code } ?: Unexpected
  }
}

/** Progress while a command runs: the Discovery driver's steps, by its names for them. */
enum class CoreStep {
  Linked, // Bluetooth link open
  Granted, // PIN accepted
  ScreenReady, // panel screen received
  PlanningOff, // time planning switched off
  PlanningOn, // time planning switched on
  Menu, // HANDM. ROUTE open
  Selected, // route selected
  StartPressed, // Start pressed, question shown
  Answered, // question answered: route is running
  Restored, // dry run / abort: back as before
  Syncing, // the robot sends its image set (first connection), again per part: CoreStatus.syncPercent
}

/** The Discovery's panel keys: its driver's inputs, by its names for them. */
enum class RobotKey { Back, Soft2, Soft1, Up, Prog, Down, Soft3, Enter, StartStop }

/** The manual control screen's motors and direction; null = not known. */
data class ManualState(val left: Boolean?, val right: Boolean?, val forward: Boolean?)

/** The charger approach screen (HANDM. LADER): the charger's side and the battery as shown. */
data class ChargerState(val left: Boolean, val right: Boolean, val volt: String, val amp: String)

/** What the core read from the robot's screen (clean_status_json). */
data class CoreStatus(
  /** Time planning on/off; null = not on the home screen. */
  val planning: Boolean? = null,
  val nextRide: String = "",
  val route: String = "",
  val action: String = "",
  val batteryMv: Int? = null,
  val busy: Boolean = true,
  /** A route runs (driving screen): stopRoute() can stop it. */
  val driving: Boolean = false,
  /** After a stop the robot asks "finish the route?": answerStop() passes the user's choice on. */
  val stopQuestion: Boolean = false,
  val screen: String = "",
  /** Selected menu row as on the display, "" if none. */
  val highlighted: String = "",
  /** The robot's own language: "nl", "en" or "" if not known. */
  val language: String = "",
  /** How far the robot is with sending its image set (0-100), null when it is not. */
  val syncPercent: Int? = null,
  /** On the manual control screen: what the robot shows there as images, else null. */
  val manual: ManualState? = null,
  /** On the charger approach screen: the charger's side and the battery, else null. */
  val charger: ChargerState? = null,
  /** A command runs that [CoreSession.cancel] can still stop (until Start for a route). */
  val cancellable: Boolean = false,
) {
  companion object {
    fun fromJson(json: String): CoreStatus {
      val d = runCatching { Json.parseToJsonElement(json).jsonObject }.getOrNull() ?: return CoreStatus()
      val own = d["own"] as? JsonObject ?: JsonObject(emptyMap())
      fun s(k: String, o: JsonObject = d) = (o[k] as? JsonPrimitive)?.content ?: ""
      fun i(k: String) = (d[k] as? JsonPrimitive)?.intOrNull
      fun b(k: String) = (d[k] as? JsonPrimitive)?.booleanOrNull
      return CoreStatus(
        planning = b("planning"),
        nextRide = s("nextRun"), route = s("route"), action = s("activity"),
        batteryMv = i("batteryMv"),
        busy = b("busy") ?: true, driving = b("driving") ?: false, stopQuestion = b("finishQuestion") ?: false,
        screen = s("screen", own), highlighted = s("highlighted", own), language = s("language", own),
        syncPercent = i("progress"),
        cancellable = b("cancellable") ?: false,
        manual = (own["manual"] as? JsonObject)?.let { m ->
          fun f(k: String) = when (m[k]?.jsonPrimitive?.intOrNull) { 1 -> true; 0 -> false; else -> null }
          ManualState(f("left"), f("right"), f("forward"))
        },
        charger = (own["charger"] as? JsonObject)?.let { c ->
          ChargerState(c["left"]?.jsonPrimitive?.intOrNull == 1, c["right"]?.jsonPrimitive?.intOrNull == 1, s("volt", c), s("amp", c))
        },
      )
    }
  }
}

/** One session in the core, per platform: JNI (Android, desktop) or cinterop (iOS). Every call
 *  may come from any thread, but [wait] and [outgoing]: those are the one loop's that moves the
 *  bytes. */
internal interface CoreNative {
  fun incoming(data: ByteArray)
  /** The next bytes to write to the robot, null when there are none. */
  fun outgoing(): ByteArray?
  fun linkDown()
  /** Blocks until the session has something new (bytes, a result, a step), at most [ms]. */
  fun wait(ms: Int)
  /** The running command's result, [CoreResult.InProgress] until it is done. */
  fun result(): Int
  fun connect(): Int
  fun startRoute(letter: Char, dryRun: Boolean): Int
  fun setPlanning(on: Boolean): Int
  fun stopRoute(): Int
  fun answerStop(finish: Boolean): Int
  fun readRoutes(): Int
  fun finish(): Int
  fun input(name: String, hold: Boolean): Int
  fun release()
  fun cancel()
  fun statusJson(): String
  fun routesJson(): String
  /** The next step the running command took, "" when there is none. */
  fun nextStep(): String
  fun memory(): String
  fun lastError(): String
  fun close()
}

/** A session with [driver] (session_open); null for a driver that is not in this build. */
internal expect fun openCoreNative(driver: String, pin: String, memory: String, language: String): CoreNative?

/** The drivers in this build (session_drivers_json). */
internal expect fun nativeDriversJson(): String

/** The PIN as the Discovery driver sends it (for the fake robots in tests). */
internal expect fun nativeEncryptPin(pin: String): ByteArray

/** Where a coroutine may block in [CoreNative.wait]. */
internal expect val waitingDispatcher: CoroutineDispatcher

/** The manure robot driver in this build: {"id", "links": how its robots are reached, ...}. */
internal val cleanDriver: JsonObject? by lazy {
  Json.parseToJsonElement(nativeDriversJson()).jsonArray.map { it.jsonObject }
    .firstOrNull { d -> d["contracts"]?.jsonArray.orEmpty().any { it.jsonPrimitive.content == "clean" } }
}

/** One connection's worth of robot logic in the core. Commands run one at a time.
 *  [memory]: what [memory] gave last time for this robot. */
class CoreSession(
  private val onWrite: (ByteArray) -> Unit,
  pin: String? = null,
  memory: String = "",
  private val onStep: ((CoreStep) -> Unit)? = null,
  language: String = "nl",
) {
  @Volatile private var pending: CompletableDeferred<CoreResult>? = null
  @Volatile private var native: CoreNative? =
    openCoreNative(cleanDriver?.get("id")?.jsonPrimitive?.content ?: "", pin ?: "", memory, language)
  private val starting = Mutex()
  private val pump: Job? = native?.let { n -> CoroutineScope(waitingDispatcher).launch { move(n) } }

  /** The session's exchange (session_c.h): its bytes to the robot, its steps and results to
   *  whoever waits for them. A command's result is read before its last bytes are taken, so
   *  whoever gets the result knows those bytes went out through [onWrite]. */
  private suspend fun move(n: CoreNative) {
    while (currentCoroutineContext().isActive) {
      n.wait(250)
      val (waiting, result) = starting.withLock { pending to n.result() }
      while (true) onWrite(n.outgoing() ?: break)
      while (true) {
        val step = n.nextStep()
        if (step.isEmpty()) break
        CoreStep.entries.firstOrNull { it.name == step }?.let { onStep?.invoke(it) }
      }
      if (waiting != null && result != CoreResult.InProgress.code) {
        starting.withLock { if (pending === waiting) pending = null }
        waiting.complete(CoreResult.of(result))
      }
    }
  }

  /** Bytes from the robot (TX notifications). */
  fun feed(data: ByteArray) {
    if (data.isNotEmpty()) native?.incoming(data)
  }

  fun linkDown() { native?.linkDown() }

  suspend fun connect() = command { it.connect() }
  suspend fun startRoute(letter: Char, dryRun: Boolean = false) = command { it.startRoute(letter.uppercaseChar(), dryRun) }
  suspend fun setPlanning(on: Boolean) = command { it.setPlanning(on) }
  suspend fun stopRoute() = command { it.stopRoute() }
  /** The user's answer to "route afmaken?" after a stop: true = Ja (drives on), false = Nee. */
  suspend fun answerStop(finish: Boolean) = command { it.answerStop(finish) }
  suspend fun readRoutes() = command { it.readRoutes() }

  /** After [readRoutes]: route letter -> its sub-routes, e.g. "C" -> "1 2 3". */
  fun routes(): Map<String, String> = native?.let { n ->
    runCatching { Json.parseToJsonElement(n.routesJson()).jsonObject.mapValues { it.value.jsonPrimitive.content } }.getOrNull()
  } ?: emptyMap()
  suspend fun press(key: RobotKey) = command { it.input(key.name, false) }

  /** Holds [key] down until [up] completes, this is cancelled or the link drops; returns when the
   *  key is up again. Released only after the hold reached the core, so a quick tap cannot slip
   *  in before it and leave the key held. */
  suspend fun hold(key: RobotKey, up: Deferred<*>) = command(whileRunning = {
    try {
      up.await()
    } finally {
      native?.release()
    }
  }) { it.input(key.name, true) }

  suspend fun finish() = command { it.finish() }

  /** The user cancelled: the running command stops before its next key and ends with
   *  [CoreResult.Cancelled], the robot back as it was. Any thread; does not wait. */
  fun cancel() { native?.cancel() }

  /** What the driver wants kept for this robot until the next session (clean_c.h): read it
   *  after each command and hand it to the next session. */
  fun memory(): String = native?.memory() ?: ""

  /** Starts a command and waits for it; [whileRunning] runs once it reached the core and is
   *  cancelled when the command is done. */
  private suspend fun command(whileRunning: (suspend () -> Unit)? = null, start: (CoreNative) -> Int): CoreResult {
    val n = native ?: return CoreResult.ConnectFailed
    val d = CompletableDeferred<CoreResult>()
    val started = starting.withLock {
      if (pending != null) return CoreResult.InProgress
      start(n).also { if (it == 0) pending = d }
    }
    if (started != 0) return CoreResult.of(started)
    // [d] is completed where the session's bytes are moved. Resume on Default, never there: a
    // caller without a dispatcher (Swift, a UI test) would otherwise run on and could close
    // the session from the loop it then waits for.
    return withContext(Dispatchers.Default) {
      val w = whileRunning?.let { launch { it() } }
      try {
        d.await()
      } finally {
        w?.cancel()
      }
    }
  }

  fun status(): CoreStatus = native?.let { CoreStatus.fromJson(it.statusJson()) } ?: CoreStatus()

  /** What went wrong with the last command, in the session's language. */
  fun lastError(): String = native?.lastError() ?: ""

  /** Ends the session (waits for a running command). Safe to call twice. */
  suspend fun close() {
    val n = native ?: return
    native = null
    n.linkDown()   // also wakes the loop
    pump?.cancelAndJoin()
    n.close()
    pending?.complete(CoreResult.ConnectFailed)
    pending = null
  }
}
