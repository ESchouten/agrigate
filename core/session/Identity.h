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
constexpr const char* VisitStarted = "visitStarted";
constexpr const char* VisitEnded = "visitEnded";
constexpr const char* Refused = "refused";
}  // namespace event

struct Place {
  std::string id, name, kind = kind::Unknown;
  bool occupied = false;   // an animal was identified here
  Animal animal;
  int64_t since = 0;
  int64_t visitSince = 0;     // the place serves its animal since then (a robot: milks it); 0: it does not
  Animal visitor;             // the animal of that visit (or of the last one), for its end
  int64_t lastEnd = 0;        // the last visit end
  int64_t pendingStart = 0;   // a start not yet told: an identification may still come with it
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
  /** [p] started serving an animal (a robot: the milking started): the one identified closest to
      [now], within a minute. A start during a visit is the same visit. */
  void visitStarted(Place& p, int64_t now);
  /** [p] is done with its animal and lets it out. */
  void visitEnded(Place& p, int64_t now);
  /** [p] does not serve the animal with [responder] and lets it out. */
  void refused(Place& p, const std::string& responder, int64_t now);
  /** Tells a start once its moment has passed (call it from the driver's tick). */
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
