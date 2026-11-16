// The web page: the same screens as the apps, while one computer in the barn (the desktop app,
// Settings > Web page) keeps the robot connection. The page's RemoteController sends each command
// as a [Call]; the barn computer's RemoteHost runs it on its own RobotController and sends the
// state after every change. So there is one robot connection, however many pages are open, and
// every rule (planning back on, the finish question, ...) stays in one place.
//
// A key held on the page (manual driving) ends when the page lets go, goes quiet for a second
// (it sends [Call.Still] while holding) or drops: the robot stops as on a Bluetooth drop.
package nl.eschouten.scraper.app

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import nl.eschouten.scraper.model.FoundScraper
import nl.eschouten.scraper.model.ScraperModel
import nl.eschouten.scraper.model.modelFor
import nl.eschouten.scraper.texts.tr
import nl.eschouten.scraper.texts.useLanguage
import nl.eschouten.scraper.ui.newScope
import kotlin.time.Clock

/** A command from the page, one per [Controller] function. */
@Serializable
sealed interface Call {
  @Serializable @SerialName("route") data class StartRoute(val route: String, val dryRun: Boolean) : Call
  @Serializable @SerialName("planning") data class SetPlanning(val on: Boolean) : Call
  @Serializable @SerialName("refresh") data object Refresh : Call
  @Serializable @SerialName("readRoutes") data object ReadRoutes : Call
  @Serializable @SerialName("hold") data object Hold : Call
  @Serializable @SerialName("release") data object Release : Call
  /** [held]: until [InputUp]. */
  @Serializable @SerialName("input") data class Input(val name: String, val held: Boolean) : Call
  @Serializable @SerialName("inputUp") data object InputUp : Call
  /** "Still holding", every [STILL_MS] while an input is held. */
  @Serializable @SerialName("still") data object Still : Call
  @Serializable @SerialName("finish") data class AnswerFinish(val finish: Boolean?) : Call
  @Serializable @SerialName("discover") data class Discover(val model: String, val name: String?) : Call
  @Serializable @SerialName("select") data class SelectRobot(val uid: String) : Call
  @Serializable @SerialName("add") data class AddRobot(val robot: RobotProfile) : Call
  @Serializable @SerialName("remove") data class RemoveRobot(val uid: String) : Call
  @Serializable @SerialName("update") data class UpdateRobot(val robot: RobotProfile) : Call
  @Serializable @SerialName("settings") data class UpdateSettings(val flags: AppFlags) : Call
  @Serializable @SerialName("clearHistory") data object ClearHistory : Call
}

/** Page -> barn computer. */
@Serializable
data class Request(val id: Int, val call: Call)

/** Barn computer -> page: the state after a change, or request [Done.id] is done. */
@Serializable
sealed interface Reply {
  @Serializable @SerialName("state") data class State(val state: ControllerState) : Reply
  @Serializable @SerialName("done") data class Done(val id: Int, val outcome: Outcome? = null, val found: List<FoundScraper> = emptyList()) : Reply
}

private val wire = Json { ignoreUnknownKeys = true }

/** How often the page says it still holds a key, and how long the barn computer waits for that. */
const val STILL_MS = 250L
private const val QUIET_MS = 1_000L

/** The page's controller. [send] a text to the barn computer; give it every text that comes
 *  back ([receive]) and say when the connection dropped ([closed]). Runs on the browser's one thread. */
class RemoteController(private val send: (String) -> Unit, private val scope: CoroutineScope = newScope()) : Controller {
  private val _state = MutableStateFlow(ControllerState())
  override val state: StateFlow<ControllerState> = _state

  /** True once the barn computer sent its state (and stays so across reconnects). */
  private val _ready = MutableStateFlow(false)
  val ready: StateFlow<Boolean> = _ready

  private var next = 0
  private val waiting = mutableMapOf<Int, CompletableDeferred<Reply.Done>>()

  fun receive(text: String) {
    when (val r = wire.decodeFromString<Reply>(text)) {
      is Reply.State -> {
        if (r.state.language != _state.value.language || !_ready.value) useLanguage(r.state.language)
        _state.value = r.state
        _ready.value = true
      }
      is Reply.Done -> waiting.remove(r.id)?.complete(r)
    }
  }

  /** The connection dropped: commands under way fail (a held key is let go on the other side). */
  fun closed() {
    val gone = waiting.values.toList()
    waiting.clear()
    gone.forEach { it.complete(Reply.Done(-1, Outcome(false, tr.barnComputerGone, Problem.NotFound))) }
  }

  private fun post(c: Call, id: Int = 0) {
    runCatching { send(wire.encodeToString(Request.serializer(), Request(id, c))) }
  }

  private suspend fun call(c: Call): Reply.Done {
    val id = ++next
    val d = CompletableDeferred<Reply.Done>()
    waiting[id] = d
    post(c, id)
    return d.await()
  }

  private suspend fun outcome(c: Call) = call(c).outcome ?: Outcome(false, tr.barnComputerGone, Problem.NotFound)

  override suspend fun startRoute(route: String, dryRun: Boolean) = outcome(Call.StartRoute(route, dryRun))
  override suspend fun setPlanning(on: Boolean) = outcome(Call.SetPlanning(on))
  override suspend fun refresh() = outcome(Call.Refresh)
  override suspend fun readRoutes() = outcome(Call.ReadRoutes)
  override suspend fun hold() = outcome(Call.Hold)
  override suspend fun release() {
    call(Call.Release)
  }

  override suspend fun input(name: String, until: Deferred<*>?): Outcome {
    if (until == null) return outcome(Call.Input(name, held = false))
    val still = scope.launch {
      while (isActive && !until.isCompleted) {
        delay(STILL_MS)
        post(Call.Still)
      }
    }
    val up = scope.launch {
      until.await()
      post(Call.InputUp)
    }
    return try {
      outcome(Call.Input(name, held = true))
    } finally {
      still.cancel()
      up.cancel()
    }
  }

  override fun answerFinish(finish: Boolean?) = post(Call.AnswerFinish(finish))

  override suspend fun discover(model: ScraperModel, name: String?): List<FoundScraper> {
    val d = call(Call.Discover(model.id, name))
    d.outcome?.takeIf { !it.ok }?.let { throw Exception(it.message) }
    return d.found
  }

  override suspend fun selectRobot(uid: String) {
    call(Call.SelectRobot(uid))
  }

  override suspend fun addRobot(p: RobotProfile) {
    call(Call.AddRobot(p))
  }

  override suspend fun removeRobot(uid: String) {
    call(Call.RemoveRobot(uid))
  }

  override suspend fun updateRobot(p: RobotProfile) {
    call(Call.UpdateRobot(p))
  }

  override suspend fun updateSettings(change: (AppFlags) -> AppFlags) {
    call(Call.UpdateSettings(change(_state.value.flags)))
  }

  override fun clearHistory() = post(Call.ClearHistory)
}

/** One page's connection on the barn computer: runs its calls on [controller]. [send] a text to
 *  the page; give it every text from the page ([receive]) and call [close] when it drops. */
class RemoteHost(
  private val controller: RobotController,
  private val send: suspend (String) -> Unit,
  private val scope: CoroutineScope,
  private val now: () -> Long = { Clock.System.now().toEpochMilliseconds() },
) {
  private val out = Mutex()
  private var sent = -1L
  private var holds = 0
  private var up: CompletableDeferred<Unit>? = null
  private var heard = now()
  private val updates = scope.launch { controller.state.collect { sendState(it) } }

  fun receive(text: String) {
    val r = runCatching { wire.decodeFromString<Request>(text) }.getOrNull() ?: return
    heard = now()
    // These act at once, in the order they came (a quick tap is a press and a let-go).
    val until = when (val c = r.call) {
      Call.Still -> return
      Call.InputUp -> {
        up?.complete(Unit)
        return
      }
      is Call.AnswerFinish -> {
        controller.answerFinish(c.finish)
        return
      }
      is Call.Input -> if (c.held) CompletableDeferred<Unit>().also { u -> up?.complete(Unit); up = u } else null
      else -> null
    }
    scope.launch { run(r, until) }
  }

  /** The page is gone: let go of its key and of its hold on the connection. */
  suspend fun close() {
    updates.cancel()
    up?.complete(Unit)
    repeat(holds) { controller.release() }
    holds = 0
  }

  private suspend fun run(r: Request, until: CompletableDeferred<Unit>?) {
    var found = emptyList<FoundScraper>()
    val o: Outcome? = when (val c = r.call) {
      is Call.StartRoute -> controller.startRoute(c.route, c.dryRun)
      is Call.SetPlanning -> controller.setPlanning(c.on)
      Call.Refresh -> controller.refresh()
      Call.ReadRoutes -> controller.readRoutes()
      Call.Hold -> controller.hold().also { if (it.ok) holds++ }
      Call.Release -> {
        if (holds > 0) {
          holds--
          controller.release()
        }
        null
      }
      is Call.Input -> {
        val watch = until?.let { u ->
          scope.launch {
            while (!u.isCompleted) {
              delay(STILL_MS)
              if (now() - heard > QUIET_MS) u.complete(Unit)
            }
          }
        }
        try {
          controller.input(c.name, until)
        } finally {
          watch?.cancel()
        }
      }
      is Call.Discover -> try {
        found = controller.discover(modelFor(c.model), c.name)
        null
      } catch (e: kotlinx.coroutines.CancellationException) {
        throw e
      } catch (e: Exception) {
        Outcome(false, e.message ?: e.toString())
      }
      is Call.SelectRobot -> {
        controller.selectRobot(c.uid)
        null
      }
      is Call.AddRobot -> {
        controller.addRobot(c.robot)
        null
      }
      is Call.RemoveRobot -> {
        controller.removeRobot(c.uid)
        null
      }
      is Call.UpdateRobot -> {
        controller.updateRobot(c.robot)
        null
      }
      is Call.UpdateSettings -> {
        controller.updateSettings { c.flags }
        null
      }
      Call.ClearHistory -> {
        controller.clearHistory()
        null
      }
      Call.Still, Call.InputUp, is Call.AnswerFinish -> null
    }
    // The state first, so the page sees what the call changed when it carries on.
    sendState(controller.state.value)
    reply(Reply.Done(r.id, o, found))
  }

  private suspend fun sendState(s: ControllerState) = out.withLock {
    if (s.revision <= sent) return@withLock
    sent = s.revision
    runCatching { send(wire.encodeToString(Reply.serializer(), Reply.State(s))) }
  }

  private suspend fun reply(r: Reply) = out.withLock {
    runCatching { send(wire.encodeToString(Reply.serializer(), r)) }
  }
}
