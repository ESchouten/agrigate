// The live API (core/live_c.h) on a session.
#include "live_c.h"

#include <cstring>

#include "Json.h"
#include "Live.h"
#include "Robot.h"

namespace robot {
namespace live {
namespace {

constexpr int64_t kSameArrivalMs = 5000;
constexpr int64_t kConnectedMs = 10000;
constexpr size_t kEventsKept = 200;

std::string animalJson(const Animal& a) {
  return "\"number\":" + quote(a.number) + ",\"name\":" + quote(a.name) + ",\"responder\":" + quote(a.responder);
}

}  // namespace

void Session::arrival(const Animal& a, int64_t now) {
  heard(now);
  if (hasAnimal_ && animal_.responder == a.responder && animal_.number == a.number && now - animal_.since < kSameArrivalMs) return;
  animal_ = a;
  animal_.since = now;
  hasAnimal_ = true;
  events_.push_back({++seq_, now, "arrival", animal_});
  while (events_.size() > kEventsKept) events_.pop_front();
}

bool Session::next(std::string& datagram) const {
  if (out_.empty()) return false;
  datagram = out_.front();
  return true;
}

std::string Session::json(int64_t now) const {
  std::string o = "{\"address\":" + quote(address) + ",\"connected\":" + (lastMessage_ && now - lastMessage_ < kConnectedMs ? "true" : "false") +
                  ",\"lastMessage\":" + (lastMessage_ ? std::to_string(lastMessage_) : "null") + ",\"animal\":";
  if (!hasAnimal_) return o + "null}";
  return o + "{" + animalJson(animal_) + ",\"since\":" + std::to_string(animal_.since) + "}}";
}

std::string Session::events(int64_t after) const {
  std::string o = "{\"events\":[";
  bool first = true;
  for (const Event& e : events_) {
    if (e.seq <= after) continue;
    o += std::string(first ? "" : ",") + "{\"seq\":" + std::to_string(e.seq) + ",\"time\":" + std::to_string(e.time) +
         ",\"kind\":" + quote(e.kind) + "," + animalJson(e.animal) + "}";
    first = false;
  }
  return o + "],\"last\":" + std::to_string(seq_) + "}";
}

}  // namespace live
}  // namespace robot

extern "C" {

int32_t live_drivers_json(char* buf, int32_t size) {
  std::string o = "[";
  for (int i = 0; i < robot::live::kDriverCount; i++) {
    const robot::live::Driver& d = *robot::live::kDrivers[i];
    o += std::string(i ? "," : "") + "{\"id\":" + robot::quote(d.id) + ",\"name\":" + robot::quote(d.name) +
         ",\"link\":{\"kind\":\"udp\",\"port\":" + std::to_string(d.port) + "}}";
  }
  return robot::copyOut(o + "]", buf, size);
}

live_session* live_open(const char* driver, const char* address) {
  for (int i = 0; i < robot::live::kDriverCount; i++)
    if (driver && !strcmp(robot::live::kDrivers[i]->id, driver)) return new live_session(*robot::live::kDrivers[i], address ? address : "");
  return nullptr;
}

void live_close(live_session* s) { delete s; }

void live_tick(live_session* s, int64_t now_ms) { s->driver.tick(*s, now_ms); }

int32_t live_outgoing(live_session* s, uint8_t* buf, int32_t size) {
  std::string d;
  if (!s->next(d)) return 0;
  if (buf && size >= (int32_t)d.size()) {
    memcpy(buf, d.data(), d.size());
    s->pop();
  }
  return (int32_t)d.size();
}

void live_incoming(live_session* s, const uint8_t* data, int32_t len, int64_t now_ms) {
  if (data && len > 0) s->driver.incoming(*s, std::string((const char*)data, (size_t)len), now_ms);
}

void live_stop(live_session* s) {
  if (s->driver.stop) s->driver.stop(*s);
}

int32_t live_json(live_session* s, int64_t now_ms, char* buf, int32_t size) { return robot::copyOut(s->json(now_ms), buf, size); }

int32_t live_events_json(live_session* s, int64_t after, char* buf, int32_t size) { return robot::copyOut(s->events(after), buf, size); }

}  // extern "C"
