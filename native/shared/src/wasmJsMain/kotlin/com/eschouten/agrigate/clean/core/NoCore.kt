// The web page has no robot driver: the computer that serves the page runs it, and the page's
// robot goes through that computer's web API (api/ApiSession). Nothing on the page calls these.
package com.eschouten.agrigate.clean.core

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers

private fun onTheServer(): Nothing = error("the robot driver runs on the computer that serves the page, not in the browser")

internal actual fun openCoreNative(driver: String, pin: String, memory: String, language: String): CoreNative? = onTheServer()

internal actual fun nativeDriversJson(): String = "[]"

internal actual fun nativeEncryptPin(pin: String): ByteArray = onTheServer()

internal actual val waitingDispatcher: CoroutineDispatcher get() = Dispatchers.Default
