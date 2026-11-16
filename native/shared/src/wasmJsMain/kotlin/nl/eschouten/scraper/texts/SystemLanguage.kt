package nl.eschouten.scraper.texts

/** The browser's language, "nl" from "nl-NL". */
internal actual fun systemLanguage(): String = navigatorLanguage().substringBefore('-')

private fun navigatorLanguage(): String = js("navigator.language || ''")
