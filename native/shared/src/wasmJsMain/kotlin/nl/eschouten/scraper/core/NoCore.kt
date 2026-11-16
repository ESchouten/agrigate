// The web page has no robot driver: the computer that serves the page runs it, and the page's
// robot goes through that computer's web API (api/ApiSession). Nothing on the page calls these.
package nl.eschouten.scraper.core

private fun onTheServer(): Nothing = error("the robot driver runs on the computer that serves the page, not in the browser")

internal actual fun newCoreNative(pin: String, imageVersion: ByteArray?, language: String, callbacks: CoreCallbacks): CoreNative = onTheServer()

internal actual fun nativeResultText(code: Int, language: String): String = onTheServer()

internal actual fun nativeEncryptPin(pin: String): ByteArray = onTheServer()

internal actual fun nativeLinksJson(): String = "[]"
