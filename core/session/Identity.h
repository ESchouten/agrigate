// Which animal is where, as a driver reports it: the data behind core/identity_c.h, where every
// field is described.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace agrigate {
namespace identity {

namespace kind {
constexpr const char* MilkingRobot = "milkingRobot";
constexpr const char* FeedingStation = "feedingStation";
constexpr const char* Gate = "gate";
constexpr const char* Unknown = "unknown";
}  // namespace kind

struct Animal {
  std::string number, name, responder;
};

struct Place {
  std::string id, name, kind = kind::Unknown;
  bool occupied = false;   // an animal was identified here
  Animal animal;
  int64_t since = 0;
};

struct Event {
  int64_t seq = 0, time = 0;
  std::string place;
  Animal animal;
};

struct Places {
  int64_t updated = 0;
  std::vector<Place> places;

  /** The place [id], added (as [kind]) when it is new. */
  Place& place(const std::string& id, const char* kind);
  /** The source identified [a] at [p]. The same animal there again within 5 s is the same one. */
  void identified(Place& p, const Animal& a, int64_t now);

  std::string json() const;
  std::string eventsJson(int64_t after) const;

 private:
  std::deque<Event> events_;
  int64_t seq_ = 0;
};

}  // namespace identity
}  // namespace agrigate
