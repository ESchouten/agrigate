package com.eschouten.agrigate.clean.core

/** Android: libclean_jni.so is built by the app's NDK build (androidApp, core/CMakeLists.txt). */
internal fun loadCoreLibrary() = System.loadLibrary("clean_jni")
