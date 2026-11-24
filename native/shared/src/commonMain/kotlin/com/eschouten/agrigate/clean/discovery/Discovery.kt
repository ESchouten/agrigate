// Lely Discovery: the first implementation of the generic model of a manure robot. All robot logic
// (screens, route steps, safety rules) is in the shared C++ core; this file only translates
// between that core (Lely, CoreSession) and model/Clean.kt.
package com.eschouten.agrigate.clean.discovery

import kotlinx.coroutines.Deferred
import com.eschouten.agrigate.clean.core.*
import com.eschouten.agrigate.clean.link.*

import kotlinx.coroutines.delay
import com.eschouten.agrigate.clean.app.RobotProfile
import com.eschouten.agrigate.clean.model.Capability
import com.eschouten.agrigate.clean.model.FoundRobot
import com.eschouten.agrigate.clean.model.ModelScreen
import com.eschouten.agrigate.clean.model.RunIntent
import com.eschouten.agrigate.clean.model.RunStep
import com.eschouten.agrigate.clean.model.CleanException
import com.eschouten.agrigate.clean.model.CleanFault
import com.eschouten.agrigate.clean.model.CleanModel
import com.eschouten.agrigate.clean.model.CleanSession
import com.eschouten.agrigate.clean.model.CleanStatus
import com.eschouten.agrigate.clean.texts.tr

/** Replaces Bluetooth with a simulated robot (desktop development, tests): see Simulator. */
class LinkProvider(
  val link: (id: String) -> RobotLink,
  val robots: List<FoundDevice>,
  val scanDelayMs: Long = 2_000,
)

object Discovery : CleanModel() {
  /** Set by the demo, tests or the desktop simulator; null = Bluetooth. */
  var linkProvider: LinkProvider? = devLinkProvider()

  const val PANEL = "panel"

  override val id = "lely.discovery"
  override val brand = "Lely"
  override val name = "Discovery"
  override val capabilities = setOf(Capability.Routes, Capability.Planning, Capability.Battery, Capability.NextRun, Capability.Stop, Capability.RouteContents)
  override val pinHelp get() = tr.btPinHelp
  override val reachHelp get() = tr.btReachHelp
  override val scanHelp get() = tr.btScanHelp

  override suspend fun discover(name: String?): List<FoundRobot> {
    linkProvider?.let { sim ->
      delay(sim.scanDelayMs)
      return sim.robots.map { FoundRobot(it.id, it.name, it.rssi) }
    }
    return scanRobots(name).map { FoundRobot(it.id, it.name, it.rssi, it.kind) }
  }

  override fun openSession(p: RobotProfile, onStep: (RunStep) -> Unit, onChanged: (RobotProfile) -> Unit): DiscoverySession =
    DiscoverySession(p, onStep, onChanged)

  // ---- progress ----
  private fun step(s: RunStep) = CoreStep.entries.firstOrNull { it.name == s.id }

  override fun stepLabel(s: RunStep, routeTitle: String): String = when (step(s)) {
    CoreStep.Linked -> tr.stepLinked
    CoreStep.Granted -> tr.stepGranted
    CoreStep.ScreenReady -> tr.stepScreenReady
    CoreStep.PlanningOff -> tr.stepPlanningOff
    CoreStep.PlanningOn -> tr.stepPlanningOn
    CoreStep.Menu -> tr.stepMenu
    CoreStep.Selected -> tr.stepSelected(routeTitle)
    CoreStep.StartPressed -> tr.stepStartPressed
    CoreStep.Answered -> tr.stepAnswered
    CoreStep.Restored -> tr.stepRestored
    CoreStep.Syncing -> tr.syncing(s.percent ?: 0)
    null -> s.id
  }

  override fun nextStep(done: List<RunStep>, intent: RunIntent): String {
    val route = intent.route != null
    val dry = intent.dryRun
    return when (if (done.isEmpty()) null else step(done.last())) {
      null -> tr.connecting
      CoreStep.Linked -> tr.nextCheckPin
      CoreStep.Granted -> tr.nextWaitPanel
      CoreStep.Syncing -> tr.syncing(done.last().percent ?: 0)
      CoreStep.ScreenReady -> when {
        !route -> tr.nextReadStatus
        intent.planningOn == true -> tr.nextPlanningOff
        else -> tr.nextOpenMenu
      }
      CoreStep.PlanningOff -> tr.nextOpenMenu
      CoreStep.PlanningOn -> if (dry) tr.nextRestore else tr.nextOpenMenu
      CoreStep.Menu -> tr.nextChooseRoute
      CoreStep.Selected -> if (dry) tr.nextRestore else tr.nextPressStart
      CoreStep.StartPressed -> tr.nextAnswer
      CoreStep.Answered, CoreStep.Restored -> tr.nextFinish
    }
  }

  override fun expectedSteps(intent: RunIntent): Int {
    if (intent.route == null) return 3
    val on = intent.planningOn ?: true
    return if (intent.dryRun) (if (on) 8 else 6) else (if (on) 8 else 7)
  }

  override val screens get() = listOf(ModelScreen(PANEL, tr.panelTitle, tr.panelSubtitle))

  override val demoRobots get() = Demo.robots.map { FoundRobot(it.id, it.name, it.rssi) }
  override fun installDemo(words: Map<String, String>) = Demo.install(words)
  override fun uninstallDemo() = Demo.uninstall()
}

private const val MEMORY = "memory"
private val MEMORY_BEFORE = setOf("imageVersion", "owedPlanning")   // what versions up to 1.6.223 kept instead

/** One connection to one Discovery, through the C++ core. */
class DiscoverySession(
  private var profile: RobotProfile,
  private val onStep: (RunStep) -> Unit,
  private val onChanged: (RobotProfile) -> Unit,
) : CleanSession {
  private var robot: Robot? = null

  override val isConnected get() = robot?.isConnected ?: false

  override suspend fun awaitClosed() {
    robot?.link?.awaitClosed()
  }

  override suspend fun connect() {
    val sim = Discovery.linkProvider
    if (sim == null) {
      bluetoothProblem()?.let { why ->
        throw CleanException(
          CleanFault.BluetoothOff,
          when (why) {
            BluetoothProblem.Off -> tr.btOff
            BluetoothProblem.Denied -> tr.btDenied
            BluetoothProblem.Unsupported -> tr.btUnsupported
          },
        )
      }
    }
    val id = profile.robotId ?: throw CleanException(CleanFault.NotFound, tr.noRobotNearby)
    val link = sim?.link?.invoke(id) ?: openLink(profile.link, id, profile.robotName.ifEmpty { null })
    val r = Robot(
      link, pin = profile.pin, memory = memory(), language = tr.code, onStep = { onStep(RunStep(it.name, if (it == CoreStep.Syncing) robot?.status()?.syncPercent ?: 0 else null)) },
      onMemory = ::keep,
    )
    robot = r
    guard { r.connect() }
  }

  override suspend fun disconnect() {
    runCatching { robot?.disconnect() }
  }

  override fun status(): CleanStatus {
    val s = robot?.status() ?: return CleanStatus()
    return CleanStatus(
      planning = s.planning, nextRun = s.nextRide, batteryMv = s.batteryMv, activity = s.action,
      route = s.route, busy = s.busy, driving = s.driving, finishQuestion = s.stopQuestion,
      screen = s.screen, highlighted = s.highlighted, extra = Manual.write(s.manual, s.charger),
      cancellable = s.cancellable,
    )
  }

  override fun cancel() { robot?.cancel() }

  override val revision: Any get() = robot?.status()?.let { "${it.screen}\u0000${it.highlighted}" } ?: ""

  override suspend fun startRoute(slot: String, dryRun: Boolean) = guard { robot!!.startRoute(slot.first(), dryRun) }
  override suspend fun setPlanning(on: Boolean) = guard { robot!!.setPlanning(on) }
  override suspend fun stopRoute() = guard { robot!!.stopRoute() }
  override suspend fun answerStop(finish: Boolean) = guard { robot!!.answerStop(finish) }

  override suspend fun readRoutes(): Map<String, String> {
    var out = emptyMap<String, String>()
    guard { out = robot!!.readRoutes() }
    return out
  }

  /** A panel key by its RobotKey name, held down until [until] completes (manual control:
   *  drives while Start/Stop is held), or a tap. */
  override suspend fun input(name: String, until: Deferred<*>?) {
    val key = RobotKey.entries.firstOrNull { it.name == name } ?: throw CleanException(CleanFault.Failed, tr.notSupported)
    guard { if (until == null) robot!!.press(key) else robot!!.hold(key, until) }
  }

  /** What the driver keeps with this robot (the core's clean_memory). */
  private fun memory(): String =
    profile.extra[MEMORY] ?: profile.extra["imageVersion"]?.let { """{"image":"$it"}""" } ?: ""

  private fun keep(memory: String) {
    profile = profile.copy(extra = profile.extra - MEMORY_BEFORE + (MEMORY to memory))
    onChanged(profile)
  }

  /** Core errors -> generic faults. */
  private suspend fun guard(f: suspend () -> Unit) {
    try {
      f()
    } catch (e: CleanException) {
      throw e
    } catch (e: PinRejected) {
      throw CleanException(CleanFault.AuthRejected, tr.pinRefused)
    } catch (e: RobotBusy) {
      throw CleanException(CleanFault.Busy, tr.robotBusyNothingPressed)
    } catch (e: RobotException) {
      throw when (e.result) {
        CoreResult.StartedNoAnswer -> CleanException(CleanFault.NoConfirmation, tr.noQuestionAfterStart)
        CoreResult.Cancelled -> CleanException(CleanFault.Cancelled, tr.cancelledBody)
        CoreResult.ConnectFailed -> CleanException(CleanFault.NotFound, firstLine(e.message ?: ""))
        else -> CleanException(CleanFault.Failed, firstLine(e.message ?: ""))
      }
    } catch (e: kotlinx.coroutines.CancellationException) {
      throw e
    } catch (e: Exception) {
      throw CleanException(CleanFault.NotFound, tr.noConnection(e.message))
    }
  }

  private fun firstLine(s: String): String {
    val t = s.lineSequence().first().trim()
    return if (t.isEmpty()) tr.unknownError else t.replaceFirstChar { it.uppercase() }
  }
}
