// The robot API, same names as Python and the M5Stack. All logic runs in the robot driver
// (C++); this class only joins it to a [RobotLink].
package com.eschouten.agrigate.clean.core

import kotlinx.coroutines.Deferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.concurrent.Volatile
import com.eschouten.agrigate.clean.texts.tr

/** The link to the robot: bytes to it and bytes from it. Each platform provides one per kind
 *  of link the drivers ask for (link/LinkSpec.kt). */
interface RobotLink {
  /** Identifier to remember the robot by (MAC on Android, UUID on iOS). */
  val id: String

  /** Open the link and subscribe to notifications. */
  suspend fun connect()

  /** Bytes from the robot, from [connect] until the link drops. */
  val notifications: Flow<ByteArray>

  /** Returns when the link drops (or after [disconnect]). */
  suspend fun awaitClosed()

  suspend fun write(frame: ByteArray)
  suspend fun disconnect()
}

open class RobotException(val result: CoreResult, message: String) : Exception(message)

/** Robot is not on the home screen or route menu (driving?): nothing was pressed. */
class RobotBusy(result: CoreResult, message: String) : RobotException(result, message)

class PinRejected(result: CoreResult, message: String) : RobotException(result, message)

class Robot(
  val link: RobotLink,
  val pin: String? = null,
  /** What [onMemory] gave last time for this robot: it skips the ~75 s of a first connection
   *  and tells what an earlier session still owed the robot after its link dropped. */
  memory: String = "",
  /** Optional progress callback (connect, PIN ok, route menu, Start, ...). */
  val onStep: ((CoreStep) -> Unit)? = null,
  /** Language of error messages: "nl" or "en". */
  val language: String = "nl",
  /** The driver has something else to keep for this robot (clean_c.h): store it with the
   *  robot for the next session. */
  val onMemory: ((String) -> Unit)? = null,
) {
  var memory: String = memory
    private set

  private var core: CoreSession? = null
  private var scope: CoroutineScope? = null
  private var writes: Channel<ByteArray>? = null
  private var writer: Job? = null

  val isConnected: Boolean get() = core != null

  @Volatile private var cancelled = false

  /** The user cancelled (any thread): the running command ends with [CoreResult.Cancelled],
   *  the robot back as it was ([CoreSession.cancel]); also while the link still opens. */
  fun cancel() {
    cancelled = true
    core?.cancel()
  }

  suspend fun connect() {
    cancelled = false
    link.connect()
    if (cancelled) {
      link.disconnect()
      throw RobotException(CoreResult.Cancelled, tr.cancelledBody)
    }
    val s = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    val out = Channel<ByteArray>(Channel.UNLIMITED)
    val c = CoreSession(onWrite = { out.trySend(it) }, pin = pin, memory = memory, onStep = onStep, language = language)
    core = c
    scope = s
    writes = out
    s.launch { link.notifications.collect { c.feed(it) } }
    s.launch {
      link.awaitClosed()
      c.linkDown()
    }
    writer = s.launch {
      for (frame in out) {
        try {
          link.write(frame)
        } catch (e: Exception) {
          c.linkDown()
        }
      }
    }
    val r = if (cancelled) CoreResult.Cancelled else c.connect()
    noteMemory()
    if (r != CoreResult.Ok) {
      val e = error(r)
      disconnect()
      throw e
    }
  }

  fun status(): CoreStatus = core?.status() ?: CoreStatus()

  suspend fun startRoute(letter: Char, dryRun: Boolean = false) = run { it.startRoute(letter, dryRun) }
  suspend fun setPlanning(on: Boolean) = run { it.setPlanning(on) }

  /** Safety stop: one tap on Start/Stop, only while a route runs ([CoreStatus.driving]). */
  suspend fun stopRoute() = run { it.stopRoute() }

  /** The user's answer to "route afmaken?" after a stop ([CoreStatus.stopQuestion]). */
  suspend fun answerStop(finish: Boolean) = run { it.answerStop(finish) }

  /** What routes A..D consist of on the robot, e.g. "C" -> "1 2 3". Only reads. */
  suspend fun readRoutes(): Map<String, String> {
    run { it.readRoutes() }
    return core?.routes() ?: emptyMap()
  }
  suspend fun press(key: RobotKey) = run { it.press(key) }

  /** [key] held down as on the robot's own panel until [up] completes: in manual control the
   *  robot drives while Start/Stop is held. Returns after the key went up. */
  suspend fun hold(key: RobotKey, up: Deferred<*>) = run { it.hold(key, up) }

  /** Leaves the robot as it was found: back to the home screen from a safe menu and the
   *  planning as it was at connect (unless [setPlanning] changed it on purpose). */
  suspend fun disconnect() {
    val c = core
    if (c != null) withTimeoutOrNull(30_000) { c.finish() }
    noteMemory()
    writes?.close()
    withTimeoutOrNull(1_000) { writer?.join() } // let the last frames (e.g. Back) go out
    c?.close()
    core = null
    scope?.cancel()
    scope = null
    writes = null
    writer = null
    link.disconnect()
  }

  private suspend fun run(command: suspend (CoreSession) -> CoreResult) {
    val c = core ?: throw RobotException(CoreResult.ConnectFailed, tr.notConnected)
    cancelled = false
    val r = command(c)
    noteMemory()
    if (r != CoreResult.Ok) throw error(r)
  }

  private fun noteMemory() {
    val m = core?.memory() ?: return
    if (m == memory) return
    memory = m
    onMemory?.invoke(m)
  }

  private fun error(r: CoreResult): RobotException {
    val why = if (r == CoreResult.InProgress) tr.busy else core?.lastError() ?: ""
    return when (r) {
      CoreResult.Busy -> RobotBusy(r, why)
      CoreResult.PinRejected -> PinRejected(r, why)
      else -> RobotException(r, why)
    }
  }
}
