// The identity API (core/identity_c.h) on a session.
#include "identity_c.h"

#include "Session.h"

namespace agrigate {
namespace identity {
namespace {

constexpr int64_t kSameMs = 5000;
constexpr size_t kEventsKept = 200;
constexpr int64_t kMilkedWithinMs = 60000;   // the animal milked was identified at most this long before or after the start
constexpr int64_t kSettleMs = 10000;         // a start is told this long after it (or at the next event of the place)
constexpr size_t kRecentKept = 8;
constexpr int64_t kSameMilkingMs = 120000;   // a start within this time of the last one is the same milking (a missed visit end otherwise)

std::string animalJson(const Animal& a) {
  return "\"number\":" + quote(a.number) + ",\"name\":" + quote(a.name) + ",\"responder\":" + quote(a.responder);
}

}  // namespace

Place& Places::place(const std::string& id, const char* kind) {
  for (Place& p : places)
    if (p.id == id) return p;
  places.emplace_back();
  places.back().id = id;
  places.back().kind = kind;
  return places.back();
}

void Places::add(const Place& p, const char* kind, const Animal& a, int64_t now) {
  Event e;
  e.seq = ++seq_;
  e.time = now;
  e.place = p.id;
  e.animal = a;
  e.kind = kind;
  events_.push_back(e);
  while (events_.size() > kEventsKept) events_.pop_front();
}

void Places::identified(Place& p, const Animal& a, int64_t now) {
  if (p.pendingStart && now - p.pendingStart >= kSettleMs) tellStart(p);
  if (p.occupied && p.animal.responder == a.responder && p.animal.number == a.number && now - p.since < kSameMs) return;
  p.occupied = true;
  p.animal = a;
  p.since = now;
  p.recent.push_back({now, a});
  while (p.recent.size() > kRecentKept) p.recent.pop_front();
  add(p, event::Identified, a, now);
}

void Places::milkingStarted(Place& p, int64_t now) {
  if (p.milkingSince && now - p.milkingSince < kSameMilkingMs) return;   // the same milking (the cups again)
  if (p.pendingStart) tellStart(p);
  p.milkingSince = now;
  p.pendingStart = now;   // told by settle() or the next event: an identification may come with it
}

// The animal milked is the one identified closest in time to the start (within a minute): on a robot
// the cow in the box is identified at the moment the milking starts, a cow walking past (into the next
// robot) some time before or after it.
void Places::tellStart(Place& p) {
  const int64_t start = p.pendingStart;
  p.pendingStart = 0;
  const Animal* best = nullptr;
  int64_t bestGap = kMilkedWithinMs + 1;
  for (const auto& r : p.recent) {
    const int64_t gap = r.first > start ? r.first - start : start - r.first;
    if (gap <= bestGap) { bestGap = gap; best = &r.second; }   // equal gap: the later one
  }
  p.milked = best ? *best : Animal();
  if (best) {
    p.occupied = true;
    p.animal = *best;
  }
  add(p, event::MilkingStarted, p.milked, start);
}

void Places::settle(int64_t now) {
  for (Place& p : places)
    if (p.pendingStart && now - p.pendingStart >= kSettleMs) tellStart(p);
}

void Places::visitEnded(Place& p, int64_t now) {
  if (p.pendingStart) tellStart(p);
  const bool milking = p.milkingSince != 0;
  p.milkingSince = 0;
  add(p, event::VisitEnded, milking ? p.milked : Animal(), now);
}

std::string Places::json() const {
  std::string o = "{\"updated\":" + timeJson(updated) + ",\"places\":[";
  for (size_t i = 0; i < places.size(); i++) {
    const Place& p = places[i];
    o += std::string(i ? "," : "") + "{\"id\":" + quote(p.id) + ",\"name\":" + quote(p.name) + ",\"kind\":" + quote(p.kind) + ",\"animal\":" +
         (p.occupied ? "{" + animalJson(p.animal) + ",\"since\":" + std::to_string(p.since) + "}" : "null") +
         ",\"milking\":" + (p.milkingSince ? "true" : "false") + ",\"milkingSince\":" + (p.milkingSince ? std::to_string(p.milkingSince) : "null") + "}";
  }
  return o + "]}";
}

std::string Places::eventsJson(int64_t after) const {
  std::string o = "{\"events\":[";
  bool first = true;
  for (const Event& e : events_) {
    if (e.seq <= after) continue;
    o += std::string(first ? "" : ",") + "{\"seq\":" + std::to_string(e.seq) + ",\"time\":" + std::to_string(e.time) +
         ",\"kind\":" + quote(e.kind) + ",\"place\":" + quote(e.place) + ",\"animal\":{" + animalJson(e.animal) + "}}";
    first = false;
  }
  return o + "],\"last\":" + std::to_string(seq_) + "}";
}

}  // namespace identity
}  // namespace agrigate

extern "C" {

int32_t identity_read(session* s) {
  if (!s->driver.identity) return SESSION_UNSUPPORTED;
  return s->read(s->driver.identity, &s->identity.updated);   // places and events stay: a read adds to them
}

int32_t identity_json(session* s, char* buf, int32_t size) { return agrigate::copyOut(s->identity.json(), buf, size); }

int32_t identity_events_json(session* s, int64_t after, char* buf, int32_t size) {
  return agrigate::copyOut(s->identity.eventsJson(after), buf, size);
}

}  // extern "C"
