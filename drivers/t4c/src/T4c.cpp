// The T4C farm management server (Lely Horizon): its milking robots and its herd, read only.
// Checked against a real server (T4C 4.22, two Astronauts, 92 animals, 2026-11-23); see ../API.md.
#include "Robot.h"

#include <map>
#include <memory>

namespace robot {
namespace {

// Two ways in. With a user name: the login of the server's own pages (a token as Bearer).
// Without one, the password is the farm's licence key, which a T4C server takes on every request
// in the header "licensekey" (exactly so: in lower case) without a login of its own.
void login(Session& s) {
  if (s.user.empty()) {
    if (s.password.empty()) return s.fail(ROBOT_LOGIN_REJECTED, "no user name and no licence key");
    s.headers["licensekey"] = s.password;
    return;
  }
  s.post("/t4capi/login/secure", "{\"userName\":" + quote(s.user) + ",\"password\":" + quote(s.password) + "}", [&s](const Json& answer) {
    const Json& user = answer["data"];
    const std::string& token = user["accessToken"]["token"].str();
    if (token.empty()) return s.fail(ROBOT_LOGIN_REJECTED, answer["serverEror"]["message"].str());
    s.headers["Authorization"] = "Bearer " + token;
    // who is asking, as the server's own pages send it along
    static const char* const kWho[][2] = {{"userid", "userId"}, {"herdid", "defaultHerdId"}, {"languageid", "languageId"},
                                          {"languagecode", "languageCode"}, {"countryid", "countryId"}, {"cultureid", "cultureId"}};
    for (const auto& w : kWho)
      if (!user[w[1]].str().empty()) s.headers[w[0]] = user[w[1]].str();
  });
}

// An answer whose envelope reports an error (HTTP 200 with "serverEror": a real server spells it
// so) ends the read: the server refused, for example a licence key it does not know.
bool refused(Session& s, const Json& answer, const char* what) {
  const Json& e = answer["serverEror"];
  const double code = e["code"].num(200);
  if (e.isNull() || code == 200) return false;
  s.fail(code == 401 || code == 403 ? ROBOT_LOGIN_REJECTED : ROBOT_UNEXPECTED, std::string(what) + ": " + e["message"].str());
  return true;
}

void readMilking(Session& s) {
  s.get("/t4capi/astronaut/overviews", [&s](const Json& answer) {
    if (refused(s, answer, "robots")) return;
    for (const Json& o : answer["data"].items) {
      milking::Robot r;
      r.id = o["id"].str();
      r.name = o["name"].str();
      r.group = o["barnName"].str();
      r.raw = o["robotStatus"].str();   // a number whose meaning is not known yet: the state stays "unknown"
      r.lastVisit = o["lastProcessTime"].str();
      s.milking.robots.push_back(r);
    }
    for (size_t i = 0; i < s.milking.robots.size(); i++) {
      const std::string id = s.milking.robots[i].id;
      s.get("/t4capi/alarms/astronaut/" + id, [&s, i](const Json& answer) {
        for (const Json& a : answer["data"].items) {
          milking::Alarm alarm;
          alarm.time = a["creationDate"].str();
          alarm.code = a["indicationId"].str();
          alarm.text = a["description"].str();
          s.milking.robots[i].alarms.push_back(alarm);
        }
      }, true);
      s.get("/t4capi/astronaut/" + id + "/detail", [&s, i](const Json& answer) {
        const Json& detail = answer["data"];
        const Json& kpi = detail["astronautKpis"].at(0);
        milking::Robot& r = s.milking.robots[i];
        r.milkKg = kpi["totalMilkProduced"].num();
        r.milkings = kpi["numberOfMilking"].num();
        r.failedMilkings = detail["failedMilkingKpi"]["totalFailedMilkingCount"].num();
      }, true);
    }
  });
}

// ---- the herd: seven requests whatever its size (Animals/active, the status per 150 animals,
// the farm's number, the sires and four lists of events in the ICAR format the server also offers)

// T4C's ReproductionStatusId (GET /t4capi/reproductionstatustypes on a real server)
const char* reproduction(double id) {
  switch ((int)id) {
    case 1: case 2: case 7: return herd::reproduction::Open;   // open, never inseminated, open (cycling)
    case 3: return herd::reproduction::Inseminated;
    case 4: case 6: case 8: case 9: return herd::reproduction::Pregnant;   // pregnant, auto, "pregnant ?", probably
    case 5: return herd::reproduction::DoNotBreed;
    default: return herd::reproduction::Unknown;
  }
}

const char* production(const std::string& p) {
  if (p == "In Lactation") return herd::production::Milking;
  if (p == "Dry Off") return herd::production::Dry;
  if (p == "Young Stock") return herd::production::Young;
  return herd::production::Unknown;
}

// ICAR (integrationglobal) words
const char* inseminationType(const std::string& t) {
  if (t.find("Natural") != std::string::npos || t == "Sire") return "bull";
  if (t.find("mplant") != std::string::npos || t.find("mbryo") != std::string::npos) return "embryo";
  if (t.find("nsemination") != std::string::npos || t == "AI") return "ai";
  return "unknown";
}

const char* checkResult(const std::string& r) {
  if (r == "Pregnant") return "pregnant";
  if (r == "Empty" || r == "NotPregnant" || r == "Not Pregnant" || r == "Open") return "notPregnant";
  return "unknown";
}

std::string idOf(const Json& identifiers, const char* scheme) {
  for (const Json& i : identifiers.items)
    if (i["scheme"].str() == scheme) return i["id"].str();
  return "";
}

struct HerdRead {
  std::map<std::string, size_t> byId, byLife;                 // the server's animal id / life number -> index
  std::map<std::string, std::string> sireByCode, sireByLife;  // -> the sire's name
  std::string ucn;
};

herd::Animal* animal(Session& s, HerdRead& h, const Json& icarAnimal) {
  auto it = h.byLife.find(icarAnimal["id"].str());
  return it == h.byLife.end() ? nullptr : &s.herd.animals[it->second];
}

void readHerd(Session& s) {
  auto h = std::make_shared<HerdRead>();
  s.get("/t4capi/Animals/active", [&s, h](const Json& answer) {
    if (refused(s, answer, "animals")) return;
    for (const Json& a : answer["data"].items) {
      herd::Animal x;
      x.id = a["id"].str();
      x.number = a["userNumber"].str();
      x.name = a["name"].str();
      x.lifeNumber = a["lifeNumber"].str();
      x.responder = a["responderNumber"].str();
      x.birthDate = herd::day(a["birthDate"].str());
      x.group = a["location"].str();
      h->byId[x.id] = s.herd.animals.size();
      h->byLife[x.lifeNumber] = s.herd.animals.size();
      s.herd.animals.push_back(x);
    }
    for (size_t from = 0; from < s.herd.animals.size(); from += 150) {   // as the server's own pages ask
      std::string q;
      for (size_t i = from; i < s.herd.animals.size() && i < from + 150; i++) q += (q.empty() ? "?" : "&") + std::string("animalIds=") + s.herd.animals[i].id;
      s.get("/t4capi/Animal/status" + q, [&s, h](const Json& answer) {
        for (const Json& st : answer["data"].items) {
          auto it = h->byId.find(st["animalId"].str());
          if (it == h->byId.end()) continue;
          herd::Animal& x = s.herd.animals[it->second];
          x.reproduction = reproduction(st["reproductionStatusId"].num(0));
          x.reproductionRaw = st["reproductionStatus"].str();
          x.production = production(st["productionStatus"].str());
          x.lactation = st["lactationNumber"].num();
          x.daysInMilk = x.production == std::string(herd::production::Young) ? -1 : st["daysInLactation"].num();
          const double preg = st["pregnancyDays"].num();
          x.pregnantDays = x.reproduction == std::string(herd::reproduction::Pregnant) && preg > 0 ? preg : -1;
        }
      });
    }
    s.get("/t4capi/v2/herds", [&s, h](const Json& answer) {
      for (const Json& row : answer["data"].items)
        if (h->ucn.empty()) h->ucn = row["ucnNumber"].str();
      if (h->ucn.empty()) return s.fail(ROBOT_UNEXPECTED, "herds: no UCN number for the events");
      s.get("/t4capi/v2/sires", [&s, h](const Json& answer) {
        for (const Json& sire : answer["data"].items) {
          if (!sire["sireCode"].str().empty()) h->sireByCode[sire["sireCode"].str()] = sire["sireName"].str();
          if (!sire["lifeNumber"].str().empty()) h->sireByLife[sire["lifeNumber"].str()] = sire["sireName"].str();
        }
      }, true);
      const std::string farm = "?ucnNumber=" + h->ucn;
      s.get("/t4capi/integrationglobal/parturitions" + farm, [&s, h](const Json& answer) {
        for (const Json& e : answer["data"].items)
          if (herd::Animal* x = animal(s, *h, e["animal"])) {
            const std::string d = herd::day(e["eventDateTime"].str());
            if (d > x->calvingDate) x->calvingDate = d;
          }
      }, true);
      s.get("/t4capi/integrationglobal/inseminations" + farm, [&s, h](const Json& answer) {
        for (const Json& e : answer["data"].items)
          if (herd::Animal* x = animal(s, *h, e["animal"])) {
            herd::Insemination n;
            n.date = herd::day(e["eventDateTime"].str());
            n.sireCode = idOf(e["sireIdentifiers"], "nl.aicode");
            n.sireLifeNumber = idOf(e["sireIdentifiers"], "eu.bovine");
            n.sireName = h->sireByCode.count(n.sireCode) ? h->sireByCode[n.sireCode] : h->sireByLife[n.sireLifeNumber];
            n.type = inseminationType(e["inseminationType"].str());
            x->inseminations.push_back(n);
          }
      }, true);
      s.get("/t4capi/integrationglobal/pregnancy-checks" + farm, [&s, h](const Json& answer) {
        for (const Json& e : answer["data"].items)
          if (herd::Animal* x = animal(s, *h, e["animal"])) x->pregnancyChecks.push_back({herd::day(e["eventDateTime"].str()), checkResult(e["result"].str())});
      }, true);
      s.get("/t4capi/integrationglobal/gestations" + farm, [&s, h](const Json& answer) {
        for (const Json& e : answer["data"].items)
          if (herd::Animal* x = animal(s, *h, e["animal"]))
            if (x->reproduction == std::string(herd::reproduction::Pregnant)) x->expectedCalving = herd::day(e["expectedCalvingDate"].str());
      }, true);
    });
  });
}

}  // namespace

extern const Driver t4c = {"t4c", "T4C", "http", 80, login, readMilking, readHerd};

}  // namespace robot
