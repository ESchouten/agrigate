/* Milk: the milking robots and how they are doing, the same for every driver that serves
 * "milk", on a session of session_c.h. It only reads: nothing here changes anything on the farm.
 *
 * milk_read brings the session up to date; when the read is done (session_c.h), milk_json gives
 * what the source knows:
 *   {"updated": when that was read (null: not yet, or the session was given no time),
 *    "robots": [{
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

#include "session_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start a read. Returns SESSION_OK (started: now the link's exchange), SESSION_UNSUPPORTED or
 * SESSION_IN_PROGRESS. */
SESSION_API int32_t milk_read(session* s);
SESSION_API int32_t milk_json(session* s, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
