// The Android process: Bluetooth needs the app context, and the app and its widgets share one
// controller per process (the store is the same SharedPreferences).
package com.eschouten.agrigate.clean

import android.app.Application
import com.eschouten.agrigate.clean.app.AppStart
import com.eschouten.agrigate.clean.app.PrefsStore
import com.eschouten.agrigate.clean.app.RobotController
import com.eschouten.agrigate.clean.app.WidgetBridge
import com.eschouten.agrigate.clean.app.AndroidPlatform

class CleanApp : Application() {
  override fun onCreate() {
    super.onCreate()
    AndroidPlatform.init(this)
  }

  private var controller: RobotController? = null

  /** [demo]: words for the demo robot (UI tests, "adb shell am start ... --es demo ..."). */
  fun controller(demo: String? = null): RobotController =
    controller ?: run {
      val store = PrefsStore(this)
      AppStart.controller(store, demo).also {
        controller = it
        // demo runs keep their own store; widgets show the real robots
        if (demo == null) WidgetBridge.install(it, store) { WidgetViews.refreshAll(this) }
      }
    }

  /** A widget started a route on its own connection: the app reads the logbook again. */
  fun widgetRan() {
    controller?.reload()
  }
}
