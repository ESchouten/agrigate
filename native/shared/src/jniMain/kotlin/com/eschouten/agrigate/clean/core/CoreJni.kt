// JNI side of core/jni/clean_jni.cpp, shared by Android and the desktop JVM.
package com.eschouten.agrigate.clean.core

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers

internal object CoreJni {
  init { loadCoreLibrary() }

  external fun nativeOpen(driver: String, pin: String, memory: String, language: String): Long
  external fun nativeClose(h: Long)
  external fun nativeIncoming(h: Long, data: ByteArray)
  external fun nativeOutgoing(h: Long): ByteArray?
  external fun nativeLinkDown(h: Long)
  external fun nativeWait(h: Long, ms: Int)
  external fun nativeResult(h: Long): Int
  external fun nativeConnect(h: Long): Int
  external fun nativeStartRoute(h: Long, letter: Char, dryRun: Boolean): Int
  external fun nativeSetPlanning(h: Long, on: Boolean): Int
  external fun nativeStopRoute(h: Long): Int
  external fun nativeAnswerStop(h: Long, finish: Boolean): Int
  external fun nativeReadRoutes(h: Long): Int
  external fun nativeFinish(h: Long): Int
  external fun nativeInput(h: Long, name: String, hold: Boolean): Int
  external fun nativeRelease(h: Long)
  external fun nativeCancel(h: Long)
  external fun nativeStatusJson(h: Long): String
  external fun nativeRoutesJson(h: Long): String
  external fun nativeNextStep(h: Long): String
  external fun nativeMemory(h: Long): String
  external fun nativeLastError(h: Long): String
  external fun nativeDriversJson(): String
  external fun nativeEncryptPin(pin: String): ByteArray
}

private class JniCore(private val h: Long) : CoreNative {
  override fun incoming(data: ByteArray) = CoreJni.nativeIncoming(h, data)
  override fun outgoing() = CoreJni.nativeOutgoing(h)
  override fun linkDown() = CoreJni.nativeLinkDown(h)
  override fun wait(ms: Int) = CoreJni.nativeWait(h, ms)
  override fun result() = CoreJni.nativeResult(h)
  override fun connect() = CoreJni.nativeConnect(h)
  override fun startRoute(letter: Char, dryRun: Boolean) = CoreJni.nativeStartRoute(h, letter, dryRun)
  override fun setPlanning(on: Boolean) = CoreJni.nativeSetPlanning(h, on)
  override fun stopRoute() = CoreJni.nativeStopRoute(h)
  override fun answerStop(finish: Boolean) = CoreJni.nativeAnswerStop(h, finish)
  override fun readRoutes() = CoreJni.nativeReadRoutes(h)
  override fun finish() = CoreJni.nativeFinish(h)
  override fun input(name: String, hold: Boolean) = CoreJni.nativeInput(h, name, hold)
  override fun release() = CoreJni.nativeRelease(h)
  override fun cancel() = CoreJni.nativeCancel(h)
  override fun statusJson() = CoreJni.nativeStatusJson(h)
  override fun routesJson() = CoreJni.nativeRoutesJson(h)
  override fun nextStep() = CoreJni.nativeNextStep(h)
  override fun memory() = CoreJni.nativeMemory(h)
  override fun lastError() = CoreJni.nativeLastError(h)
  override fun close() = CoreJni.nativeClose(h)
}

internal actual fun openCoreNative(driver: String, pin: String, memory: String, language: String): CoreNative? =
  CoreJni.nativeOpen(driver, pin, memory, language).takeIf { it != 0L }?.let(::JniCore)

internal actual fun nativeDriversJson(): String = CoreJni.nativeDriversJson()

internal actual fun nativeEncryptPin(pin: String): ByteArray = CoreJni.nativeEncryptPin(pin)

internal actual val waitingDispatcher: CoroutineDispatcher get() = Dispatchers.IO
