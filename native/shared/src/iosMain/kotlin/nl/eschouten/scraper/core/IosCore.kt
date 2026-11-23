// iOS side of the core: clean_c.h through cinterop, the robot driver linked in as libcore.a.
@file:OptIn(ExperimentalForeignApi::class)

package nl.eschouten.scraper.core

import kotlinx.cinterop.ByteVar
import kotlinx.cinterop.COpaquePointer
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.StableRef
import kotlinx.cinterop.UByteVar
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.allocArray
import kotlinx.cinterop.asStableRef
import kotlinx.cinterop.get
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.readBytes
import kotlinx.cinterop.reinterpret
import kotlinx.cinterop.staticCFunction
import kotlinx.cinterop.toCValues
import kotlinx.cinterop.toKString
import kotlinx.cinterop.usePinned
import nl.eschouten.scraper.core.c.clean_connect
import nl.eschouten.scraper.core.c.clean_encrypt_pin
import nl.eschouten.scraper.core.c.clean_feed
import nl.eschouten.scraper.core.c.clean_finish
import nl.eschouten.scraper.core.c.clean_free
import nl.eschouten.scraper.core.c.clean_image_version
import nl.eschouten.scraper.core.c.clean_owed_planning
import nl.eschouten.scraper.core.c.clean_read_routes
import nl.eschouten.scraper.core.c.clean_routes_json
import nl.eschouten.scraper.core.c.clean_set_owed_planning
import nl.eschouten.scraper.core.c.clean_last_error
import nl.eschouten.scraper.core.c.clean_link_down
import nl.eschouten.scraper.core.c.clean_new
import nl.eschouten.scraper.core.c.clean_on_progress
import nl.eschouten.scraper.core.c.clean_press
import nl.eschouten.scraper.core.c.clean_hold
import nl.eschouten.scraper.core.c.clean_release
import nl.eschouten.scraper.core.c.clean_unhold
import nl.eschouten.scraper.core.c.clean_cancel
import nl.eschouten.scraper.core.c.clean_result_text_lang
import nl.eschouten.scraper.core.c.clean_set_language
import nl.eschouten.scraper.core.c.clean_set_planning
import nl.eschouten.scraper.core.c.clean_start_route
import nl.eschouten.scraper.core.c.clean_links_json
import nl.eschouten.scraper.core.c.clean_status_json
import nl.eschouten.scraper.core.c.clean_stop_route
import nl.eschouten.scraper.core.c.clean_answer_stop

private fun callbacks(user: COpaquePointer?) = user!!.asStableRef<CoreCallbacks>().get()

private val onWriteC = staticCFunction { user: COpaquePointer?, data: CPointer<UByteVar>?, len: Int ->
  val bytes = data!!.reinterpret<ByteVar>().readBytes(len)
  clean_release(data)
  callbacks(user).onWrite(bytes)
}
private val onDoneC = staticCFunction { user: COpaquePointer?, command: Int, result: Int ->
  callbacks(user).onDone(command, result)
}
private val onProgressC = staticCFunction { user: COpaquePointer?, step: Int ->
  callbacks(user).onProgress(step)
}

private class IosCore(
  private val s: CPointer<cnames.structs.clean_session>,
  private val ref: StableRef<CoreCallbacks>,
) : CoreNative {
  override fun feed(data: ByteArray) = data.usePinned { clean_feed(s, it.addressOf(0).reinterpret(), data.size) }
  override fun linkDown() = clean_link_down(s)
  override fun connect() = clean_connect(s)
  override fun startRoute(letter: Char, dryRun: Boolean) = clean_start_route(s, letter.code.toByte(), if (dryRun) 1 else 0)
  override fun setPlanning(on: Boolean) = clean_set_planning(s, if (on) 1 else 0)
  override fun stopRoute() = clean_stop_route(s)
  override fun answerStop(finish: Boolean) = clean_answer_stop(s, if (finish) 1 else 0)
  override fun readRoutes() = clean_read_routes(s)
  override fun routesJson() = text { buf, size -> clean_routes_json(s, buf, size) }
  override fun press(key: Int) = clean_press(s, key)
  override fun hold(key: Int) = clean_hold(s, key)
  override fun unhold() = clean_unhold(s)
  override fun cancel() = clean_cancel(s)
  override fun finish() = clean_finish(s)
  override fun setOwedPlanning(owed: Int) = clean_set_owed_planning(s, owed)
  override fun owedPlanning() = clean_owed_planning(s)
  override fun statusJson() = text { buf, size -> clean_status_json(s, buf, size) }
  override fun lastError() = text { buf, size -> clean_last_error(s, buf, size) }

  override fun imageVersion(): ByteArray = memScoped {
    val out = allocArray<UByteVar>(9)
    val changed = clean_image_version(s, out) == 1
    ByteArray(10) { i -> if (i < 9) out[i].toByte() else if (changed) 1 else 0 }
  }

  override fun free() {
    clean_free(s) // waits for the core's thread: no callbacks after this
    ref.dispose()
  }

  private fun text(fn: (CPointer<ByteVar>?, Int) -> Int): String {
    val n = fn(null, 0)
    return memScoped {
      val buf = allocArray<ByteVar>(n + 1)
      fn(buf, n + 1)
      buf.toKString()
    }
  }
}

internal actual fun newCoreNative(pin: String, imageVersion: ByteArray?, language: String, callbacks: CoreCallbacks): CoreNative {
  val ref = StableRef.create(callbacks)
  val version = imageVersion?.let { v -> UByteArray(v.size) { v[it].toUByte() }.toCValues() }
  val s = clean_new(pin, version, onWriteC, onDoneC, ref.asCPointer()) ?: error("clean_new failed")
  clean_on_progress(s, onProgressC)
  clean_set_language(s, language)
  return IosCore(s, ref)
}

internal actual fun nativeResultText(code: Int, language: String): String =
  clean_result_text_lang(code, language)?.toKString() ?: ""

internal actual fun nativeEncryptPin(pin: String): ByteArray = memScoped {
  val buf = allocArray<UByteVar>(64)
  val n = clean_encrypt_pin(pin, buf, 64).coerceIn(0, 64)
  buf.reinterpret<ByteVar>().readBytes(n)
}

internal actual fun nativeLinksJson(): String = memScoped {
  val buf = allocArray<ByteVar>(1024)
  clean_links_json(buf, 1024)
  buf.toKString()
}
