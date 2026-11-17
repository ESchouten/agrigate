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
    mainClass = "nl.eschouten.scraper.app.MainKt"
    nativeDistributions {
      packageName = "Scraper Control"
      // beyond what is found by itself: the web API (api/ApiServer), updates (app/DesktopUpdates)
      modules("jdk.httpserver", "java.net.http", "jdk.crypto.ec")
      // the app icon (from the iOS/Android icon), per system
      macOS {
        iconFile.set(project.file("icons/icon.icns"))
        bundleID = "nl.eschouten.scraper.desktop"
        // macOS stops an app that uses Bluetooth without saying why (BLE and classic alike)
        infoPlist {
          extraKeysRawXml = """
            <key>NSBluetoothAlwaysUsageDescription</key>
            <string>To control the robot over Bluetooth.</string>
          """.trimIndent()
        }
      }
      linux { iconFile.set(project.file("icons/icon.png")) }
      windows {
        iconFile.set(project.file("icons/icon.ico"))
        menu = true            // in the Start menu
        perUserInstall = true  // no administrator needed
        upgradeUuid = "1fe694cb-fc77-4315-bb64-e8666b0218d5"   // never changes: a newer installer replaces the older app
      }
      targetFormats(
        org.jetbrains.compose.desktop.application.dsl.TargetFormat.Dmg,
        org.jetbrains.compose.desktop.application.dsl.TargetFormat.Deb,
        org.jetbrains.compose.desktop.application.dsl.TargetFormat.Exe,
      )
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

if (findProject(":androidApp") != null) apply(from = "android.gradle")   // an Android SDK is there
