// The milking robot API (core/milk_c.h) on a session.
#include "milk_c.h"

#include "Session.h"

namespace agrigate {

namespace milk {

std::string Farm::json() const {
  std::string o = "{\"updated\":" + timeJson(updated) + ",\"robots\":[";
  for (size_t i = 0; i < robots.size(); i++) {
    const Robot& r = robots[i];
    o += std::string(i ? "," : "") + "{\"id\":" + quote(r.id) + ",\"name\":" + quote(r.name) + ",\"group\":" + quote(r.group) +
         ",\"state\":" + quote(r.state) + ",\"raw\":" + quote(r.raw) + ",\"since\":" + quote(r.since) +
         ",\"lastVisit\":" + quote(r.lastVisit) + ",\"alarms\":[";
    for (size_t k = 0; k < r.alarms.size(); k++) {
      const Alarm& a = r.alarms[k];
      o += std::string(k ? "," : "") + "{\"time\":" + quote(a.time) + ",\"severity\":" + quote(a.severity) +
           ",\"code\":" + quote(a.code) + ",\"text\":" + quote(a.text) + "}";
    }
    o += "],\"milkKg\":" + number(r.milkKg) + ",\"milkings\":" + number(r.milkings) +
         ",\"failedMilkings\":" + number(r.failedMilkings) + "}";
  }
  return o + "]}";
}

}  // namespace milk
}  // namespace agrigate

extern "C" {

int32_t milk_read(session* s) {
  if (!s->driver.milk) return SESSION_UNSUPPORTED;
  return s->read([](agrigate::Session& session) {
    session.milk.robots.clear();
    session.driver.milk(session);
  }, &s->milk.updated);
}

int32_t milk_json(session* s, char* buf, int32_t size) { return agrigate::copyOut(s->milk.json(), buf, size); }

}  // extern "C"
