// The desktop apps' updates: a feed signed by the release signer (firmware/sign_release.py --feed)
// is accepted, anything else refused; the newest full release is picked.
package nl.eschouten.scraper.app

import java.io.File
import java.nio.file.Files
import java.security.KeyPairGenerator
import java.security.spec.ECGenParameterSpec
import java.util.Base64
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class DesktopUpdatesTest {
  private val root = File("..", "..")   // tests run in native/shared
  private val feed = """{"Assets":[{"PackageId":"AgrigateClean","Version":"1.6.190","Type":"Full","FileName":"AgrigateClean-1.6.190-osx-full.nupkg","SHA256":"AB","Size":1},""" +
    """{"PackageId":"AgrigateClean","Version":"1.6.200","Type":"Delta","FileName":"d.nupkg","SHA256":"CD","Size":1},""" +
    """{"PackageId":"AgrigateClean","Version":"1.6.185","Type":"Full","FileName":"old.nupkg","SHA256":"EF","Size":1}]}"""

  private fun keys() = KeyPairGenerator.getInstance("EC").apply { initialize(ECGenParameterSpec("secp256r1")) }.generateKeyPair()

  /** The feed signed by the release signer with [key]'s private half. */
  private fun signed(key: java.security.KeyPair, text: String = feed): ByteArray {
    val dir = Files.createTempDirectory("feed").toFile()
    val pem = File(dir, "key.pem").apply {
      writeText("-----BEGIN PRIVATE KEY-----\n" + Base64.getMimeEncoder(64, "\n".toByteArray()).encodeToString(key.private.encoded) + "\n-----END PRIVATE KEY-----\n")
    }
    val f = File(dir, "releases.osx.json").apply { writeText(text) }
    val p = ProcessBuilder("python3", File(root, "firmware/sign_release.py").path, "--feed", f.path, "--key", pem.path).redirectErrorStream(true).start()
    val out = p.inputStream.readBytes()
    assertEquals(0, p.waitFor(), out.decodeToString())
    return out
  }

  @Test fun aFeedFromTheReleaseSignerIsAccepted() {
    val k = keys()
    assertContentEquals(feed.toByteArray(), DesktopUpdates.verifiedFeed(signed(k), k.public, "osx"))
  }

  @Test fun anotherKeySystemOrAChangedFeedIsRefused() {
    val k = keys()
    val env = signed(k).decodeToString()
    assertNull(DesktopUpdates.verifiedFeed(env.toByteArray(), keys().public, "osx"))
    assertNull(DesktopUpdates.verifiedFeed(env.toByteArray(), k.public, "win"))   // signed as the macOS feed
    val other = Base64.getEncoder().encodeToString(feed.replace("1.6.190", "1.6.999").toByteArray())
    val tampered = env.replace(Regex("\"feed\": \"[^\"]*\""), "\"feed\": \"$other\"")
    assertNull(DesktopUpdates.verifiedFeed(tampered.toByteArray(), k.public, "osx"))
    assertNull(DesktopUpdates.verifiedFeed("nonsense".toByteArray(), k.public, "osx"))
  }

  @Test fun theNewestFullReleaseAboveThisOne() {
    assertEquals("1.6.190", DesktopUpdates.newer(feed.toByteArray(), "1.6.183")?.version)
    assertEquals("AgrigateClean-1.6.190-osx-full.nupkg", DesktopUpdates.newer(feed.toByteArray(), "1.6.183")?.asset?.FileName)
    assertNull(DesktopUpdates.newer(feed.toByteArray(), "1.6.190"))
    assertNull(DesktopUpdates.newer(feed.toByteArray(), "1.7.0"))
    assertTrue(DesktopUpdates.compareVersions("1.6.10", "1.6.9") > 0)
  }

  @Test fun theAppsKeyIsTheButtonsKey() {
    val header = File(root, "firmware/src/ReleaseKey.h").readText()
    val pem = Regex("\"([A-Za-z0-9+/=]+)\\\\n\"").findAll(header).map { it.groupValues[1] }.joinToString("")
    assertEquals(pem, DesktopUpdates.RELEASE_KEY)
  }
}
