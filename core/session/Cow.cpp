// The herd API (core/cow_c.h) on a session.
#include "cow_c.h"

#include <algorithm>

#include "Session.h"

namespace agrigate {
namespace cow {

std::string day(const std::string& when) {
  const int64_t t = timeMs(when);
  if (!t) return "";
  return when.back() == 'Z' ? timeText(t + 12 * 3600000LL).substr(0, 10) : when.substr(0, 10);   // UTC from noon on: the next day
}

std::string Herd::json() const {
  std::string o = "{\"updated\":" + timeJson(updated) + ",\"animals\":[";
  for (size_t i = 0; i < animals.size(); i++) {
    Animal a = animals[i];   // the events oldest first, whatever order the server gave them in
    std::stable_sort(a.inseminations.begin(), a.inseminations.end(), [](const Insemination& x, const Insemination& y) { return x.date < y.date; });
    std::stable_sort(a.pregnancyChecks.begin(), a.pregnancyChecks.end(), [](const PregnancyCheck& x, const PregnancyCheck& y) { return x.date < y.date; });
    std::stable_sort(a.heats.begin(), a.heats.end(), [](const Heat& x, const Heat& y) { return x.date < y.date; });
    // "Do not breed" holds until a calving or an insemination after it, whatever status the server kept
    if (a.doNotBreedDate < a.calvingDate || (!a.inseminations.empty() && a.doNotBreedDate < a.inseminations.back().date)) a.doNotBreedDate.clear();
    if (!a.doNotBreedDate.empty() && a.reproduction != std::string(reproduction::Pregnant)) a.reproduction = reproduction::DoNotBreed;
    o += std::string(i ? "," : "") + "{\"id\":" + quote(a.id) + ",\"number\":" + quote(a.number) + ",\"name\":" + quote(a.name) +
         ",\"lifeNumber\":" + quote(a.lifeNumber) + ",\"workNumber\":" + quote(a.workNumber) + ",\"responder\":" + quote(a.responder) +
         ",\"birthDate\":" + quote(a.birthDate) + ",\"group\":" + quote(a.group) + ",\"production\":" + quote(a.production) +
         ",\"reproduction\":" + quote(a.reproduction) + ",\"reproductionRaw\":" + quote(a.reproductionRaw) +
         ",\"lactation\":" + number(a.lactation) + ",\"daysInMilk\":" + number(a.daysInMilk) + ",\"calvingDate\":" + quote(a.calvingDate) +
         ",\"pregnantDays\":" + number(a.pregnantDays) + ",\"expectedCalving\":" + quote(a.expectedCalving) +
         ",\"dryOffDate\":" + quote(a.dryOffDate) + ",\"daysSinceInsemination\":" + number(a.daysSinceInsemination) +
         ",\"daysSinceHeat\":" + number(a.daysSinceHeat) + ",\"inseminationsThisLactation\":" + number(a.inseminationsThisLactation) +
         ",\"doNotBreedDate\":" + quote(a.doNotBreedDate) + ",\"inseminations\":[";
    for (size_t k = 0; k < a.inseminations.size(); k++) {
      const Insemination& n = a.inseminations[k];
      o += std::string(k ? "," : "") + "{\"date\":" + quote(n.date) + ",\"sireName\":" + quote(n.sireName) + ",\"sireCode\":" + quote(n.sireCode) +
           ",\"sireLifeNumber\":" + quote(n.sireLifeNumber) + ",\"type\":" + quote(n.type) + "}";
    }
    o += "],\"pregnancyChecks\":[";
    for (size_t k = 0; k < a.pregnancyChecks.size(); k++)
      o += std::string(k ? "," : "") + "{\"date\":" + quote(a.pregnancyChecks[k].date) + ",\"result\":" + quote(a.pregnancyChecks[k].result) + "}";
    o += "],\"heats\":[";
    for (size_t k = 0; k < a.heats.size(); k++)
      o += std::string(k ? "," : "") + "{\"date\":" + quote(a.heats[k].date) + ",\"method\":" + quote(a.heats[k].method) + "}";
    o += "]}";
  }
  return o + "]}";
}

}  // namespace cow
}  // namespace agrigate

extern "C" {

int32_t cow_read(session* s) {
  if (!s->driver.cow) return SESSION_UNSUPPORTED;
  return s->read([](agrigate::Session& session) {
    session.cow.animals.clear();
    session.driver.cow(session);
  }, &s->cow.updated);
}

int32_t cow_json(session* s, char* buf, int32_t size) { return agrigate::copyOut(s->cow.json(), buf, size); }

}  // extern "C"
