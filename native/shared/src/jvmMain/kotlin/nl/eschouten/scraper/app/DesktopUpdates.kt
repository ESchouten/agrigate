// Updates of the desktop app on Windows and macOS (Linux: apt), the way the buttons update: from
// the latest GitHub release, and only what the release key signed. A release signs each system's
// Velopack feed (firmware/sign_release.py --feed); the app checks that signature, downloads the
// package, checks its SHA-256 against the signed feed, and hands Velopack (core/jni/
// updates_jni.cpp) a folder with just those two. Velopack puts the new version in place once the
// app has closed and starts it.
package nl.eschouten.scraper.app

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import nl.eschouten.scraper.core.loadCoreLibrary
import java.io.File
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.PublicKey
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64

object DesktopUpdates {
  /** Where releases are; SCRAPER_UPDATES: another server laid out the same (a test of an update). */
  private val releases = System.getenv("SCRAPER_UPDATES") ?: "https://github.com/ESchouten/scraper-control/releases"
  /** What the release signs before a system's feed (firmware/sign_release.py --feed): a feed
   *  signed for one system is not one for another. */
  internal fun prefix(channel: String) = "scraper-control desktop feed v1 $channel\n"

  /** The release key: the same as the buttons' (firmware/src/ReleaseKey.h). */
  internal const val RELEASE_KEY = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE5+BVstQnXROhuybMANmQsU1sRAZK" +
    "TB7yUjvfV7I+WLhYFhBkSv4gVirRK6oehdckBqIeV8QmclcXYdsj4SsvLw=="

  /** Velopack's channel for this system (only Windows and macOS have Velopack). */
  private val channel = if (System.getProperty("os.name").lowercase().startsWith("mac")) "osx" else "win"

  @Serializable internal data class Envelope(val feed: String, val sig: String)
  @Serializable internal data class Feed(val Assets: List<Asset> = emptyList())
  @Serializable internal data class Asset(val Version: String, val Type: String, val FileName: String, val SHA256: String, val Size: Long = 0)

  /** A newer signed version: [feed] the verified feed's bytes, [asset] its full package. */
  class Update internal constructor(val version: String, internal val feed: ByteArray, internal val asset: Asset)

  private val json = Json { ignoreUnknownKeys = true }
  private val http by lazy { HttpClient.newBuilder().followRedirects(HttpClient.Redirect.NORMAL).build() }

  @JvmStatic private external fun nativeAppRun(args: Array<String>)
  @JvmStatic private external fun nativeInstalledVersion(): String
  @JvmStatic private external fun nativeApply(dir: String): String

  /** Where an update is put together; left behind by the update itself, cleared at the next start. */
  private val work = File(System.getProperty("java.io.tmpdir"), "scraper-control-update")

  /** First thing in main(): Velopack's install and update hooks run (and exit) here. */
  fun appRun(args: Array<String>) {
    runCatching { loadCoreLibrary(); nativeAppRun(args) }
    work.deleteRecursively()
  }

  /** The installed version; "" when run from a build (no updates then). */
  private val installedVersion: String by lazy { runCatching { loadCoreLibrary(); nativeInstalledVersion() }.getOrDefault("") }

  /** The latest release's version for this system when it is newer and its feed is signed; null otherwise. */
  suspend fun check(): Update? = withContext(Dispatchers.IO) {
    if (installedVersion.isEmpty()) return@withContext null
    val bytes = get("$releases/latest/download/releases.$channel.signed.json") ?: return@withContext null
    val feed = verifiedFeed(bytes) ?: return@withContext null
    newer(feed, installedVersion)
  }

  /** Download, check and, once [idle] returns (no robot session running), hand to Velopack;
   *  [beforeExit] runs, the app quits and comes back as the new version. Returns why not when it
   *  does not get that far. */
  suspend fun install(u: Update, idle: suspend () -> Unit = {}, beforeExit: () -> Unit = {}): String = withContext(Dispatchers.IO) {
    val dir = work.apply { deleteRecursively(); mkdirs() }
    val pkg = File(dir, u.asset.FileName)
    if (!download("$releases/download/v${u.version}/${u.asset.FileName}", pkg)) return@withContext "download failed"
    if (!sha256(pkg).equals(u.asset.SHA256, ignoreCase = true)) return@withContext "the download does not match the signed release"
    File(dir, "releases.$channel.json").writeBytes(u.feed)
    idle()
    nativeApply(dir.absolutePath).ifEmpty {
      beforeExit()
      kotlin.system.exitProcess(0)
    }
  }

  /** The feed inside [envelope] when [key] signed it for [channel], else null. */
  internal fun verifiedFeed(envelope: ByteArray, key: PublicKey = releaseKey(), channel: String = this.channel): ByteArray? = runCatching {
    val e = json.decodeFromString<Envelope>(envelope.decodeToString())
    val feed = Base64.getDecoder().decode(e.feed)
    val s = Signature.getInstance("SHA256withECDSA")
    s.initVerify(key)
    s.update(prefix(channel).toByteArray() + feed)
    if (s.verify(e.sig.chunked(2).map { it.toInt(16).toByte() }.toByteArray())) feed else null
  }.getOrNull()

  /** The newest full release in [feed] above [current], or null. */
  internal fun newer(feed: ByteArray, current: String): Update? {
    val f = runCatching { json.decodeFromString<Feed>(feed.decodeToString()) }.getOrNull() ?: return null
    val best = f.Assets.filter { it.Type == "Full" }.maxWithOrNull { a, b -> compareVersions(a.Version, b.Version) } ?: return null
    return if (compareVersions(best.Version, current) > 0) Update(best.Version, feed, best) else null
  }

  internal fun compareVersions(a: String, b: String): Int {
    val x = a.split('.', '-').map { it.toIntOrNull() ?: 0 }
    val y = b.split('.', '-').map { it.toIntOrNull() ?: 0 }
    for (i in 0 until maxOf(x.size, y.size)) {
      val c = (x.getOrElse(i) { 0 }).compareTo(y.getOrElse(i) { 0 })
      if (c != 0) return c
    }
    return 0
  }

  private fun releaseKey(): PublicKey =
    KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(Base64.getDecoder().decode(RELEASE_KEY)))

  private fun sha256(f: File): String {
    val md = MessageDigest.getInstance("SHA-256")
    f.forEachBlock { block, n -> md.update(block, 0, n) }   // in blocks: packages are ~100 MB
    return md.digest().joinToString("") { "%02x".format(it) }
  }

  private fun request(url: String) = HttpRequest.newBuilder(URI(url)).header("User-Agent", "scraper-control").build()

  private fun get(url: String): ByteArray? = runCatching {
    val r = http.send(request(url), HttpResponse.BodyHandlers.ofByteArray())
    if (r.statusCode() == 200) r.body() else null
  }.getOrNull()

  /** Into [to], streamed. */
  private fun download(url: String, to: File): Boolean = runCatching {
    http.send(request(url), HttpResponse.BodyHandlers.ofFile(to.toPath())).statusCode() == 200
  }.getOrDefault(false)
}
