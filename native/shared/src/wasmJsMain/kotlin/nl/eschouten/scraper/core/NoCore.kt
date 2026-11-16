// The web page has no robot driver: the barn computer runs it and the page talks to that
// (app/Remote.kt). Nothing on the page calls these.
package nl.eschouten.scraper.core

private fun onTheServer(): Nothing = error("the robot driver runs on the barn computer, not in the browser")

internal actual fun newCoreNative(pin: String, imageVersion: ByteArray?, language: String, callbacks: CoreCallbacks): CoreNative = onTheServer()

internal actual fun nativeResultText(code: Int, language: String): String = onTheServer()

internal actual fun nativeEncryptPin(pin: String): ByteArray = onTheServer()

internal actual fun nativeLinksJson(): String = "[]"
