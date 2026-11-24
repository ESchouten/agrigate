// The Android app: an Activity around the Compose UI (:composeApp), the core's JNI library
// built by the NDK from the robot driver, and the home-screen widgets.
plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.plugin.compose")
}

android {
  namespace = "com.eschouten.agrigate.clean"
  compileSdk { version = release(rootProject.extra["androidCompileSdk"] as Int) { minorApiLevel = 0 } }
  ndkVersion = "27.3.13750724" // the one on GitHub's Ubuntu runners: no NDK download in ci
  defaultConfig {
    applicationId = "com.eschouten.agrigate.clean"
    minSdk = rootProject.extra["androidMinSdk"] as Int
    targetSdk = 36
    versionCode = rootProject.extra["appBuild"] as Int
    versionName = rootProject.extra["appVersion"] as String   // 1.6.120 (build.gradle.kts)
    ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
  }
  externalNativeBuild {
    cmake { path = file("../../core/CMakeLists.txt") }
  }
  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  buildFeatures { compose = true }
  // Release builds (the release workflow): signed with the project's own key, so every
  // version installs over the previous one (Obtainium, a downloaded APK). The keystore comes
  // from the environment: AGRIGATE_KEYSTORE (path) and AGRIGATE_KEYSTORE_PASSWORD (the store's and
  // the key's). Without them a release build is unsigned.
  val keystore = System.getenv("AGRIGATE_KEYSTORE")?.let { file(it) }?.takeIf { it.isFile }
  if (keystore != null) {
    signingConfigs {
      create("release") {
        storeFile = keystore
        storePassword = System.getenv("AGRIGATE_KEYSTORE_PASSWORD")
        keyAlias = "scraper-control"
        keyPassword = System.getenv("AGRIGATE_KEYSTORE_PASSWORD")
      }
    }
  }
  buildTypes {
    release {
      isMinifyEnabled = false
      if (keystore != null) signingConfig = signingConfigs.getByName("release")
    }
  }
}

dependencies {
  implementation(project(":composeApp"))
  implementation("androidx.activity:activity-compose:1.10.1")
  implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.11.0")
}
