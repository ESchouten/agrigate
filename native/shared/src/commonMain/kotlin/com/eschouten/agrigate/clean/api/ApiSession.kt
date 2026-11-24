// A robot reached through another device's web API (docs/API.md): the buttons or a computer
// that holds the Bluetooth link. Behind the same ScraperSession as a robot over Bluetooth, so
// the controller, the screens and the widgets do not know the difference. Every command is a
// whole session on that device (connect, act, disconnect there); connect() reads the robot.
// For a type's own screen (the panel) the device keeps its connection open for as long as this
// session asks for the screen, and a held key for as long as it is repeated: a page or phone
// that goes away lets go by itself.
package com.eschouten.agrigate.clean.api

import io.ktor.client.HttpClient
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.request.header
import io.ktor.client.request.request
import io.ktor.client.statement.bodyAsText
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpMethod
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlin.concurrent.Volatile
import com.eschouten.agrigate.clean.app.RobotProfile
import com.eschouten.agrigate.clean.model.ScraperException
import com.eschouten.agrigate.clean.model.ScraperFault
import com.eschouten.agrigate.clean.model.ScraperSession
import com.eschouten.agrigate.clean.model.ScraperStatus
import com.eschouten.agrigate.clean.texts.tr

/** Every answer of the web API. [age]: seconds since [status] was read. */
@Serializable
data class ApiAnswer(
  val ok: Boolean,
  val fault: ScraperFault? = null,
  val message: String = "",
  val age: Long? = null,
  val status: ScraperStatus? = null,
)

internal val apiJson = Json { ignoreUnknownKeys = true; encodeDefaults = true; explicitNulls = true }

/** How often a session asks for the robot's screen, and repeats a held key. The device closes
 *  its connection [PANEL_FOR_MS] after the last ask, and lets a key go [KEY_FOR_MS] after the
 *  last repeat. */
const val PANEL_EVERY_MS = 300L
const val PANEL_FOR_MS = 5_000L
const val KEY_REPEAT_MS = 300L
const val KEY_FOR_MS = 1_000L

/** "agrigate-clean-1a2b.local" or "host:8080" -> "http://...", a full URL as it is. */
fun apiBase(via: String): String = via.trim().trimEnd('/').let { if ("://" in it) it else "http://$it" }

private val client by lazy {
  HttpClient {
    install(HttpTimeout) {
      connectTimeoutMillis = 5_000
      requestTimeoutMillis = 5 * 60_000   // a first connection syncs with the robot for minutes
    }
  }
}

class ApiSession(private val p: RobotProfile) : ScraperSession {
  private val base = apiBase(p.via)
  private var status = ScraperStatus()
  private var changes = 0
  private val closed = CompletableDeferred<Unit>()
  private val scope = CoroutineScope(Dispatchers.Default)

  override var isConnected = false
    private set

  override suspend fun awaitClosed() = closed.await()

  override suspend fun connect() {
    call(HttpMethod.Get, "status/fresh")
    isConnected = true
  }

  override suspend fun disconnect() {
    isConnected = false
    scope.cancel()
    closed.complete(Unit)
  }

  /** The device keeps its connection to the robot for as long as it is asked for the screen. */
  override suspend fun watch() {
    call(HttpMethod.Get, "panel")
    scope.launch {
      while (isActive) {
        delay(PANEL_EVERY_MS)
        runCatching { call(HttpMethod.Get, "panel") }.onFailure { if (it !is CancellationException) disconnect() }
      }
    }
  }

  /** A held key is repeated until [until]; the device lets go when the repeats stop. */
  override suspend fun input(name: String, until: Deferred<*>?) {
    if (until == null) return call(HttpMethod.Post, "panel/$name")
    try {
      do call(HttpMethod.Post, "panel/$name/down") while (withTimeoutOrNull(KEY_REPEAT_MS) { until.await() } == null)
    } finally {
      withContext(NonCancellable) { runCatching { call(HttpMethod.Post, "panel/$name/up") } }
    }
  }

  @Volatile private var acting = false

  /** While a command runs on the device, it may still be cancellable there. */
  override fun status() = if (acting) status.copy(cancellable = true) else status

  /** Cancel the command running on the device (a device that cannot answers 404: nothing happens). */
  override fun cancel() {
    if (acting) scope.launch { runCatching { call(HttpMethod.Post, "cancel") } }
  }
  override val revision: Any get() = changes

  override suspend fun startRoute(slot: String, dryRun: Boolean) = acting { call(HttpMethod.Post, "route/$slot" + if (dryRun) "/dryRun" else "") }
  override suspend fun setPlanning(on: Boolean) = acting { call(HttpMethod.Post, "planning/" + if (on) "on" else "off") }
  override suspend fun stopRoute() = call(HttpMethod.Post, "stop")
  override suspend fun answerStop(finish: Boolean) = call(HttpMethod.Post, "answer/" + if (finish) "yes" else "no")

  private suspend fun acting(block: suspend () -> Unit) {
    acting = true
    try {
      block()
    } finally {
      acting = false
    }
  }

  private suspend fun call(method: HttpMethod, path: String) {
    val res = try {
      client.request("$base/api/$path") {
        this.method = method
        header(HttpHeaders.Authorization, "Bearer ${p.pin}")
      }
    } catch (e: CancellationException) {
      throw e
    } catch (e: Exception) {
      throw ScraperException(ScraperFault.NotFound, tr.deviceUnreachable(p.via))
    }
    when (res.status.value) {
      401 -> throw ScraperException(ScraperFault.AuthRejected, tr.pinRefused)
      429 -> throw ScraperException(ScraperFault.AuthRejected, tr.tooManyTries)
      404 -> throw ScraperException(ScraperFault.Failed, tr.notSupported)
    }
    val a = runCatching { apiJson.decodeFromString<ApiAnswer>(res.bodyAsText()) }.getOrNull()
      ?: throw ScraperException(ScraperFault.Failed, tr.deviceUnreachable(p.via))
    a.status?.takeIf { it != status }?.let {
      status = it
      changes++
    }
    if (!a.ok) throw ScraperException(a.fault ?: ScraperFault.Failed, a.message)
  }
}
