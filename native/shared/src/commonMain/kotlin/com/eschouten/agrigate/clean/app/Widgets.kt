// The home-screen widgets live outside the shared module (Android AppWidget, iOS WidgetKit).
// The apps hook in here: after a run from the app, the widget shows the same result line.
package com.eschouten.agrigate.clean.app

object Widgets {
  data class Report(val robotUid: String, val route: String, val line: String, val short: String)

  /** Set by the platform app. */
  var report: ((Report) -> Unit)? = null
}
