// What the screens use to work with the robots: the state to draw and the commands. Two kinds:
// RobotController, which talks to the robot itself (the apps), and RemoteController, which
// asks the barn computer's RobotController (the web page, Remote.kt). Screens cannot tell them apart.
package nl.eschouten.scraper.app

import kotlinx.coroutines.Deferred
import kotlinx.coroutines.flow.StateFlow
import kotlinx.serialization.Serializable
import nl.eschouten.scraper.model.FoundScraper
import nl.eschouten.scraper.model.ScraperModel

/** The app-wide choices (not per robot). */
@Serializable
data class AppFlags(val onboarded: Boolean, val widgetTipSeen: Boolean, val language: String)

interface Controller {
  val state: StateFlow<ControllerState>

  /** [route] "A".."D". A route button pressed while a route runs stops it (not a dry run). */
  suspend fun startRoute(route: String, dryRun: Boolean = false): Outcome
  suspend fun setPlanning(on: Boolean): Outcome

  /** Connect, read the home screen, disconnect. */
  suspend fun refresh(): Outcome

  /** Read what each route consists of on the robot and remember it with the robot. */
  suspend fun readRoutes(): Outcome

  /** Keep the connection open (a type's own screen) until [release]. */
  suspend fun hold(): Outcome
  suspend fun release()

  /** An input on a type's own screen while held (e.g. a panel key), held until [until] completes or a tap. */
  suspend fun input(name: String, until: Deferred<*>? = null): Outcome

  /** The user's answer to [ControllerState.finishQuestion]: true = finish, false = don't,
   *  null = later (leave the question on the robot). */
  fun answerFinish(finish: Boolean?)

  /** Robots of [model] nearby, strongest first. */
  suspend fun discover(model: ScraperModel, name: String?): List<FoundScraper>

  /** Switch to another robot (closes the connection to the current one). */
  suspend fun selectRobot(uid: String)
  suspend fun addRobot(p: RobotProfile)
  suspend fun removeRobot(uid: String)

  /** Change a robot's profile (names, routes, PIN, ...). */
  suspend fun updateRobot(p: RobotProfile)

  /** Change the app-wide choices. */
  suspend fun updateSettings(change: (AppFlags) -> AppFlags)

  fun clearHistory()
}

val ControllerState.flags: AppFlags get() = AppFlags(onboarded, widgetTipSeen, language)
