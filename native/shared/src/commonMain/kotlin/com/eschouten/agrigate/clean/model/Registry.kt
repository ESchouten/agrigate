// Every robot type the apps support. To add one: implement CleanModel and CleanSession
// (see Clean.kt) and list it here. The first one is the default.
package com.eschouten.agrigate.clean.model

import com.eschouten.agrigate.clean.discovery.Discovery

val cleanModels: List<CleanModel> = listOf(Discovery)

val defaultModelId: String get() = cleanModels.first().id

fun modelFor(id: String): CleanModel = cleanModels.firstOrNull { it.id == id } ?: cleanModels.first()
