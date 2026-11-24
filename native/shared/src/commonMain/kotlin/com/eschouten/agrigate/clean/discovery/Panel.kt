// The robot's panel, the Lely Discovery's own screen: connected only while the screen is open
// and the app is in front.
// The flow (connect, keys, drop on leaving, reconnect on coming back) lives here, so both apps
// behave the same; they draw [PanelScreen].
package com.eschouten.agrigate.clean.discovery

import com.eschouten.agrigate.clean.core.*
import com.eschouten.agrigate.clean.link.*

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import com.eschouten.agrigate.clean.app.Cancellable
import com.eschouten.agrigate.clean.app.ControllerState
import com.eschouten.agrigate.clean.app.Outcome
import com.eschouten.agrigate.clean.app.RobotController
import com.eschouten.agrigate.clean.app.immediateOrDefault
import com.eschouten.agrigate.clean.texts.tr
import com.eschouten.agrigate.clean.ui.Action
import com.eschouten.agrigate.clean.ui.ActionKind
import com.eschouten.agrigate.clean.ui.Glyph
import com.eschouten.agrigate.clean.ui.newScope
import com.eschouten.agrigate.clean.ui.watchIn

data class PanelState(val held: Boolean = false, val error: String? = null, val resume: Boolean = false)

class PanelFlow(private val controller: RobotController) {
  private val scope = newScope()

  private val _state = MutableStateFlow(PanelState())
  val state: StateFlow<PanelState> = _state

  fun watch(onChange: (PanelState) -> Unit): Cancellable = _state.watchIn(scope, onChange)

  /** The connect button. */
  suspend fun connect() {
    if (_state.value.held) return
    _state.value = PanelState(held = true)
    val o = controller.hold()
    if (!o.ok) _state.value = PanelState(error = o.message)
  }

  /** The "live · disconnect" chip, and leaving the screen. */
  suspend fun disconnect() {
    keyUp()
    if (!_state.value.held) return
    _state.value = _state.value.copy(held = false)
    controller.release()
  }

  /** The app went to the background: drop the link, connect again when it comes back. */
  suspend fun pause() {
    val held = _state.value.held
    disconnect()
    _state.value = _state.value.copy(resume = held)
  }

  suspend fun resume() {
    if (_state.value.resume) {
      _state.value = _state.value.copy(resume = false)
      connect()
    }
  }

  /** A tap on a key; the message for a toast when it failed, else null. */
  suspend fun press(key: RobotKey): String? {
    val o: Outcome = controller.custom { it.input(key.name) }
    return if (o.ok) null else o.message
  }

  private var up: CompletableDeferred<Unit>? = null
  private var downSent: RobotKey? = null

  /** A finger on a key: held as on the robot's own panel until [keyUp], so in manual control the
   *  robot drives while Start/Stop is held. [onError] gets the message for a toast. */
  fun keyDown(key: RobotKey, onError: (String) -> Unit) {
    keyUp()
    downSent = key
    val u = CompletableDeferred<Unit>().also { up = it }
    scope.launch(Dispatchers.Main.immediateOrDefault()) {
      val o = controller.custom { it.input(key.name, u) }
      if (!o.ok) onError(o.message)
    }
  }

  /** The finger left the key. */
  fun keyUp() {
    up?.complete(Unit)
    up = null
  }

  /** A button's action, after the finger left it (SwiftUI, accessibility): a tap, unless
   *  [keyDown] already sent this press (SwiftUI may skip a press too quick to draw). */
  fun keyTapped(key: RobotKey, onError: (String) -> Unit) {
    if (downSent != key) {
      keyDown(key, onError)
      keyUp()
    }
    downSent = null
  }

  fun view(cs: ControllerState, st: PanelState = _state.value): PanelScreen {
    val live = cs.live
    return panelScreen(cs, st.held, st.error, live?.screen, live?.highlighted, live?.extra?.let(Manual::read), live?.extra?.let(Manual::readCharger))
  }
}

/** The manual control screen's motors and direction in [com.eschouten.agrigate.clean.model.CleanStatus.extra]:
 *  "manual" -> left, right, forward as 1 / 0 / ? (not known), e.g. "10?". The charger approach
 *  screen: "charger" -> side, volt and amp, e.g. "10|12.9|0.0". */
internal object Manual {
  private const val KEY = "manual"
  private const val CHARGER = "charger"

  fun write(m: ManualState?, c: ChargerState?): Map<String, String> = buildMap {
    if (m != null) put(KEY, listOf(m.left, m.right, m.forward).joinToString("") { when (it) { true -> "1"; false -> "0"; null -> "?" } })
    if (c != null) put(CHARGER, "${if (c.left) 1 else 0}${if (c.right) 1 else 0}|${c.volt}|${c.amp}")
  }

  fun readCharger(extra: Map<String, String>): ChargerState? = extra[CHARGER]?.split('|')?.takeIf { it.size == 3 }?.let { (side, volt, amp) ->
    ChargerState(side.getOrNull(0) == '1', side.getOrNull(1) == '1', volt, amp)
  }

  fun read(extra: Map<String, String>): ManualState? = extra[KEY]?.let { v ->
    fun b(i: Int) = when (v.getOrNull(i)) { '1' -> true; '0' -> false; else -> null }
    ManualState(b(0), b(1), b(2))
  }
}

// ---- what the apps draw ----

data class PanelKey(val key: RobotKey, val label: String, val testId: String)

/** In manual control, above each soft key what it sets: the robot shows that as images. */
data class ManualCell(val label: String, val value: String, val on: Boolean, val glyph: Glyph?, val testId: String)

data class PanelScreen(
  val title: String,
  val robotName: String,
  /** The robot's display, line by line, as the core read it. */
  val lines: List<String>,
  /** The selected row (drawn in brand red), "" if none. */
  val highlighted: String,
  val dimmed: Boolean,
  /** Manual control: left motor, direction, right motor; null on other screens. */
  val manual: List<ManualCell>?,
  val keysEnabled: Boolean,
  /** 3 rows of 3, in the robot's own layout. */
  val keys: List<List<PanelKey>>,
  val connected: Boolean,
  val disconnectLabel: String,
  /** The connect button while not connected (null when connected). */
  val connect: Action?,
  val connecting: Boolean,
  val error: String?,
  val info: String,
)

/** [held]: this screen asked for the connection; [error]: why that failed. */
fun panelScreen(s: ControllerState, held: Boolean, error: String?, screen: String?, highlighted: String?, manual: ManualState?,
                charger: ChargerState? = null): PanelScreen {
  val connected = s.connected && held
  val connecting = held && !s.connected
  val text = when {
    connected && screen != null -> screen
    connecting -> "\n   ${tr.connectingDots}"
    else -> "\n   ${tr.notConnected}"
  }
  return PanelScreen(
    title = tr.panel,
    robotName = s.robot.displayName,
    lines = text.split('\n'),
    highlighted = if (connected) (highlighted ?: "").trim() else "",
    dimmed = !connected,
    manual = manual?.takeIf { connected }?.let { m ->
      fun motor(on: Boolean?) = on?.let { if (it) tr.on else tr.off } ?: "?"
      listOf(
        ManualCell(tr.motorLeft, motor(m.left), m.left == true, null, "manual-left"),
        ManualCell(tr.direction, m.forward?.let { if (it) tr.forward else tr.backward } ?: "?", false,
          m.forward?.let { if (it) Glyph.Forward else Glyph.Backward }, "manual-direction"),
        ManualCell(tr.motorRight, motor(m.right), m.right == true, null, "manual-right"),
      )
    } ?: charger?.takeIf { connected }?.let { c ->
      // the charger approach: the charger's side over Soft1 / Soft3, the battery in the middle
      listOf(
        ManualCell(tr.chargerLeft, if (c.left) tr.chargerHere else "–", c.left, null, "charger-left"),
        ManualCell(tr.battery, "${c.volt} V" + if (c.amp.isEmpty()) "" else " · ${c.amp} A", false, null, "charger-battery"),
        ManualCell(tr.chargerRight, if (c.right) tr.chargerHere else "–", c.right, null, "charger-right"),
      )
    },
    keysEnabled = connected,
    keys = listOf(
      listOf(PanelKey(RobotKey.Soft1, tr.softLeft, "key-soft1"), PanelKey(RobotKey.Soft2, tr.softMiddle, "key-soft2"), PanelKey(RobotKey.Soft3, tr.softRight, "key-soft3")),
      listOf(PanelKey(RobotKey.Up, tr.keyUp, "key-up"), PanelKey(RobotKey.StartStop, "Start/Stop", "key-startstop"), PanelKey(RobotKey.Back, "Esc", "key-back")),
      listOf(PanelKey(RobotKey.Down, tr.keyDown, "key-down"), PanelKey(RobotKey.Prog, "Prog", "key-prog"), PanelKey(RobotKey.Enter, "Enter", "key-enter")),
    ),
    connected = connected,
    disconnectLabel = tr.liveDisconnect,
    connect = if (connected) null else Action(if (connecting) tr.connectingDots else tr.connectPanel, ActionKind.Confirm, "panel-connect", enabled = !connecting && !s.isBusy),
    connecting = connecting,
    error = if (connected) null else error,
    info = tr.panelInfo,
  )
}
