// The ICAR event lists (Icar.h).
#include "Icar.h"

namespace agrigate {
namespace icar {
namespace {

const char* inseminationType(const std::string& t) {
  if (t.find("Natural") != std::string::npos || t == "Sire") return "bull";
  if (t.find("mplant") != std::string::npos || t.find("mbryo") != std::string::npos) return "embryo";
  if (t.find("nsemination") != std::string::npos || t == "AI") return "ai";
  return "unknown";
}

const char* checkResult(const std::string& r) {
  if (r == "Pregnant" || r == "Multiple") return "pregnant";
  if (r == "Empty" || r == "NotPregnant" || r == "Not Pregnant" || r == "Open") return "notPregnant";
  return "unknown";
}

const char* heatMethod(const std::string& m) {
  if (m == "Visual") return "visual";
  if (m.find("ctivity") != std::string::npos || m.find("edometer") != std::string::npos || m.find("ensor") != std::string::npos) return "activity";
  return "unknown";
}

std::string found(const std::map<std::string, std::string>& in, const std::string& key) {
  auto it = in.find(key);
  return it == in.end() ? "" : it->second;
}

void latest(std::string& date, const Json& e) {
  const std::string d = cow::day(e["eventDateTime"].str());
  if (d > date) date = d;
}

}  // namespace

cow::Animal* Herd::animal(Session& s, const Json& identifier) const {
  auto it = byId.find(identifier["id"].str());
  return it == byId.end() ? nullptr : &s.cow.animals[it->second];
}

const Json& items(const Json& list) { return list["member"].type == Json::Array ? list["member"] : list["data"]; }

std::string idOf(const Json& identifiers, const char* scheme) {
  for (const Json& i : identifiers.items)
    if (i["scheme"].str() == scheme) return i["id"].str();
  return "";
}

void readEvents(Session& s, std::shared_ptr<Herd> h, const std::string& path, const std::string& query) {
  // each list: what an event says about its animal
  using Read = void (*)(const Herd&, cow::Animal&, const Json&);
  static const std::pair<const char*, Read> kLists[] = {
      {"parturitions", [](const Herd&, cow::Animal& x, const Json& e) { latest(x.calvingDate, e); }},
      {"inseminations", [](const Herd& h, cow::Animal& x, const Json& e) {
         cow::Insemination n;
         n.date = cow::day(e["eventDateTime"].str());
         n.sireCode = idOf(e["sireIdentifiers"], "nl.aicode");
         n.sireLifeNumber = idOf(e["sireIdentifiers"], "eu.bovine");
         n.sireName = found(h.sireByCode, n.sireCode);
         if (n.sireName.empty()) n.sireName = found(h.sireByLife, n.sireLifeNumber);
         n.type = inseminationType(e["inseminationType"].str());
         x.inseminations.push_back(n);
       }},
      {"pregnancy-checks", [](const Herd&, cow::Animal& x, const Json& e) {
         x.pregnancyChecks.push_back({cow::day(e["eventDateTime"].str()), checkResult(e["result"].str())});
       }},
      {"heats", [](const Herd&, cow::Animal& x, const Json& e) {
         x.heats.push_back({cow::day(e["eventDateTime"].str()), heatMethod(e["heatDetectionMethod"].str())});
       }},
      {"drying-offs", [](const Herd&, cow::Animal& x, const Json& e) { latest(x.dryOffDate, e); }},
      // T4C calls it "keep open", and sets the status back to open after a heat while the event stays
      {"do-not-breeds", [](const Herd&, cow::Animal& x, const Json& e) {
         if (e["doNotBreed"].str() != "false") latest(x.doNotBreedDate, e);
       }},
  };
  for (const auto& list : kLists) {
    const Read read = list.second;
    s.get(path + list.first + query, [&s, h, read](const Json& answer) {
      for (const Json& e : items(answer).items)
        if (cow::Animal* x = h->animal(s, e["animal"])) read(*h, *x, e);
    }, true);
  }
}

}  // namespace icar
}  // namespace agrigate
