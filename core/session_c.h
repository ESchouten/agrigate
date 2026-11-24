/* A session with one source on the farm: the farm's management server, a robot, a feeding
 * station. What a source knows is read through the contracts on top of this header (milk_c.h,
 * cow_c.h, identity_c.h): the same calls and the same JSON whatever the source is and however
 * it is reached. Manure robots are reached directly: clean_c.h.
 *
 * This header is the other side: how a host keeps a session going. The driver (drivers/<name>/)
 * decides what to say and understands the answers; moving the bytes is the host's job, as
 * Bluetooth is in clean_c.h. So the same driver runs in the apps, in Python and on a
 * microcontroller. No threads and no callbacks: one call at a time per session.
 *
 *   s = session_open("<driver>", "192.168.1.10", user, password)
 *   session_tick(s, now_ms)             the time, before every read
 *   milk_read(s)                        (or another contract's read)
 *   ...the link's exchange, below...
 *   session_result(s) == SESSION_OK  ->  milk_json(s, buf, size)
 *   session_close(s)
 *
 * The exchange, by the driver's link (session_drivers_json):
 *   "http"  after a read, until there is no request left:
 *             while (session_request_json(s, buf, size) > 0) {   {"method","url","headers":{..},"body"}
 *               ...send that request...
 *               session_response(s, status, body, len)            status 0: no answer
 *             }
 *           The session logs in by itself, keeps its login for the next read and logs in again
 *           when the server no longer takes it.
 *   "udp"   the source tells by itself. One UDP socket per session, and all the time:
 *             every ~100 ms:  session_tick(s, now_ms)
 *                             while ((n = session_outgoing(s, buf, size)) > 0) send buf[0..n) to address:port
 *             on a datagram from the address:  session_incoming(s, data, len, now_ms)
 *           A read then asks nothing: it is done at once. Before closing: session_stop(s) and
 *           send what session_outgoing gives. What a driver sends is no more than the source
 *           needs to keep telling (a registration), never a command.
 * now_ms: milliseconds since 1970-01-01 UTC; the times in the JSON are the same. */
#ifndef SESSION_C_H
#define SESSION_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define SESSION_API __declspec(dllexport)
#else
#define SESSION_API __attribute__((visibility("default"))) __attribute__((used))
#endif

typedef struct session session;

/* results */
enum {
  SESSION_OK = 0,
  SESSION_UNREACHABLE = 1,    /* a request got no answer; a source that tells by itself was silent for 10 s */
  SESSION_LOGIN_REJECTED = 2, /* wrong user name or password */
  SESSION_UNEXPECTED = 3,     /* an answer the driver does not understand (session_last_error) */
  SESSION_UNSUPPORTED = 4,    /* this driver does not serve that contract */
  SESSION_IN_PROGRESS = 100,  /* requests are still open */
};

/* The drivers in this build: a JSON array of
 *   {"id", "name" (what the source is), "contracts": ["milk", "cow", "identity"],
 *    "link": {"kind": "http", "scheme": "http", "port": 80} or {"kind": "udp", "port": 6000}}
 * scheme and port are the defaults for an address without them. Copies into buf (NUL-terminated,
 * cut to size) and returns the full length, as every *_json call does. */
SESSION_API int32_t session_drivers_json(char* buf, int32_t size);

/* address: "host", "host:port" or (http) a whole "http://host:port". user and password: for a
 * source that wants a login, else "". NULL for a driver not in this build. */
SESSION_API session* session_open(const char* driver, const char* address, const char* user, const char* password);
SESSION_API void session_close(session* s);

/* The time, and for a link that listens: let the driver keep going what it needs (a
 * registration it renews). */
SESSION_API void session_tick(session* s, int64_t now_ms);

/* "http": the next request to send, 0 when there is none (the read is done: see session_result). */
SESSION_API int32_t session_request_json(session* s, char* buf, int32_t size);
/* The answer to that request. */
SESSION_API void session_response(session* s, int32_t status, const char* body, int32_t len);

/* "udp": the next datagram to send to the source: its length, 0 when there is none. It is taken
 * off the queue once it fits in buf. */
SESSION_API int32_t session_outgoing(session* s, uint8_t* buf, int32_t size);
/* A datagram that came from the source's address. */
SESSION_API void session_incoming(session* s, const uint8_t* data, int32_t len, int64_t now_ms);
/* Leave: session_outgoing then gives the goodbye (if the driver has one). */
SESSION_API void session_stop(session* s);

/* How the last read went. */
SESSION_API int32_t session_result(session* s);
/* What went wrong, in English; also what a read that ended SESSION_OK had to leave out. */
SESSION_API int32_t session_last_error(session* s, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
