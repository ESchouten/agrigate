/* Cow: the herd as a source knows it (usually the farm's management server), the same for every
 * driver that serves "cow", on a session of session_c.h. It only reads: nothing here changes
 * anything on the farm. Where an animal is right now is another contract: identity_c.h.
 *
 * cow_read brings the session up to date; when the read is done (session_c.h), cow_json gives
 * what the source knows. Dates are "YYYY-MM-DD" (the farm's own day), "" when not known; numbers
 * are null when not known:
 *   {"updated": when that was read (null: not yet, or the session was given no time),
 *    "animals": [{
 *     "id": the server's own id of the animal,
 *     "number": the farm's animal number (cow number on the robot and in the barn),
 *     "name", "lifeNumber" (official ear tag, e.g. "NL 123456789"), "workNumber" ("" if the
 *              server has none), "responder" (the collar/ear tag the robots read, "" if none),
 *     "birthDate", "group" ("" if none),
 *     "production": "milking" | "dry" | "young" | "unknown",
 *     "reproduction": "open" | "inseminated" | "pregnant" | "doNotBreed" | "unknown",
 *     "reproductionRaw": the status as the server gives it (for a word this API has none for),
 *     "lactation": lactation number, "daysInMilk", "calvingDate" (last calving),
 *     "pregnantDays", "expectedCalving", "dryOffDate" (last dry-off),
 *     "daysSinceInsemination", "daysSinceHeat", "inseminationsThisLactation",
 *     "doNotBreedDate": when the animal was last set to "do not breed" ("keep open") after its last
 *              calving and insemination, "" if not; then "reproduction" is "doNotBreed" even when the
 *              server's own status says otherwise (a server may set it back to open after a heat),
 *     "heats": [{"date", "method": "visual" | "activity" | "unknown"}]   oldest first,
 *     "inseminations": [{"date", "sireName", "sireCode" (AI code), "sireLifeNumber", "type": "ai" |
 *              "diy" (by the farmer) | "bull" | "embryo" | "unknown"}]   oldest first,
 *     "pregnancyChecks": [{"date", "result": "pregnant" | "notPregnant" | "unknown"}]   oldest first
 *   }]}
 * How far back inseminations and pregnancy checks go is up to the source (see the driver's API.md). */
#ifndef COW_C_H
#define COW_C_H

#include "session_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start a read. Returns SESSION_OK (started: now the link's exchange), SESSION_UNSUPPORTED or
 * SESSION_IN_PROGRESS. */
SESSION_API int32_t cow_read(session* s);
SESSION_API int32_t cow_json(session* s, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
