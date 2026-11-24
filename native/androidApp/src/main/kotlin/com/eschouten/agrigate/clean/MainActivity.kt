package com.eschouten.agrigate.clean

import android.Manifest
import android.os.Build
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import com.eschouten.agrigate.clean.app.App
import com.eschouten.agrigate.clean.app.AppHost
import com.eschouten.agrigate.clean.app.Nav
import com.eschouten.agrigate.clean.ui.Actions
import com.eschouten.agrigate.clean.ui.Platform

class MainActivity : ComponentActivity(), AppHost {
  override val platform = Platform.Android
  private val front = MutableStateFlow(false)
  override val foreground: StateFlow<Boolean> get() = front
  private val nav = Nav()
  private var asked: CompletableDeferred<Unit>? = null
  private val permissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { asked?.complete(Unit) }

  override suspend fun requestBluetooth() {
    val want = if (Build.VERSION.SDK_INT >= 31) {
      arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else {
      arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }
    val d = CompletableDeferred<Unit>()
    asked = d
    permissions.launch(want)
    d.await()
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    enableEdgeToEdge()
    super.onCreate(savedInstanceState)
    val controller = (application as ScraperApp).controller(intent.getStringExtra("demo"))
    onBackPressedDispatcher.addCallback(
      this,
      object : OnBackPressedCallback(true) {
        override fun handleOnBackPressed() {
          if (!nav.back()) moveTaskToBack(true)
        }
      },
    )
    setContent { App(controller, this, nav) }
    open(intent)
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    open(intent)
  }

  /** A agrigate-clean://add link (a QR code scanned by the camera): add that robot. */
  private fun open(intent: Intent?) {
    val link = intent?.data?.toString() ?: return
    val controller = (application as ScraperApp).controller(null)
    lifecycleScope.launch { if (Actions.openLink(controller, link)) nav.home() }
  }

  override fun onStart() {
    super.onStart()
    front.value = true
  }

  override fun onStop() {
    front.value = false
    super.onStop()
  }
}
