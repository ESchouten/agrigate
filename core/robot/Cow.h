// A farm's herd as a driver fills it in: the data behind core/cow_c.h, where every field is
// described.
#pragma once

#include <string>
#include <vector>

namespace robot {
namespace cow {

namespace production {
constexpr const char* Milking = "milking";
constexpr const char* Dry = "dry";
constexpr const char* Young = "young";
constexpr const char* Unknown = "unknown";
}  // namespace production

namespace reproduction {
constexpr const char* Open = "open";
constexpr const char* Inseminated = "inseminated";
constexpr const char* Pregnant = "pregnant";
constexpr const char* DoNotBreed = "doNotBreed";
constexpr const char* Unknown = "unknown";
}  // namespace reproduction

struct Insemination {
  std::string date, sireName, sireCode, sireLifeNumber, type = "unknown";
};

struct PregnancyCheck {
  std::string date, result = "unknown";
};

struct Animal {
  std::string id, number, name, lifeNumber, workNumber, responder, birthDate, group;
  std::string production = production::Unknown, reproduction = reproduction::Unknown, reproductionRaw;
  double lactation = -1, daysInMilk = -1, pregnantDays = -1;   // -1: not known
  std::string calvingDate, expectedCalving;
  std::vector<Insemination> inseminations;
  std::vector<PregnancyCheck> pregnancyChecks;
};

struct Herd {
  std::vector<Animal> animals;
  std::string json() const;
};

/** "2025-11-24T23:00:00Z" (a day's start in the farm's time zone, given in UTC) or
 *  "2025-11-25T00:00:00" -> "2025-11-25". A UTC time from noon on is taken as the next day:
 *  a farm's midnight lies within 12 hours of UTC. "" for anything else. */
std::string day(const std::string& when);

}  // namespace cow
}  // namespace robot
