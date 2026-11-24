"""agrigate: the robots in the barn. Here Agrigate Clean, a manure robot over Bluetooth; milking
robots and the herd are in agrigate.farm, the cow at each robot in agrigate.live.

The robot logic lives in the robot driver (drivers/, C++); this package finds the robot,
opens the link and exposes the same API as the M5Stack buttons and the apps.
"""
from .api import KEYS, PinRejected, Robot, RobotBusy, RobotError, Status
from .discover import discover as find_robots   # keep agrigate.discover = the module

__all__ = ["Robot", "RobotError", "RobotBusy", "PinRejected", "Status", "KEYS", "find_robots"]
