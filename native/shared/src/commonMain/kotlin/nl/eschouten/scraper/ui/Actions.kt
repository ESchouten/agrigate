// What a tap does, where it changes something: both apps call these, so a tap does the same
// thing everywhere. Each returns the message for a toast (or null for none).
package nl.eschouten.scraper.ui

import nl.eschouten.scraper.app.Controller
import nl.eschouten.scraper.app.RobotProfile
import nl.eschouten.scraper.model.FoundScraper
import nl.eschouten.scraper.texts.tr

object Actions {
  /** The state pill and pull-to-refresh: read the robot once. */
  suspend fun refresh(c: Controller): String? = c.refresh().let { if (it.ok) null else it.message }

  /** After [planningConfirm] said yes. */
  suspend fun togglePlanning(c: Controller): String {
    val on = c.state.value.snapshot?.planning ?: false
    return c.setPlanning(!on).message
  }

  suspend fun dismissWidgetTip(c: Controller) = c.updateSettings { it.copy(widgetTipSeen = true) }

  suspend fun setLanguage(c: Controller, code: String) {
    if (code != c.state.value.language) c.updateSettings { it.copy(language = code) }
  }

  /** Back to the welcome screen (the robots stay). */
  suspend fun resetSetup(c: Controller) = c.updateSettings { it.copy(onboarded = false) }

  /** "Test connection" in a robot's settings: makes it the active robot first. */
  suspend fun testConnection(c: Controller, uid: String): String {
    c.selectRobot(uid)
    val o = c.refresh()
    return if (o.ok) tr.connectionWorks else o.message
  }

  /** Read what routes A..D consist of on the robot; the answer for a toast. */
  suspend fun readRoutes(c: Controller, uid: String): String {
    c.selectRobot(uid)
    return c.readRoutes().message
  }

  suspend fun selectRobot(c: Controller, uid: String) = c.selectRobot(uid)

  suspend fun removeRobot(c: Controller, uid: String) = c.removeRobot(uid)

  /** Changes robot [uid]'s profile (nothing when it was removed meanwhile). */
  private suspend fun update(c: Controller, uid: String, change: (RobotProfile) -> RobotProfile) {
    c.state.value.robots.firstOrNull { it.uid == uid }?.let { c.updateRobot(change(it)) }
  }

  suspend fun rename(c: Controller, uid: String, name: String) = update(c, uid) { it.copy(label = name.trim()) }

  suspend fun setPin(c: Controller, uid: String, pin: String) = update(c, uid) { it.copy(pin = pin.trim()) }

  suspend fun renameRoute(c: Controller, uid: String, route: String, name: String) = update(c, uid) { it.withRouteName(route, name) }

  suspend fun showRoute(c: Controller, uid: String, route: String, shown: Boolean) = update(c, uid) { it.withRoute(route, shown) }

  /** The device picker in a robot's settings. */
  suspend fun chooseDevice(c: Controller, uid: String, f: FoundScraper) = update(c, uid) { it.withDevice(f) }

  fun clearLogbook(c: Controller) = c.clearHistory()
}
