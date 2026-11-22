// A farm's milking robots as a driver fills them in: the data behind core/milking_c.h, where
// every field is described.
#pragma once

#include <string>
#include <vector>

namespace robot {
namespace milking {

namespace state {
constexpr const char* Idle = "idle";
constexpr const char* Milking = "milking";
constexpr const char* Cleaning = "cleaning";
constexpr const char* Alarm = "alarm";
constexpr const char* Emergency = "emergency";
constexpr const char* Assistance = "assistance";
constexpr const char* Paused = "paused";
constexpr const char* Offline = "offline";
constexpr const char* Unknown = "unknown";
}  // namespace state

struct Alarm {
  std::string time, severity = "unknown", code, text;
};

struct Robot {
  std::string id, name, group;
  std::string state = state::Unknown, raw, since, lastVisit;
  std::vector<Alarm> alarms;
  double milkKg = -1, milkings = -1, failedMilkings = -1;   // -1: not known
};

struct Farm {
  std::vector<Robot> robots;
  std::string json() const;
};

}  // namespace milking
}  // namespace robot
