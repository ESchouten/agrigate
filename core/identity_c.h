/* Identity: which animal is where. A source identifies an animal at a place (a milking robot, a
 * feeding station, a gate: anything that reads the animal's responder) and that is all a consumer
 * needs to know; which device it is and how it is reached is the driver's business. On a session
 * of session_c.h, the same for every driver that serves "identity". It only reads.
 *
 * identity_read brings the session up to date (a source that tells by itself has nothing to be
 * asked: the read is done at once). Then:
 *
 * identity_json: who is at each place now
 *   {"updated": when the source last said anything (null: not yet),
 *    "places": [{
 *      "id": the source's own id of the place, "name" ("" if the source has none),
 *      "kind": "milkingRobot" | "feedingStation" | "gate" | "unknown",
 *      "animal": {"number": the farm's animal number, "name", "responder", "since"} or null,
 *      "milking": true while the animal there is being milked (a milking robot that says so), else false,
 *      "milkingSince": when that milking started, null when not milking
 *    }]}
 *   "animal" is the last one identified there: it stays until the next one, as a source does not
 *   always say when an animal leaves (an animal a robot refuses walks on without a word). When a
 *   milking starts, "animal" is the one identified closest in time to the start (within a minute): a
 *   robot that also identifies animals walking past (into a next robot) does not lose the one in its
 *   box. The "milkingStarted" event comes some seconds after the start (its "time" is the start), to
 *   include an identification that arrives with it. A "visitEnded" event carries the animal of that milking.
 *
 * identity_events_json: what happened, for a consumer that follows
 *   {"events": [{"seq", "time", "kind": "identified" | "milkingStarted" | "visitEnded", "place": the place's id,
 *                "animal": {"number", "name", "responder"} (for a milking: the animal milked, see above;
 *                           all empty when no animal was identified there)}],
 *    "last": seq}
 *   with seq > after, oldest first; the session keeps the last 200. The same animal at the same
 *   place again within 5 s is the same identification. "visitEnded": the robot is done with the
 *   animal and lets it out (it may still be in the box for some seconds). */
#ifndef IDENTITY_C_H
#define IDENTITY_C_H

#include "session_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start a read. Returns SESSION_OK (started: now the link's exchange), SESSION_UNSUPPORTED or
 * SESSION_IN_PROGRESS. */
SESSION_API int32_t identity_read(session* s);
SESSION_API int32_t identity_json(session* s, char* buf, int32_t size);
SESSION_API int32_t identity_events_json(session* s, int64_t after, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
