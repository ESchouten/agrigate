/* Robots that are not reached themselves: the farm's management server knows them and answers
 * over HTTP on the farm network. This is what every kind of robot shares there: the drivers
 * (drivers/<name>/, one per server software), a session with one server and the HTTP exchange.
 * What a kind of robot can do is in its own header on top of this one (milk_c.h); a next
 * kind adds its header the same way. Manure scrapers are reached directly: scraper_c.h.
 *
 * The driver decides what to ask and understands the answers; the HTTP itself is the host's
 * job, as Bluetooth is in scraper_c.h. So the same driver runs in the apps, in Python and on a
 * microcontroller:
 *   s = robot_open("<driver>", "192.168.1.10", user, password)
 *   milk_read(s)                                   (or another kind's call)
 *   while (robot_request_json(s, buf, size) > 0) {    {"method","url","headers":{..},"body"}
 *     ...send that request...
 *     robot_response(s, status, body, len)            status 0: no answer
 *   }
 *   robot_result(s) == ROBOT_OK  ->  milk_json(s, buf, size)
 *   robot_close(s)
 * No threads and no callbacks: one call at a time per session. A session logs in by itself,
 * keeps its login for the next read and logs in again when the server no longer takes it. */
#ifndef ROBOT_C_H
#define ROBOT_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define ROBOT_API __declspec(dllexport)
#else
#define ROBOT_API __attribute__((visibility("default"))) __attribute__((used))
#endif

typedef struct robot_session robot_session;

/* results */
enum {
  ROBOT_OK = 0,
  ROBOT_UNREACHABLE = 1,    /* a request got no answer */
  ROBOT_LOGIN_REJECTED = 2, /* wrong user name or password */
  ROBOT_UNEXPECTED = 3,     /* an answer the driver does not understand (robot_last_error) */
  ROBOT_UNSUPPORTED = 4,    /* this driver does not know that kind of robot */
  ROBOT_IN_PROGRESS = 100,  /* requests are still open */
};

/* The drivers in this build: a JSON array of
 *   {"id", "name" (the server software), "kinds": ["milk", ..],
 *    "link": {"kind": "http", "scheme": "http", "port": 80}}
 * "http": the user gives the server's address; scheme and port are the defaults for an address
 * without them. Copies into buf (NUL-terminated, cut to size) and returns the full length, as
 * every *_json call here does. */
ROBOT_API int32_t robot_drivers_json(char* buf, int32_t size);

/* address: "host", "host:port" or a whole "http://host:port". NULL for a driver not in this build. */
ROBOT_API robot_session* robot_open(const char* driver, const char* address, const char* user, const char* password);
ROBOT_API void robot_close(robot_session* s);

/* The next request to send, 0 when there is none (the read is done: see robot_result). */
ROBOT_API int32_t robot_request_json(robot_session* s, char* buf, int32_t size);
/* The answer to that request. */
ROBOT_API void robot_response(robot_session* s, int32_t status, const char* body, int32_t len);

ROBOT_API int32_t robot_result(robot_session* s);
/* What went wrong, in English; also what a read that ended ROBOT_OK had to leave out. */
ROBOT_API int32_t robot_last_error(robot_session* s, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
