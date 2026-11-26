// The T4C farm management server: its milking robots and its herd, read only.
// Checked against a real server (T4C 4.22, two Astronauts, 92 animals, 2026-11-23); see ../API.md.
#include "Icar.h"

namespace agrigate {
namespace {

// Two ways in. With a user name: the login of the server's own pages (a token as Bearer).
// Without one, the password is the farm's licence key, which a T4C server takes on every request
// in the header "licensekey" (exactly so: in lower case) without a login of its own.
void login(Session& s) {
  if (s.user.empty()) {
    if (s.password.empty()) return s.fail(SESSION_LOGIN_REJECTED, "no user name and no licence key");
    s.headers["licensekey"] = s.password;
    return;
  }
  s.post("/t4capi/login/secure", "{\"userName\":" + quote(s.user) + ",\"password\":" + quote(s.password) + "}", [&s](const Json& answer) {
    const Json& user = answer["data"];
    const std::string& token = user["accessToken"]["token"].str();
    if (token.empty()) return s.fail(SESSION_LOGIN_REJECTED, answer["serverEror"]["message"].str());
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
  s.fail(code == 401 || code == 403 ? SESSION_LOGIN_REJECTED : SESSION_UNEXPECTED, std::string(what) + ": " + e["message"].str());
  return true;
}

void readMilk(Session& s) {
  s.get("/t4capi/astronaut/overviews", [&s](const Json& answer) {
    if (refused(s, answer, "robots")) return;
    for (const Json& o : answer["data"].items) {
      milk::Robot r;
      r.id = o["id"].str();
      r.name = o["name"].str();
      r.group = o["barnName"].str();
      r.raw = o["robotStatus"].str();   // a number whose meaning is not known yet: the state stays "unknown"
      r.lastVisit = o["lastProcessTime"].str();
      s.milk.robots.push_back(r);
    }
    for (size_t i = 0; i < s.milk.robots.size(); i++) {
      const std::string id = s.milk.robots[i].id;
      s.get("/t4capi/alarms/astronaut/" + id, [&s, i](const Json& answer) {
        for (const Json& a : answer["data"].items) {
          milk::Alarm alarm;
          alarm.time = a["creationDate"].str();
          alarm.code = a["indicationId"].str();
          alarm.text = a["description"].str();
          s.milk.robots[i].alarms.push_back(alarm);
        }
      }, true);
      s.get("/t4capi/astronaut/" + id + "/detail", [&s, i](const Json& answer) {
        const Json& detail = answer["data"];
        const Json& kpi = detail["astronautKpis"].at(0);
        milk::Robot& r = s.milk.robots[i];
        r.milkKg = kpi["totalMilkProduced"].num();
        r.milkings = kpi["numberOfMilking"].num();
        r.failedMilkings = detail["failedMilkingKpi"]["totalFailedMilkingCount"].num();
      }, true);
    }
  });
}

// ---- identity: the last cow each robot dealt with (astronaut/overviews: AniUserNumber at
// LastProcessTime, the farm's own time without a zone). A new one is reported when it is seen, at
// the session's time; what a robot had at the first read is only noted (it may be hours old).
void readIdentity(Session& s) {
  s.get("/t4capi/astronaut/overviews", [&s](const Json& answer) {
    if (refused(s, answer, "robots")) return;
    for (const Json& o : answer["data"].items) {
      identity::Place& p = s.identity.place(o["id"].str(), identity::kind::MilkingRobot);
      p.name = o["name"].str();
      const std::string number = o["aniUserNumber"].str(), seen = number + " " + o["lastProcessTime"].str();
      const bool first = p.seen.empty();
      if (seen == p.seen) continue;
      p.seen = seen;
      if (!first && !number.empty() && number != "0") s.identity.identified(p, {number, "", ""}, s.now);
    }
  });
}

// ---- the herd: Animals/active, their status (one request per 150 animals), the farm's number,
// the sires and the lists of events in the ICAR format (Icar.h) the server also offers

// T4C's ReproductionStatusId (GET /t4capi/reproductionstatustypes on a real server)
const char* reproduction(double id) {
  switch ((int)id) {
    case 1: case 2: case 7: return cow::reproduction::Open;   // open, never inseminated, open (cycling)
    case 3: return cow::reproduction::Inseminated;
    case 4: case 6: case 8: case 9: return cow::reproduction::Pregnant;   // pregnant, auto, "pregnant ?", probably
    case 5: return cow::reproduction::DoNotBreed;
    default: return cow::reproduction::Unknown;
  }
}

const char* production(const std::string& p) {
  if (p == "In Lactation") return cow::production::Milking;
  if (p == "Dry Off") return cow::production::Dry;
  if (p == "Young Stock") return cow::production::Young;
  return cow::production::Unknown;
}

void readCow(Session& s) {
  auto h = std::make_shared<icar::Herd>();
  s.get("/t4capi/Animals/active", [&s, h](const Json& answer) {
    if (refused(s, answer, "animals")) return;
    for (const Json& a : answer["data"].items) {
      cow::Animal x;
      x.id = a["id"].str();
      x.number = a["userNumber"].str();
      x.name = a["name"].str();
      x.lifeNumber = a["lifeNumber"].str();
      x.responder = a["responderNumber"].str();
      x.birthDate = cow::day(a["birthDate"].str());
      x.group = a["location"].str();
      h->byId[x.lifeNumber] = s.cow.animals.size();   // the ICAR lists name an animal by its life number
      s.cow.animals.push_back(x);
    }
    for (size_t from = 0; from < s.cow.animals.size(); from += 150) {   // as the server's own pages ask
      std::string q;
      for (size_t i = from; i < s.cow.animals.size() && i < from + 150; i++) q += (q.empty() ? "?" : "&") + std::string("animalIds=") + s.cow.animals[i].id;
      s.get("/t4capi/Animal/status" + q, [&s](const Json& answer) {
        for (const Json& st : answer["data"].items)
          for (cow::Animal& x : s.cow.animals) {
            if (x.id != st["animalId"].str()) continue;
            x.reproduction = reproduction(st["reproductionStatusId"].num(0));
            x.reproductionRaw = st["reproductionStatus"].str();
            x.production = production(st["productionStatus"].str());
            x.lactation = st["lactationNumber"].num();
            x.daysInMilk = x.production == std::string(cow::production::Young) ? -1 : st["daysInLactation"].num();
            const double preg = st["pregnancyDays"].num();
            x.pregnantDays = x.reproduction == std::string(cow::reproduction::Pregnant) && preg > 0 ? preg : -1;
            x.daysSinceInsemination = st["daysSinceInsemination"].num();
            x.daysSinceHeat = st["daysSinceHeat"].num();
            x.inseminationsThisLactation = st["numberOfInseminations"].num();
          }
      });
    }
    s.get("/t4capi/v2/herds", [&s, h](const Json& answer) {
      std::string ucn;
      for (const Json& row : answer["data"].items)
        if (ucn.empty()) ucn = row["ucnNumber"].str();
      if (ucn.empty()) return s.fail(SESSION_UNEXPECTED, "herds: no UCN number for the events");
      s.get("/t4capi/v2/sires", [h](const Json& answer) {
        for (const Json& sire : answer["data"].items) {
          if (!sire["sireCode"].str().empty()) h->sireByCode[sire["sireCode"].str()] = sire["sireName"].str();
          if (!sire["lifeNumber"].str().empty()) h->sireByLife[sire["lifeNumber"].str()] = sire["sireName"].str();
        }
      }, true);
      const std::string path = "/t4capi/integrationglobal/", farm = "?ucnNumber=" + ucn;
      icar::readEvents(s, h, path, farm);
      s.get(path + "gestations" + farm, [&s, h](const Json& answer) {   // T4C's own list
        for (const Json& e : answer["data"].items)
          if (cow::Animal* x = h->animal(s, e["animal"]))
            if (x->reproduction == std::string(cow::reproduction::Pregnant)) x->expectedCalving = cow::day(e["expectedCalvingDate"].str());
      }, true);
    });
  });
}

}  // namespace

extern const Driver t4c = [] {
  Driver d{"t4c", "T4C", {{"http", "http", 6969}}};
  d.login = login;
  d.milk = readMilk;
  d.cow = readCow;
  d.identity = readIdentity;
  return d;
}();

}  // namespace agrigate
