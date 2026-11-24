/* Live: which animal is at a robot right now, as it happens. Read from what the robot itself says
 * on the farm network, not from the farm's server (which knows a visit only after it). One session
 * per robot; a farm with ten robots has ten. It only listens: what a driver sends is no more than
 * the robot needs to hand its messages on (a registration), never a command.
 *
 * Link "udp": the host has one UDP socket per session and moves datagrams; the driver says what
 * to send and is handed what came back from the robot's address. Like robot_c.h and clean_c.h
 * there are no threads and no callbacks, so the same driver runs in the apps, in Python and on a
 * microcontroller:
 *   s = live_open("astronaut", "10.4.1.101")
 *   every ~100 ms:  live_tick(s, now_ms)
 *                   while ((n = live_outgoing(s, buf, size)) > 0) send buf[0..n) to address:port
 *   on a datagram from the robot:  live_incoming(s, data, len, now_ms)
 *   live_json(s, now_ms, ...)          the animal at the robot now
 *   live_events_json(s, after, ...)    arrivals since event `after`
 *   live_stop(s), send what live_outgoing gives, live_close(s)
 * now_ms: milliseconds since 1970-01-01 UTC; the times in the JSON are the same. */
#ifndef LIVE_C_H
#define LIVE_C_H

#include <stdint.h>

#include "robot_c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct live_session live_session;

/* The live drivers in this build: [{"id", "name", "link": {"kind": "udp", "port": 6000}}]. */
ROBOT_API int32_t live_drivers_json(char* buf, int32_t size);

/* address: the robot's IP address. NULL for a driver not in this build. */
ROBOT_API live_session* live_open(const char* driver, const char* address);
ROBOT_API void live_close(live_session* s);

/* Let the driver keep what it needs going (a registration it renews). */
ROBOT_API void live_tick(live_session* s, int64_t now_ms);
/* The next datagram to send to the robot (to the link's port): its length, 0 when there is none.
 * It is taken off the queue once it fits in buf. */
ROBOT_API int32_t live_outgoing(live_session* s, uint8_t* buf, int32_t size);
/* A datagram that came from the robot's address. */
ROBOT_API void live_incoming(live_session* s, const uint8_t* data, int32_t len, int64_t now_ms);
/* Leave: live_outgoing then gives the goodbye (if the driver has one). */
ROBOT_API void live_stop(live_session* s);

/* {"address", "connected": the robot spoke in the last 10 s, "lastMessage": time or null,
 *  "animal": {"number", "name", "responder", "since"} or null}
 * "animal" is the last one the robot identified: it stays until the next one, as the robot does
 * not always say when an animal leaves (an animal it refuses walks on without a word). */
ROBOT_API int32_t live_json(live_session* s, int64_t now_ms, char* buf, int32_t size);
/* {"events": [{"seq", "time", "kind": "arrival", "number", "name", "responder"}], "last": seq}
 * with seq > after, oldest first; the session keeps the last 200. */
ROBOT_API int32_t live_events_json(live_session* s, int64_t after, char* buf, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
