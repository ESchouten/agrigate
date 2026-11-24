/* The herd as the farm's management server knows it, in C, the same for every driver that knows
 * a herd, on a session of robot_c.h. It only reads: nothing here changes anything on the farm.
 *
 * cow_read asks the server for its animals; when the requests are done (robot_c.h), cow_json
 * gives what it read. Dates are "YYYY-MM-DD" (the farm's own day), "" when not known; numbers
 * are null when not known:
 *   {"animals": [{
 *     "id": the server's own id of the animal,
 *     "number": the farm's animal number (cow number on the robot and in the barn),
 *     "name", "lifeNumber" (official ear tag, e.g. "NL 123456789"), "workNumber" ("" if the
 *              server has none), "responder" (the collar/ear tag the robots read, "" if none),
 *     "birthDate", "group" ("" if none),
 *     "production": "milking" | "dry" | "young" | "unknown",
 *     "reproduction": "open" | "inseminated" | "pregnant" | "doNotBreed" | "unknown",
 *     "reproductionRaw": the status as the server gives it (for a word this API has none for),
 *     "lactation": lactation number, "daysInMilk", "calvingDate" (last calving),
 *     "pregnantDays", "expectedCalving",
 *     "inseminations": [{"date", "sireName", "sireCode" (AI code), "sireLifeNumber", "type": "ai" |
 *              "diy" (by the farmer) | "bull" | "embryo" | "unknown"}]   oldest first,
 *     "pregnancyChecks": [{"date", "result": "pregnant" | "notPregnant" | "unknown"}]   oldest first
 *   }]}
 * How far back inseminations and pregnancy checks go is up to the server (see the driver's API.md). */
#ifndef COW_C_H
#define COW_C_H

#include "robot_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start a read. Returns ROBOT_OK (started: now the requests), ROBOT_UNSUPPORTED or
 * ROBOT_IN_PROGRESS. */
ROBOT_API int32_t cow_read(robot_session* s);
ROBOT_API int32_t cow_json(robot_session* s, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
