// The Compose UI, one code base for Android, the desktop and the web page (Compose Multiplatform).
// iOS has its own SwiftUI app (iosApp/) on top of the same :shared module. The web page is
// served by the desktop app (api/ApiServer), so it is built into the desktop app's resources.
plugins {
  id("org.jetbrains.kotlin.multiplatform")
  id("org.jetbrains.kotlin.plugin.compose")
  id("org.jetbrains.compose")
}


kotlin {
  jvm {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
  }
  @OptIn(org.jetbrains.kotlin.gradle.ExperimentalWasmDsl::class)
  wasmJs {
    browser()
    binaries.executable()
  }
  sourceSets {
    wasmJsMain { resources.srcDir("src/jvmMain/resources") }   // the icon
    commonMain.dependencies {
      api(project(":shared"))
      implementation(compose.runtime)
      implementation(compose.foundation)
      implementation(compose.material3)
      implementation(compose.ui)
      implementation("org.jetbrains.compose.material:material-icons-extended:1.7.3")
    }
    jvmTest.dependencies {
      implementation(kotlin("test"))
      @OptIn(org.jetbrains.compose.ExperimentalComposeLibrary::class)
      implementation(compose.uiTest)
    }
    jvmMain.dependencies {
      implementation(compose.desktop.currentOs)
      implementation("org.jetbrains.kotlinx:kotlinx-coroutines-swing:1.11.0")
    }
    wasmJsMain.dependencies {
      implementation("org.jetbrains.kotlinx:kotlinx-browser:0.5.0")
    }
  }
}

compose.desktop {
  application {
    mainClass = "com.eschouten.agrigate.clean.app.MainKt"
    nativeDistributions {
      packageName = "Agrigate Clean"   // Linux: the package agrigate-clean
      // beyond what is found by itself: the web API (api/ApiServer), updates (app/DesktopUpdates)
      modules("jdk.httpserver", "java.net.http", "jdk.crypto.ec")
      // the app icon (from the iOS/Android icon), per system
      macOS {
        iconFile.set(project.file("icons/icon.icns"))
        bundleID = "com.eschouten.agrigate.clean.desktop"
        // macOS stops an app that uses Bluetooth without saying why (BLE and classic alike)
        infoPlist {
          extraKeysRawXml = """
            <key>NSBluetoothAlwaysUsageDescription</key>
            <string>To control the robot over Bluetooth.</string>
          """.trimIndent()
        }
      }
      linux { iconFile.set(project.file("icons/icon.png")) }
      windows { iconFile.set(project.file("icons/icon.ico")) }
      // Windows and macOS install with Velopack (task velopack below), Linux with this .deb
      targetFormats(org.jetbrains.compose.desktop.application.dsl.TargetFormat.Deb)
      packageVersion = rootProject.extra["appVersion"] as String   // 1.6.120 (build.gradle.kts)
    }
  }
}

// The web page goes into the desktop app's resources (common/web), where ApiServer finds it.
val webPage by tasks.registering(Sync::class) {
  from(tasks.named("wasmJsBrowserDistribution"))
  into(layout.buildDirectory.dir("app-resources/common/web"))
}
compose.desktop.application.nativeDistributions.appResourcesRootDir.set(layout.buildDirectory.dir("app-resources"))
tasks.matching { it.name == "prepareAppResources" }.configureEach { dependsOn(webPage) }

// The installer that keeps the app up to date (Velopack; needs the .NET SDK for its vpk tool),
// on Windows and macOS, in build/velopack: the installer (.exe / .pkg), the full package and its
// feed releases.<channel>.json, which a release signs (firmware/sign_release.py --feed) for
// app/DesktopUpdates. Linux: packageDeb, updated through apt.
val hostOs = System.getProperty("os.name").lowercase()
val velopackOs = hostOs.startsWith("mac") || hostOs.startsWith("windows")
val vpkTool by tasks.registering(Exec::class) {   // the version core/CMakeLists.txt links
  onlyIf { velopackOs }
  commandLine("dotnet", "tool", "update", "-g", "vpk", "--version", "1.2.161", "--allow-downgrade")
}
val velopack by tasks.registering(Exec::class) {
  dependsOn("createDistributable", vpkTool)
  onlyIf { velopackOs }
  val app = layout.buildDirectory.dir("compose/binaries/main/app").get().asFile
  val out = layout.buildDirectory.dir("velopack").get().asFile
  val (dir, exe) = if (hostOs.startsWith("mac")) File(app, "Agrigate Clean.app") to "Agrigate Clean"
    else File(app, "Agrigate Clean") to "Agrigate Clean.exe"
  val tools = File(System.getProperty("user.home"), ".dotnet/tools")   // where vpkTool puts it
  doFirst {
    out.deleteRecursively()
    listOf("vpk", "vpk.exe").map { File(tools, it) }.firstOrNull { it.exists() }?.let { executable = it.path }
  }
  commandLine(
    "vpk", "pack", "--packId", "AgrigateClean", "--packTitle", "Agrigate Clean",
    "--packVersion", rootProject.extra["appVersion"] as String, "--packDir", dir.path, "--mainExe", exe,
    "--delta", "None", "--outputDir", out.path, "--yes",
  )
  if (hostOs.startsWith("windows")) args("--icon", project.file("icons/icon.ico").path)
}

if (findProject(":androidApp") != null) apply(from = "android.gradle")   // an Android SDK is there
