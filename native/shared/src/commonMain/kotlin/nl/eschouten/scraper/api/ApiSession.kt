// A robot reached through another device's web API (docs/API.md): the buttons or a computer
// that holds the Bluetooth link. Behind the same ScraperSession as a robot over Bluetooth, so
// the controller, the screens and the widgets do not know the difference. Every command is a
// whole session on that device (connect, act, disconnect there); connect() reads the robot.
package nl.eschouten.scraper.api

import io.ktor.client.HttpClient
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.request.header
import io.ktor.client.request.request
import io.ktor.client.statement.bodyAsText
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpMethod
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import nl.eschouten.scraper.app.RobotProfile
import nl.eschouten.scraper.model.ScraperException
import nl.eschouten.scraper.model.ScraperFault
import nl.eschouten.scraper.model.ScraperSession
import nl.eschouten.scraper.model.ScraperStatus
import nl.eschouten.scraper.texts.tr

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

/** "scraper-control-1a2b.local" or "host:8080" -> "http://...", a full URL as it is. */
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

  override var isConnected = false
    private set

  override suspend fun awaitClosed() = closed.await()

  override suspend fun connect() {
    call(HttpMethod.Get, "status/fresh")
    isConnected = true
  }

  override suspend fun disconnect() {
    isConnected = false
    closed.complete(Unit)
  }

  override fun status() = status
  override val revision: Any get() = changes

  override suspend fun startRoute(slot: String, dryRun: Boolean) = call(HttpMethod.Post, "route/$slot" + if (dryRun) "/dryRun" else "")
  override suspend fun setPlanning(on: Boolean) = call(HttpMethod.Post, "planning/" + if (on) "on" else "off")
  override suspend fun stopRoute() = call(HttpMethod.Post, "stop")
  override suspend fun answerStop(finish: Boolean) = call(HttpMethod.Post, "answer/" + if (finish) "yes" else "no")

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
    a.status?.let {
      status = it
      changes++
    }
    if (!a.ok) throw ScraperException(a.fault ?: ScraperFault.Failed, a.message)
  }
}
