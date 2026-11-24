// Every driver in this build: add a new one (drivers/<name>/src) here.
#include "Session.h"

namespace agrigate {

extern const Driver t4c, fullsense, astronaut, discovery;

const Driver* const kDrivers[] = {&t4c, &fullsense, &astronaut, &discovery};
const int kDriverCount = sizeof kDrivers / sizeof *kDrivers;

}  // namespace agrigate
