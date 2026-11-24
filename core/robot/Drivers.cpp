// Every driver for a farm's server in this build: add a new one (drivers/<name>/src) here.
#include "Live.h"
#include "Robot.h"

namespace robot {

extern const Driver t4c, fullsense;

const Driver* const kDrivers[] = {&t4c, &fullsense};
const int kDriverCount = sizeof kDrivers / sizeof *kDrivers;

// Every live driver (core/live_c.h): which animal is at a robot, from the robot itself.
namespace live {
extern const Driver astronaut;

const Driver* const kDrivers[] = {&astronaut};
const int kDriverCount = sizeof kDrivers / sizeof *kDrivers;
}  // namespace live

}  // namespace robot
