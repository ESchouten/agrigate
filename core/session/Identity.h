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

namespace event {
constexpr const char* Identified = "identified";
constexpr const char* MilkingStarted = "milkingStarted";
constexpr const char* VisitEnded = "visitEnded";
}  // namespace event

struct Place {
  std::string id, name, kind = kind::Unknown;
  bool occupied = false;   // an animal was identified here
  Animal animal;
  int64_t since = 0;
  int64_t milkingSince = 0;   // 0: not milking
  Animal milked;              // the animal of the milking now (or the last one), for its visit end
  int64_t pendingStart = 0;   // a milking start not yet told: waits a moment for an identification just after it
  std::deque<std::pair<int64_t, Animal>> recent;   // the last identifications here (time, animal)
};

struct Event {
  int64_t seq = 0, time = 0;
  std::string place;
  Animal animal;
  const char* kind = event::Identified;
};

struct Places {
  int64_t updated = 0;
  std::vector<Place> places;

  /** The place [id], added (as [kind]) when it is new. */
  Place& place(const std::string& id, const char* kind);
  /** The source identified [a] at [p]. The same animal there again within 5 s is the same one. */
  void identified(Place& p, const Animal& a, int64_t now);
  /** A milking started at [p]: the animal milked is the one identified closest to [now] (within a minute). */
  void milkingStarted(Place& p, int64_t now);
  /** The robot is done with the animal at [p] and lets it out. */
  void visitEnded(Place& p, int64_t now);
  /** Tells a milking start once its moment has passed (call it from the driver's tick). */
  void settle(int64_t now);

  std::string json() const;
  std::string eventsJson(int64_t after) const;

 private:
  std::deque<Event> events_;
  int64_t seq_ = 0;
  void add(const Place& p, const char* kind, const Animal& a, int64_t now);
  void tellStart(Place& p);
};

}  // namespace identity
}  // namespace agrigate
