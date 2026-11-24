/* The milking robot API in C, the same for every driver that knows milking robots, on a
 * session of robot_c.h. It only reads: nothing here changes anything on the farm.
 *
 * milk_read asks the server for its milking robots; when the requests are done
 * (robot_c.h), milk_json gives what it read:
 *   {"robots": [{
 *     "id", "name", "group" (barn or group, "" if none),
 *     "state": "idle" | "milking" | "cleaning" | "alarm" | "emergency" | "assistance" (waits
 *              for a person) | "paused" | "offline" | "unknown",
 *     "raw": the state as the server gives it (for a state this API has no word for),
 *     "since": when that state began, "lastVisit": the last cow's visit (the server's own
 *              time text, "" if not known),
 *     "alarms": [{"time", "severity": "info" | "warning" | "error" | "unknown", "code", "text"}],
 *     "milkKg", "milkings", "failedMilkings": the robot's figures as the farm software shows
 *              them (null if not known)
 *   }]} */
#ifndef MILK_C_H
#define MILK_C_H

#include "robot_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start a read. Returns ROBOT_OK (started: now the requests), ROBOT_UNSUPPORTED or
 * ROBOT_IN_PROGRESS. */
ROBOT_API int32_t milk_read(robot_session* s);
ROBOT_API int32_t milk_json(robot_session* s, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
