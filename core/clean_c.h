/* The robot API in C, the same for every robot driver (drivers/<name>/ implements it), for
 * hosts that do Bluetooth themselves: the apps (JNI, cinterop) and Python (ctypes). The M5Stack
 * uses the driver's C++ API directly.
 *
 * The driver works with bytes in and bytes out; how they travel is the host's job. The host
 * opens one of the links the driver lists (clean_links_json) and then:
 *   s = clean_new(pin, version, on_write, on_done, user)
 *   bytes from the robot     -> clean_feed(s, bytes, n)      (any thread)
 *   on_write(user, bytes, n) -> send them to the robot, then clean_release(bytes)
 *   clean_connect(s); ... on_done(user, CLEAN_CMD_CONNECT, CLEAN_OK)
 *   clean_start_route(s, 'A', 0); ... on_done(user, CLEAN_CMD_ROUTE, result)
 *   clean_free(s); close the link
 * Commands run one at a time on the session's own thread; on_write and on_done are
 * called from that thread. All logic (screens, steps, safety rules) lives in the driver. */
#ifndef CLEAN_C_H
#define CLEAN_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define CLEAN_API __declspec(dllexport)
#else
#define CLEAN_API __attribute__((visibility("default"))) __attribute__((used))
#endif

typedef struct clean_session clean_session;
/* data is a copy owned by the host: call clean_release(data) when done with it, so an
 * asynchronous host (Dart's NativeCallable.listener) can read it later. */
typedef void (*clean_write_fn)(void* user, const uint8_t* data, int32_t len);
typedef void (*clean_done_fn)(void* user, int32_t command, int32_t result);

/* results */
enum {
  CLEAN_OK = 0,
  CLEAN_CONNECT_FAILED = 1,
  CLEAN_PIN_REJECTED = 2,
  CLEAN_NO_SCREEN = 3,
  CLEAN_BUSY = 4,
  CLEAN_UNEXPECTED = 5,
  CLEAN_STARTED_NO_ANSWER = 6,
  CLEAN_CANCELLED = 7, /* clean_cancel: stopped before its next key, robot put back as it was */
  CLEAN_IN_PROGRESS = 100, /* returned by a command call while another one runs */
};

/* progress steps, reported to the progress callback while a command runs; the status is
 * up to date when it is called */
enum {
  CLEAN_STEP_LINKED = 1,
  CLEAN_STEP_GRANTED = 2,
  CLEAN_STEP_SCREEN_READY = 3,
  CLEAN_STEP_PLANNING_OFF = 4,
  CLEAN_STEP_PLANNING_ON = 5,
  CLEAN_STEP_MENU = 6,
  CLEAN_STEP_SELECTED = 7,
  CLEAN_STEP_START_PRESSED = 8,
  CLEAN_STEP_ANSWERED = 9,
  CLEAN_STEP_RESTORED = 10,
  CLEAN_STEP_SYNCING = 11, /* again for every part; status "sync" = percent done, -1 = unknown */
};
typedef void (*clean_progress_fn)(void* user, int32_t step);

/* commands, as reported to on_done */
enum {
  CLEAN_CMD_CONNECT = 1,
  CLEAN_CMD_ROUTE = 2,
  CLEAN_CMD_PLANNING = 3,
  CLEAN_CMD_PRESS = 4,
  CLEAN_CMD_FINISH = 5,
  CLEAN_CMD_STOP = 6,
  CLEAN_CMD_ROUTES = 7,
  CLEAN_CMD_ANSWER_STOP = 8,
  CLEAN_CMD_HOLD = 9,
};

/* pin: the robot's PIN ("" or NULL = none). image_version: 9 bytes from last
 * time, or NULL. The link counts as up until clean_link_down(). */
CLEAN_API clean_session* clean_new(const char* pin, const uint8_t* image_version,
                                clean_write_fn on_write, clean_done_fn on_done, void* user);
CLEAN_API void clean_free(clean_session* s); /* stops the thread; waits for a running command */

/* Optional: progress steps for a UI (called from the session thread). Set before commands. */
CLEAN_API void clean_on_progress(clean_session* s, clean_progress_fn on_progress);

CLEAN_API void clean_release(const uint8_t* data); /* frees a buffer passed to on_write */

CLEAN_API void clean_feed(clean_session* s, const uint8_t* data, int32_t len);
CLEAN_API void clean_link_down(clean_session* s); /* BLE dropped: running waits end quickly */

/* Start a command. Returns CLEAN_OK (started) or CLEAN_IN_PROGRESS. */
CLEAN_API int32_t clean_connect(clean_session* s);
CLEAN_API int32_t clean_start_route(clean_session* s, char letter, int32_t dry_run);
CLEAN_API int32_t clean_set_planning(clean_session* s, int32_t on);
/* Safety stop: only while a route runs ("driving": true in the status), one tap on
 * Start/Stop. A route button pressed during a route should call this, not start. */
CLEAN_API int32_t clean_stop_route(clean_session* s);
/* The robot then asks "route afmaken?" ("stopQuestion": true in the status). Pass the user's
 * answer on: finish 1 = Ja (drives on), 0 = Nee (route over, planning stays off). Nothing is
 * pressed if the question is not on the screen. */
CLEAN_API int32_t clean_answer_stop(clean_session* s, int32_t finish);
CLEAN_API int32_t clean_press(clean_session* s, int32_t key); /* the driver's panel key codes */
/* Hold a key down as on the robot's own panel (in manual control the robot drives while
 * Start/Stop is held), until clean_unhold, the link drops or 2 minutes pass; on_done
 * comes after the key went up. clean_unhold does not wait and may come from any thread. */
CLEAN_API int32_t clean_hold(clean_session* s, int32_t key);
CLEAN_API void clean_unhold(clean_session* s);
/* Cancel the running command (the user pressed Cancel); any thread, does not wait. It stops
 * before its next key and puts the robot back as it was (home screen, planning as before);
 * on_done reports CLEAN_CANCELLED. A route can be cancelled until Start is pressed on the
 * robot: after that it runs, and clean_stop_route stops it. */
CLEAN_API void clean_cancel(clean_session* s);
/* The status JSON's "cancellable" is true while a command runs that clean_cancel can still
 * stop (show a Cancel button then); false otherwise. */
/* Read what routes A..D consist of (HANDM. ROUTE), then back as before. Afterwards
 * clean_routes_json gives {"A":"1","B":"2 3","C":"1 2 3","D":"4"} ("{}" before). */
CLEAN_API int32_t clean_read_routes(clean_session* s);
CLEAN_API int32_t clean_routes_json(clean_session* s, char* buf, int32_t size);
/* Before clean_free: back to the home screen from a safe menu and the planning back to how it
 * was at connect (unless clean_set_planning changed it on purpose). */
CLEAN_API int32_t clean_finish(clean_session* s);

/* Planning owed after a dropped link (see the driver): 1 / 0 = the
 * planning state the robot must be put back to, -1 = nothing owed. Read it after every
 * on_done and keep it (with the time) per robot; hand a recent value (a few hours at most)
 * to the next session with clean_set_owed_planning before clean_connect, which then puts the
 * robot back first. */
CLEAN_API void clean_set_owed_planning(clean_session* s, int32_t owed);
CLEAN_API int32_t clean_owed_planning(clean_session* s);

/* Block until the running command is done; returns its result (CLEAN_OK if none). */
CLEAN_API int32_t clean_wait(clean_session* s);

/* Copies into buf (NUL-terminated, cut to size). Return the full length. */
CLEAN_API int32_t clean_status_json(clean_session* s, char* buf, int32_t size);
CLEAN_API int32_t clean_last_error(clean_session* s, char* buf, int32_t size);
/* Copies the 9-byte image version; returns 1 if the robot sent a new one. */
CLEAN_API int32_t clean_image_version(clean_session* s, uint8_t* out9);
/* The PIN as the core sends it into out (for the fake robots in tests);
 * returns its length (16 or 32 bytes for up to 15 / 31 characters). For the apps' fake robot. */
CLEAN_API int32_t clean_encrypt_pin(const char* pin, uint8_t* out, int32_t size);
/* The result as text for the UI, in a language: "nl" or "en" (anything else: Dutch). */
CLEAN_API const char* clean_result_text_lang(int32_t result, const char* language);
/* Language of clean_last_error and the log: "nl" (default) or "en". The status JSON has
 * "language": the robot's own language ("nl", "en" or "" if not known). */
CLEAN_API void clean_set_language(clean_session* s, const char* language);

/* How to find and reach this driver's robots: a JSON array, most preferred first, of objects
 * with a "kind" and its details. Hosts implement each kind once and use the kinds they can;
 * a driver that talks another way names a new kind.
 *   "ble-serial": {"service", "rx", "tx" (UUIDs), "addressPrefix" (lower case, "" = any)}:
 *                 write to rx without response, notifications from tx.
 *   "bt-serial":  {"service" (the RFCOMM service UUID), "addressPrefix"}: classic Bluetooth,
 *                 a byte stream both ways.
 * The bytes are the same over every kind. Copies into buf like clean_status_json. */
CLEAN_API int32_t clean_links_json(char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
