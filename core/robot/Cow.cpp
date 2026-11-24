// The herd API (core/cow_c.h) on a session.
#include "cow_c.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "Robot.h"

namespace robot {
namespace cow {
namespace {

std::string number(double v) {
  if (v < 0) return "null";
  char b[32];
  snprintf(b, sizeof b, "%.10g", v);
  return b;
}

// days since 1970-01-01 and back (Howard Hinnant's civil calendar), for the day after a UTC evening
long daysFrom(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

void civil(long z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = (int)(yoe + era * 400 + (m <= 2));
}

}  // namespace

std::string day(const std::string& when) {
  int y, h = 0;
  unsigned m, d;
  if (when.size() < 10 || sscanf(when.c_str(), "%4d-%2u-%2u", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return "";
  if (when.size() >= 13 && when[10] == 'T') h = atoi(when.c_str() + 11);
  const bool utc = when.back() == 'Z';
  if (utc && h >= 12) civil(daysFrom(y, m, d) + 1, y, m, d);
  char b[16];
  snprintf(b, sizeof b, "%04d-%02u-%02u", y, m, d);
  return b;
}

std::string Herd::json() const {
  std::string o = "{\"animals\":[";
  for (size_t i = 0; i < animals.size(); i++) {
    Animal a = animals[i];   // the events oldest first, whatever order the server gave them in
    std::stable_sort(a.inseminations.begin(), a.inseminations.end(), [](const Insemination& x, const Insemination& y) { return x.date < y.date; });
    std::stable_sort(a.pregnancyChecks.begin(), a.pregnancyChecks.end(), [](const PregnancyCheck& x, const PregnancyCheck& y) { return x.date < y.date; });
    o += std::string(i ? "," : "") + "{\"id\":" + quote(a.id) + ",\"number\":" + quote(a.number) + ",\"name\":" + quote(a.name) +
         ",\"lifeNumber\":" + quote(a.lifeNumber) + ",\"workNumber\":" + quote(a.workNumber) + ",\"responder\":" + quote(a.responder) +
         ",\"birthDate\":" + quote(a.birthDate) + ",\"group\":" + quote(a.group) + ",\"production\":" + quote(a.production) +
         ",\"reproduction\":" + quote(a.reproduction) + ",\"reproductionRaw\":" + quote(a.reproductionRaw) +
         ",\"lactation\":" + number(a.lactation) + ",\"daysInMilk\":" + number(a.daysInMilk) + ",\"calvingDate\":" + quote(a.calvingDate) +
         ",\"pregnantDays\":" + number(a.pregnantDays) + ",\"expectedCalving\":" + quote(a.expectedCalving) + ",\"inseminations\":[";
    for (size_t k = 0; k < a.inseminations.size(); k++) {
      const Insemination& n = a.inseminations[k];
      o += std::string(k ? "," : "") + "{\"date\":" + quote(n.date) + ",\"sireName\":" + quote(n.sireName) + ",\"sireCode\":" + quote(n.sireCode) +
           ",\"sireLifeNumber\":" + quote(n.sireLifeNumber) + ",\"type\":" + quote(n.type) + "}";
    }
    o += "],\"pregnancyChecks\":[";
    for (size_t k = 0; k < a.pregnancyChecks.size(); k++)
      o += std::string(k ? "," : "") + "{\"date\":" + quote(a.pregnancyChecks[k].date) + ",\"result\":" + quote(a.pregnancyChecks[k].result) + "}";
    o += "]}";
  }
  return o + "]}";
}

}  // namespace cow
}  // namespace robot

extern "C" {

int32_t cow_read(robot_session* s) {
  if (!s->driver.cow) return ROBOT_UNSUPPORTED;
  return s->read([](robot::Session& session) {
    session.cow = {};
    session.driver.cow(session);
  });
}

int32_t cow_json(robot_session* s, char* buf, int32_t size) { return robot::copyOut(s->cow.json(), buf, size); }

}  // extern "C"
