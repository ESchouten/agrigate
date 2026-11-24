/* The Discovery driver's own call next to the contracts (core/clean_c.h). */
#ifndef DISCOVERY_C_H
#define DISCOVERY_C_H

#include "session_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The PIN as the driver sends it, into out; returns its length (16 or 32 bytes for up to 15 /
 * 31 characters). For the apps' simulated robot. */
SESSION_API int32_t discovery_encrypt_pin(const char* pin, uint8_t* out, int32_t size);

#ifdef __cplusplus
}
#endif
#endif
