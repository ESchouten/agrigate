// What the screens use to work with the robots: the state to draw and the commands. Two kinds:
// RobotController, which talks to the robot itself (the apps), and the web page's, which calls
// the RobotController of the barn computer that serves it (kotlinx.rpc makes both ends from
// [RobotApi]; WebServer.kt, WebLink.kt). Screens cannot tell them apart.
package nl.eschouten.scraper.app

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn
import kotlinx.rpc.annotations.Rpc
import kotlinx.serialization.Serializable
import nl.eschouten.scraper.model.FoundScraper
import nl.eschouten.scraper.texts.useLanguage

/** The app-wide choices (not per robot). */
@Serializable
data class AppFlags(val onboarded: Boolean, val widgetTipSeen: Boolean, val language: String)

@Rpc
interface RobotApi {
  /** The state now and after every change. */
  fun states(): Flow<ControllerState>

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

  /** An input on a type's own screen while held (e.g. a panel key): a tap, or held down for as
   *  long as [until] runs (it ends when the finger leaves the key, or with the caller). */
  suspend fun input(name: String, until: Flow<Unit>? = null): Outcome

  /** The user's answer to [ControllerState.finishQuestion]: true = finish, false = don't,
   *  null = later (leave the question on the robot). */
  suspend fun answerFinish(finish: Boolean?)

  /** Robots of type [model] (ScraperModel.id) nearby, strongest first. */
  suspend fun discover(model: String, name: String?): List<FoundScraper>

  /** Switch to another robot (closes the connection to the current one). */
  suspend fun selectRobot(uid: String)
  suspend fun addRobot(p: RobotProfile)
  suspend fun removeRobot(uid: String)

  /** Change a robot's profile (names, routes, PIN, ...). */
  suspend fun updateRobot(p: RobotProfile)
  suspend fun updateSettings(flags: AppFlags)
  suspend fun clearHistory()
}

interface Controller : RobotApi {
  val state: StateFlow<ControllerState>
  override fun states(): Flow<ControllerState> = state
}

val ControllerState.flags: AppFlags get() = AppFlags(onboarded, widgetTipSeen, language)

/** The web page's controller: every call goes to [api] on the barn computer. Returns once its
 *  state is here; [scope] ends when the connection does. */
suspend fun remoteController(api: RobotApi, scope: CoroutineScope): Controller {
  val state = api.states().onEach { useLanguage(it.language) }.stateIn(scope)
  return object : Controller, RobotApi by api {
    override val state = state
    override fun states() = state
  }
}
