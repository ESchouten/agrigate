// The milking robot API (core/milk_c.h) on a session.
#include "milk_c.h"

#include <cstdio>

#include "Robot.h"

namespace robot {

namespace milk {
namespace {
std::string number(double v) {
  if (v < 0) return "null";
  char b[32];
  snprintf(b, sizeof b, "%.10g", v);
  return b;
}
}  // namespace

std::string Farm::json() const {
  std::string o = "{\"robots\":[";
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
}  // namespace robot

extern "C" {

int32_t milk_read(robot_session* s) {
  if (!s->driver.milk) return ROBOT_UNSUPPORTED;
  return s->read([](robot::Session& session) {
    session.milk = {};
    session.driver.milk(session);
  });
}

int32_t milk_json(robot_session* s, char* buf, int32_t size) { return robot::copyOut(s->milk.json(), buf, size); }

}  // extern "C"
