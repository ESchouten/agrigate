// The identity API (core/identity_c.h) on a session.
#include "identity_c.h"

#include "Session.h"

namespace agrigate {
namespace identity {
namespace {

constexpr int64_t kSameMs = 5000;
constexpr size_t kEventsKept = 200;

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

void Places::identified(Place& p, const Animal& a, int64_t now) {
  if (p.occupied && p.animal.responder == a.responder && p.animal.number == a.number && now - p.since < kSameMs) return;
  p.occupied = true;
  p.animal = a;
  p.since = now;
  events_.push_back({++seq_, now, p.id, a});
  while (events_.size() > kEventsKept) events_.pop_front();
}

std::string Places::json() const {
  std::string o = "{\"updated\":" + timeJson(updated) + ",\"places\":[";
  for (size_t i = 0; i < places.size(); i++) {
    const Place& p = places[i];
    o += std::string(i ? "," : "") + "{\"id\":" + quote(p.id) + ",\"name\":" + quote(p.name) + ",\"kind\":" + quote(p.kind) + ",\"animal\":" +
         (p.occupied ? "{" + animalJson(p.animal) + ",\"since\":" + std::to_string(p.since) + "}" : "null") + "}";
  }
  return o + "]}";
}

std::string Places::eventsJson(int64_t after) const {
  std::string o = "{\"events\":[";
  bool first = true;
  for (const Event& e : events_) {
    if (e.seq <= after) continue;
    o += std::string(first ? "" : ",") + "{\"seq\":" + std::to_string(e.seq) + ",\"time\":" + std::to_string(e.time) +
         ",\"kind\":\"identified\",\"place\":" + quote(e.place) + ",\"animal\":{" + animalJson(e.animal) + "}}";
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
