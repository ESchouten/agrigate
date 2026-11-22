// The T4C farm management server: its milking robots, read only.
// Written from the server's own API description; see ../API.md for what a real server still
// has to confirm.
#include "Robot.h"

namespace robot {
namespace {

void login(Session& s) {
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

void readMilking(Session& s) {
  s.get("/t4capi/astronaut/overviews", [&s](const Json& answer) {
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

}  // namespace

extern const Driver t4c = {"t4c", "T4C", "http", 80, login, readMilking};

}  // namespace robot
