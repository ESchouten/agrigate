package com.eschouten.agrigate.clean.texts

import platform.Foundation.NSLocale
import platform.Foundation.preferredLanguages

internal actual fun systemLanguage(): String =
  ((NSLocale.preferredLanguages.firstOrNull() as? String) ?: "en").substringBefore('-')
