package com.eschouten.agrigate.clean.lely

import com.eschouten.agrigate.clean.core.*
import com.eschouten.agrigate.clean.link.*

/** Desktop development: a simulated robot instead of Bluetooth (Simulator.kt); null elsewhere. */
internal expect fun devLinkProvider(): LinkProvider?
