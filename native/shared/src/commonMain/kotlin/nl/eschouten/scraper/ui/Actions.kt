// What a tap does, where it changes something: both apps call these, so a tap does the same
// thing everywhere. Each returns the message for a toast (or null for none).
package nl.eschouten.scraper.ui

import nl.eschouten.scraper.app.RobotController
import nl.eschouten.scraper.app.RobotProfile
import nl.eschouten.scraper.model.FoundScraper
import nl.eschouten.scraper.texts.tr

object Actions {
  /** The state pill and pull-to-refresh: read the robot once. */
  suspend fun refresh(c: RobotController): String? = c.refresh().let { if (it.ok) null else it.message }

  /** After [planningConfirm] said yes. */
  suspend fun togglePlanning(c: RobotController): String {
    val on = c.state.value.snapshot?.planning ?: false
    return c.setPlanning(!on).message
  }

  suspend fun dismissWidgetTip(c: RobotController) = c.updateSettings { widgetTipSeen = true }

  suspend fun setLanguage(c: RobotController, code: String) {
    if (code != c.state.value.language) c.updateSettings { language = code }
  }

  /** Back to the welcome screen (the robots stay). */
  suspend fun resetSetup(c: RobotController) = c.updateSettings { onboarded = false }

  /** "Test connection" in a robot's settings: makes it the active robot first. */
  suspend fun testConnection(c: RobotController, uid: String): String {
    c.selectRobot(uid)
    val o = c.refresh()
    return if (o.ok) tr.connectionWorks else o.message
  }

  /** Read what routes A..D consist of on the robot; the answer for a toast. */
  suspend fun readRoutes(c: RobotController, uid: String): String {
    c.selectRobot(uid)
    return c.readRoutes().message
  }

  suspend fun selectRobot(c: RobotController, uid: String) = c.selectRobot(uid)

  suspend fun removeRobot(c: RobotController, uid: String) = c.removeRobot(uid)

  /** Changes robot [uid]'s profile (nothing when it was removed meanwhile). */
  private suspend fun update(c: RobotController, uid: String, change: (RobotProfile) -> RobotProfile) {
    c.state.value.robots.firstOrNull { it.uid == uid }?.let { c.updateRobot(change(it)) }
  }

  suspend fun rename(c: RobotController, uid: String, name: String) = update(c, uid) { it.copy(label = name.trim()) }

  suspend fun setPin(c: RobotController, uid: String, pin: String) = update(c, uid) { it.copy(pin = pin.trim()) }

  /** A robot through another device's web API ("" = Bluetooth from here). */
  suspend fun setVia(c: RobotController, uid: String, via: String) = update(c, uid) { it.copy(via = via.trim()) }

  /** A link from a QR code on the buttons or a computer with the web API (docs/API.md):
   *  scraper-control://add?via=<address>&pin=<PIN>. Adds that robot (or updates the one already
   *  through that address), makes it the robot in use and ends the first setup. False when it
   *  is not such a link. */
  suspend fun openLink(c: RobotController, link: String): Boolean {
    val q = addLink(link) ?: return false
    val via = q["via"]?.takeIf { it.isNotBlank() } ?: return false
    val pin = q["pin"] ?: ""
    val s = c.state.value
    val same = s.robots.firstOrNull { it.via.equals(via, ignoreCase = true) }
    val fresh = s.robots.singleOrNull()?.takeIf { !s.onboarded && it.robotId == null && it.via.isEmpty() }   // the empty first robot
    when {
      same != null -> {
        c.updateRobot(same.copy(pin = pin))
        c.selectRobot(same.uid)
      }
      fresh != null -> c.updateRobot(fresh.copy(via = via, pin = pin))
      else -> c.addRobot(RobotProfile(via = via, pin = pin))
    }
    c.updateSettings { onboarded = true }
    return true
  }

  /** The query of a scraper-control://add?... link, or null. */
  fun addLink(link: String): Map<String, String>? {
    val prefix = "scraper-control://add?"
    if (!link.startsWith(prefix, ignoreCase = true)) return null
    return link.substring(prefix.length).split('&').mapNotNull { p ->
      p.split('=', limit = 2).takeIf { it.size == 2 }?.let { it[0] to it[1] }
    }.toMap()
  }

  /** Desktop: serve the web API or not, and on which port (1024..65535; else unchanged). */
  suspend fun setApi(c: RobotController, on: Boolean) = c.updateSettings { apiOn = on }

  suspend fun setApiPort(c: RobotController, port: String): String? {
    val p = port.trim().toIntOrNull()?.takeIf { it in 1024..65535 } ?: return tr.apiPortHelp
    c.updateSettings { apiPort = p }
    return null
  }

  suspend fun renameRoute(c: RobotController, uid: String, route: String, name: String) = update(c, uid) { it.withRouteName(route, name) }

  suspend fun showRoute(c: RobotController, uid: String, route: String, shown: Boolean) = update(c, uid) { it.withRoute(route, shown) }

  /** The device picker in a robot's settings. */
  suspend fun chooseDevice(c: RobotController, uid: String, f: FoundScraper) = update(c, uid) { it.withDevice(f) }

  fun clearLogbook(c: RobotController) = c.clearHistory()
}
