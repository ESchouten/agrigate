/* Clean: manure robots, the same for every driver that serves "clean", on a session of
 * session_c.h (the robot's PIN is the session's password).
 *
 * A call here starts a command; the robot carries it out as the session goes on (the link's
 * exchange, session_c.h) and it is done when session_result is no longer SESSION_IN_PROGRESS.
 * One command at a time. All logic (screens, steps, safety rules) lives in the driver.
 *
 *   s = session_open("<driver>", address, "", pin)
 *   clean_remember(s, memory)              what clean_memory gave last time for this robot
 *   clean_connect(s); ...the exchange...   session_result(s) == SESSION_OK
 *   clean_start_route(s, 'A', 0); ...      session_result(s)
 *   clean_finish(s); ...                   leaves the robot as it was found
 *   clean_memory(s, buf, size)             keep it with the robot
 *   session_close(s); close the link
 *
 * clean_status_json, the robot as last seen:
 *   {"planning": the schedule on or off (null: not known, the robot is not at rest),
 *    "nextRun": the next scheduled run as the robot shows it ("" if none),
 *    "route": the route that runs ("" if none), "activity": what the robot is doing, in its words,
 *    "batteryMv" (null if not known),
 *    "busy": not ready for a command, "driving": a route runs (clean_stop_route stops it),
 *    "finishQuestion": after a stop the robot asks whether to finish the route (clean_answer_stop),
 *    "cancellable": a command runs that clean_cancel can still stop,
 *    "progress": 0-100 while a long part of a command runs (a first connection), else null,
 *    "own": {...}}   what only this make of robot has (its display, its manual control): see
 *                    its driver. The same goes for clean_input's names and the steps' names. */
#ifndef CLEAN_C_H
#define CLEAN_C_H

#include "session_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* results of a command, next to those of session_c.h */
enum {
  CLEAN_BUSY = 10,            /* the robot is not ready for it (driving, in a menu): nothing was pressed */
  CLEAN_NO_CONFIRMATION = 11, /* sent, but the robot's confirmation was not seen */
  CLEAN_CANCELLED = 12,       /* clean_cancel stopped it in time: the robot is back as it was */
};

/* Start a command. Returns SESSION_OK (started), SESSION_IN_PROGRESS (another one runs) or
 * SESSION_UNSUPPORTED (this session's driver does not serve "clean"). */
SESSION_API int32_t clean_connect(session* s); /* log in and read the robot */
SESSION_API int32_t clean_start_route(session* s, char letter, int32_t dry_run); /* dry_run: only the checks */
/* Safety stop: only while a route runs ("driving"). A route button pressed during a route
 * should call this, not start. */
SESSION_API int32_t clean_stop_route(session* s);
/* The robot then asks whether to finish the route ("finishQuestion"). Pass the user's answer
 * on: finish 1 = yes (drives on), 0 = no (route over, the schedule stays off). Never answer it
 * for the user. Nothing happens if the robot does not ask. */
SESSION_API int32_t clean_answer_stop(session* s, int32_t finish);
SESSION_API int32_t clean_set_planning(session* s, int32_t on);
/* Read what the routes consist of; afterwards clean_routes_json gives
 * {"A":"1","B":"2 3","C":"1 2 3","D":"4"} ("{}" before). Only reads. */
SESSION_API int32_t clean_read_routes(session* s);
/* Before closing: the robot back at rest from a safe menu and the schedule back to how it was
 * at connect (unless clean_set_planning changed it on purpose). */
SESSION_API int32_t clean_finish(session* s);
/* Cancel the running command (the user pressed Cancel): it stops before its next step and puts
 * the robot back as it was; the command ends with CLEAN_CANCELLED. A route can be cancelled until
 * the robot starts it ("cancellable"): after that it runs, and clean_stop_route stops it. */
SESSION_API void clean_cancel(session* s);

/* The robot's own inputs (the keys of its panel), by the driver's names for them. hold 0: a tap.
 * hold 1: down and held as on the robot itself (in manual control a robot drives while a key is
 * held) until clean_release, the link drops or 2 minutes pass; the command ends after the input
 * came up. SESSION_UNEXPECTED for a name the driver does not have. */
SESSION_API int32_t clean_input(session* s, const char* name, int32_t hold);
SESSION_API void clean_release(session* s);

/* Copy into buf (NUL-terminated, cut to size) and return the full length, as every *_json does. */
SESSION_API int32_t clean_status_json(session* s, char* buf, int32_t size);
SESSION_API int32_t clean_routes_json(session* s, char* buf, int32_t size);
/* Progress for a UI: the next step the running command took and that was not given yet, by the
 * driver's name for it ("" when there is none); it is taken off once it fits in buf. The status
 * is at least as new as the step. */
SESSION_API int32_t clean_next_step(session* s, char* buf, int32_t size);
/* What the driver wants kept for this robot until the next session (what makes the next
 * connection faster, what the robot is still owed after a dropped link): read it after every
 * command, keep it with the robot as it is and hand it to the next session before
 * clean_connect. */
SESSION_API int32_t clean_memory(session* s, char* buf, int32_t size);
SESSION_API void clean_remember(session* s, const char* memory);
/* The language of session_last_error for this session: "nl" or "en" (the default). */
SESSION_API void clean_set_language(session* s, const char* language);

#ifdef __cplusplus
}
#endif
#endif
