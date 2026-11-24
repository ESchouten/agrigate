// iOS side of the core: the C APIs through cinterop, the drivers linked in as libcore.a.
@file:OptIn(ExperimentalForeignApi::class)

package com.eschouten.agrigate.clean.core

import kotlinx.cinterop.ByteVar
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.UByteVar
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.allocArray
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.readBytes
import kotlinx.cinterop.reinterpret
import kotlinx.cinterop.toKString
import kotlinx.cinterop.usePinned
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.IO
import com.eschouten.agrigate.clean.core.c.clean_answer_stop
import com.eschouten.agrigate.clean.core.c.clean_cancel
import com.eschouten.agrigate.clean.core.c.clean_connect
import com.eschouten.agrigate.clean.core.c.clean_finish
import com.eschouten.agrigate.clean.core.c.clean_input
import com.eschouten.agrigate.clean.core.c.clean_memory
import com.eschouten.agrigate.clean.core.c.clean_next_step
import com.eschouten.agrigate.clean.core.c.clean_read_routes
import com.eschouten.agrigate.clean.core.c.clean_release
import com.eschouten.agrigate.clean.core.c.clean_remember
import com.eschouten.agrigate.clean.core.c.clean_routes_json
import com.eschouten.agrigate.clean.core.c.clean_set_language
import com.eschouten.agrigate.clean.core.c.clean_set_planning
import com.eschouten.agrigate.clean.core.c.clean_start_route
import com.eschouten.agrigate.clean.core.c.clean_status_json
import com.eschouten.agrigate.clean.core.c.clean_stop_route
import com.eschouten.agrigate.clean.core.c.discovery_encrypt_pin
import com.eschouten.agrigate.clean.core.c.session_close
import com.eschouten.agrigate.clean.core.c.session_drivers_json
import com.eschouten.agrigate.clean.core.c.session_incoming
import com.eschouten.agrigate.clean.core.c.session_last_error
import com.eschouten.agrigate.clean.core.c.session_link_down
import com.eschouten.agrigate.clean.core.c.session_open
import com.eschouten.agrigate.clean.core.c.session_outgoing
import com.eschouten.agrigate.clean.core.c.session_result
import com.eschouten.agrigate.clean.core.c.session_wait

private fun text(fn: (CPointer<ByteVar>?, Int) -> Int): String {
  val n = fn(null, 0)
  return memScoped {
    val buf = allocArray<ByteVar>(n + 1)
    fn(buf, n + 1)
    buf.toKString()
  }
}

private class IosCore(private val s: CPointer<cnames.structs.session>) : CoreNative {
  override fun incoming(data: ByteArray) = data.usePinned { session_incoming(s, it.addressOf(0).reinterpret(), data.size, 0) }

  override fun outgoing(): ByteArray? {
    val n = session_outgoing(s, null, 0)
    if (n <= 0) return null
    return memScoped {
      val buf = allocArray<UByteVar>(n)
      session_outgoing(s, buf, n)
      buf.reinterpret<ByteVar>().readBytes(n)
    }
  }

  override fun linkDown() = session_link_down(s)
  override fun wait(ms: Int) = session_wait(s, ms)
  override fun result() = session_result(s)
  override fun connect() = clean_connect(s)
  override fun startRoute(letter: Char, dryRun: Boolean) = clean_start_route(s, letter.code.toByte(), if (dryRun) 1 else 0)
  override fun setPlanning(on: Boolean) = clean_set_planning(s, if (on) 1 else 0)
  override fun stopRoute() = clean_stop_route(s)
  override fun answerStop(finish: Boolean) = clean_answer_stop(s, if (finish) 1 else 0)
  override fun readRoutes() = clean_read_routes(s)
  override fun finish() = clean_finish(s)
  override fun input(name: String, hold: Boolean) = clean_input(s, name, if (hold) 1 else 0)
  override fun release() = clean_release(s)
  override fun cancel() = clean_cancel(s)
  override fun statusJson() = text { buf, size -> clean_status_json(s, buf, size) }
  override fun routesJson() = text { buf, size -> clean_routes_json(s, buf, size) }
  override fun nextStep() = text { buf, size -> clean_next_step(s, buf, size) }
  override fun memory() = text { buf, size -> clean_memory(s, buf, size) }
  override fun lastError() = text { buf, size -> session_last_error(s, buf, size) }
  override fun close() = session_close(s)
}

internal actual fun openCoreNative(driver: String, pin: String, memory: String, language: String): CoreNative? {
  val s = session_open(driver, "", "", pin) ?: return null
  clean_set_language(s, language)
  clean_remember(s, memory)
  return IosCore(s)
}

internal actual fun nativeDriversJson(): String = text { buf, size -> session_drivers_json(buf, size) }

internal actual fun nativeEncryptPin(pin: String): ByteArray = memScoped {
  val buf = allocArray<UByteVar>(64)
  val n = discovery_encrypt_pin(pin, buf, 64).coerceIn(0, 64)
  buf.reinterpret<ByteVar>().readBytes(n)
}

internal actual val waitingDispatcher: CoroutineDispatcher get() = Dispatchers.IO
