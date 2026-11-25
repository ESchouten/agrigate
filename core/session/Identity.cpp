// The identity API (core/identity_c.h) on a session.
#include "identity_c.h"

#include "Session.h"

namespace agrigate {
namespace identity {
namespace {

constexpr int64_t kSameMs = 5000;
constexpr size_t kEventsKept = 200;
constexpr size_t kRecentKept = 8;
constexpr int64_t kVisitorWithinMs = 60000;   // the animal of a visit was identified at most this long before or after its start
constexpr int64_t kSettleMs = 10000;          // a start is told this long after it (or at the place's next event)
constexpr int64_t kRefusedWithinMs = 30000;   // a refusal comes at most this long after the animal was identified

bool same(const Animal& a, const Animal& b) { return a.responder == b.responder && a.number == b.number; }

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
  if (p.occupied && same(p.animal, a) && now - p.since < kSameMs) return;
  p.occupied = true;
  p.animal = a;
  p.since = now;
  p.recent.push_back({now, a});
  while (p.recent.size() > kRecentKept) p.recent.pop_front();
  add(p, event::Identified, a, now);
}

void Places::visitStarted(Place& p, int64_t now) {
  if (p.pendingStart && now - p.pendingStart >= kSettleMs) tellStart(p);
  if (p.pendingStart) return;   // the same start again
  p.pendingStart = now;         // told by settle() or the place's next event: an identification may come with it
  if (!p.visitSince) p.visitSince = now;
}

// The animal of a visit is the one identified closest in time to its start (within a minute): a
// robot identifies the cow in its box at the moment the milking starts, a cow walking past (into the
// next robot) some time before or after it.
void Places::tellStart(Place& p) {
  const int64_t start = p.pendingStart;
  p.pendingStart = 0;
  const std::pair<int64_t, Animal>* best = nullptr;
  int64_t bestGap = kVisitorWithinMs + 1;
  for (const auto& r : p.recent) {
    const int64_t gap = r.first > start ? r.first - start : start - r.first;
    if (gap <= bestGap) { bestGap = gap; best = &r; }   // equal gap: the later one
  }
  if (p.visitSince != start) {
    // A start during a visit is the same visit (a robot: the cups go on again), unless another
    // animal was identified with it: then the end of the visit before was missed.
    if (!best || bestGap > kSameMs || same(best->second, p.visitor)) return;
    p.visitSince = start;
  }
  p.visitor = best ? best->second : Animal();
  if (best) {
    p.occupied = true;
    p.animal = best->second;
    p.since = best->first;
  }
  add(p, event::VisitStarted, p.visitor, start);
}

void Places::refused(Place& p, const std::string& responder, int64_t now) {
  // A source may send the same record at the end of a visit: only one soon after the animal was
  // identified, without a visit, is a refusal.
  const std::pair<int64_t, Animal>* id = nullptr;
  for (auto it = p.recent.rbegin(); it != p.recent.rend(); ++it)
    if (it->second.responder == responder) { id = &*it; break; }
  if (!id || now - id->first > kRefusedWithinMs) return;
  const int64_t started = p.pendingStart ? p.pendingStart : p.visitSince;
  if (started && started >= id->first - kVisitorWithinMs) return;   // it is (or was just) being served
  if (p.lastEnd && p.lastEnd >= id->first) return;                  // the record of a visit that just ended
  add(p, event::Refused, id->second, now);
}

void Places::settle(int64_t now) {
  for (Place& p : places)
    if (p.pendingStart && now - p.pendingStart >= kSettleMs) tellStart(p);
}

void Places::visitEnded(Place& p, int64_t now) {
  if (p.pendingStart) tellStart(p);
  const bool visit = p.visitSince != 0;
  p.visitSince = 0;
  p.lastEnd = now;
  add(p, event::VisitEnded, visit ? p.visitor : Animal(), now);
}

std::string Places::json() const {
  std::string o = "{\"updated\":" + timeJson(updated) + ",\"places\":[";
  for (size_t i = 0; i < places.size(); i++) {
    const Place& p = places[i];
    o += std::string(i ? "," : "") + "{\"id\":" + quote(p.id) + ",\"name\":" + quote(p.name) + ",\"kind\":" + quote(p.kind) + ",\"animal\":" +
         (p.occupied ? "{" + animalJson(p.animal) + ",\"since\":" + std::to_string(p.since) + "}" : "null") +
         ",\"visitSince\":" + (p.visitSince ? std::to_string(p.visitSince) : "null") + "}";
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
