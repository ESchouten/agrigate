// The FULLSENSE farm management server: its milking robots, which cow is in them, and its herd,
// read only. The robots and the cows in them from the server's own API (/api), the herd and the
// milkings from its ICAR lists (/ICAR, Icar.h). Written from the server's API description; see
// ../API.md for what a real server still has to confirm.
#include <cstdlib>
#include <random>

#include "Icar.h"

namespace agrigate {
namespace {

// The server's lists, in its own order: an answer gives the number (or, as some servers are
// set up, the name itself).
const char* const kDisplay[] = {"", "Emergency", "Alarm", "WaitingForCow", "Milking", "WaitingForAssistance",
                                "Cleaning", "Paused", "Offline", "Idle", "Releasing"};
const char* const kAlarmClass[] = {"Info", "Warning", "Error", "Fatal", "WaitForAssistance", "Communicator"};
const char* const kAlarm[] = {
    "NoAlarm", "CowInBox", "FeedMotor", "FeedSupply", "VacuumGenerator", "VacuumOil", "VacuumPump", "VacuumSensor",
    "ArmGeneral", "ArmNoCommunication", "ArmWrongCommand", "ArmCleaning", "ArmCleanCups", "ArmFreeVacuum",
    "ArmDrainHoses", "ArmStandBy", "ArmAssistRequest", "ArmTurnIn", "ArmTurnOut", "ArmVacuum", "ArmWagonPosition",
    "ArmTeatcupPosition", "ArmNoTeats", "MilkmeterGeneral", "MilkmeterCommunication", "MilkmeterSensor",
    "CleanLowTemperature", "CleanChemicals", "MilkPump", "MilkingAborted", "ConnectionFailure", "PowerFailure",
    "MilkingPause", "SamplerFrameFull", "RetractableTeat", "CalfFeeder", "DLineTimeout", "MilkingTimeout",
    "CleaningTimeout", "Spare_2", "MangerSensor", "LiquidFilling", "ArmMilkingTimeout", "ArmAssistTimeout",
    "ArmFindUdderError", "ArmFindTeatsError", "ArmNoFlowLR", "ArmNoFlowRR", "ArmNoFlowRF", "ArmNoFlowLF", "UserAbort",
    "CommunicationAbort", "SearchTimeout", "Catching", "Releasing", "EatingSensor", "NoColdwater",
    "EndOfMilkingAlarm", "BloodSensor", "AbnormalMilk", "BucketsFull", "BucketsError", "Identification",
    "DatabaseError", "AirPressure", "LightningProtection", "NoCowTimeout", "Crystalab", "MilkpumpInverter",
    "RejectionValve", "TankValve", "DLineValve", "FlowVacuumSensors", "Pulsator", "CowFollowerCommunication",
    "CowFollowerError", "CowFollowerNoCow", "ArmPlcCommunication", "SystemCommunication", "ArmConfiguration",
    "UnderOverVoltage", "HmiStartup", "FlashError", "ArmFlowSensorLR", "ArmFlowSensorRR", "ArmFlowSensorRF",
    "ArmFlowSensorLF", "ArmPlcVersion", "TdcCommunication", "TdcError", "HmiUwfDisabled", "CleanAlarmNoWater",
    "CleanAlarmLowTemperature", "CleanAlarmLowChemical", "CleanAlarmHighRinseConductivity", "MilkingSlugsize",
    "Fullquest", "DriveDirty", "", "MilkpumpSpeedLow", "MerlinCommunication", "FullCountFarmCanCommunication",
    "FullCountNoReagent", "FullCountNoMilkInChamber", "FullCountShuttleStuck", "FullCountBlockage",
    "FullCountMilkCanCommunication", "ArmPlcNotHomedOrCalibrated", "ArmPlcDrivesUpdating", "CIPValveError",
    "MoistureTrapAlarm", "RejectionValve2", "CleaningRequired", "MerlinNotCleaning",
};

template <size_t N>
std::string name(const Json& value, const char* const (&names)[N]) {
  if (value.type != Json::Number) return value.str();
  int i = (int)value.num();
  return i >= 0 && i < (int)N ? names[i] : "";
}

/** "VacuumPump" -> "Vacuum pump": the server has no text for an alarm, only this name. */
std::string words(const std::string& name) {
  std::string o;
  for (size_t i = 0; i < name.size(); i++) {
    bool upper = name[i] >= 'A' && name[i] <= 'Z';
    if (upper && i && !(name[i - 1] >= 'A' && name[i - 1] <= 'Z')) o += ' ';
    o += upper && i ? (char)(name[i] + 32) : name[i];
  }
  return o;
}

const char* state(const std::string& display) {
  using namespace milk::state;
  static const char* const kStates[][2] = {
      {"Emergency", Emergency}, {"Alarm", Alarm}, {"WaitingForCow", Idle}, {"Idle", Idle}, {"Milking", Milking},
      {"Releasing", Milking},   // the cow is still in the box
      {"WaitingForAssistance", Assistance}, {"Cleaning", Cleaning}, {"Paused", Paused}, {"Offline", Offline}};
  for (const auto& s : kStates)
    if (display == s[0]) return s[1];
  return Unknown;
}

const char* severity(const std::string& alarmClass) {
  if (alarmClass == "Info") return "info";
  if (alarmClass == "Warning" || alarmClass == "WaitForAssistance") return "warning";
  if (alarmClass == "Error" || alarmClass == "Fatal") return "error";
  return "unknown";
}

// MerlinDecisionCode: what the robot does with the cow in it
const char* const kDecision[] = {"Milking", "Visit", "NotAllowed", "Timeout"};

bool given(const std::string& id) { return !id.empty() && id != "00000000-0000-0000-0000-000000000000"; }

constexpr int64_t kDayMs = 24 * 3600 * 1000LL;

std::string credentials(const std::string& user, const std::string& password) {   // the "}" still to come
  return "{\"userName\":" + quote(user) + ",\"password\":" + quote(password);
}

void login(Session& s) {
  s.post("/api/Authentication/Authenticate", credentials(s.user, s.password) + "}", [&s](const Json& answer) {
    if (!answer["token"].str().empty()) s.headers["Authorization"] = "Bearer " + answer["token"].str();
  });
}

// ---- ICAR: the server's lists are per farm ("location"): the first one it lists
void withFarm(Session& s, std::function<void(const std::string& path)> then, bool optional) {
  s.get("/ICAR/locations", [&s, then, optional](const Json& answer) {
    const Json& id = icar::items(answer).at(0)["identifier"];
    if (!id["id"].str().empty()) return then("/ICAR/locations/" + id["scheme"].str() + "/" + id["id"].str() + "/");
    if (!optional) s.fail(SESSION_UNEXPECTED, "ICAR: the server lists no farm");
  }, optional);
}

// The milkings of the last 24 hours, page by page: the figures per robot (its box number).
void readMilkings(Session& s, const std::string& farm, std::shared_ptr<std::map<int, size_t>> boxes, int page) {
  const int64_t from = s.now - kDayMs;
  s.get(farm + "milking-visits?pageSize=100&currentPage=" + std::to_string(page) + "&meta-modified-from=" + timeText(from),
        [&s, farm, boxes, page, from](const Json& answer) {
    if (page == 1)
      for (milk::Robot& r : s.milk.robots) r.milkKg = r.milkings = r.failedMilkings = 0;
    for (const Json& v : icar::items(answer).items) {
      auto it = boxes->find(atoi(v["milkingBoxNumber"].str().c_str()));
      const std::string& start = v["milkingStartingDateTime"].str();
      if (it == boxes->end() || timeMs(start) < from) continue;   // changed lately, milked before
      milk::Robot& r = s.milk.robots[it->second];
      (v["milkingComplete"].str() == "true" ? r.milkings : r.failedMilkings) += 1;
      const Json& kg = v["milkingMilkWeight"];
      r.milkKg += kg["value"].num(0) / (kg["unitCode"].str() == "GRM" ? 1000 : 1);
      if (timeMs(start) > timeMs(r.lastVisit)) r.lastVisit = start;
    }
    if (answer["view"]["currentPage"].num(0) < answer["view"]["totalPages"].num(0) && page < 100) readMilkings(s, farm, boxes, page + 1);
  }, true);
}

void readMilk(Session& s) {
  s.get("/api/Location/GetLocations?locationType=1", [&s](const Json& locations) {   // 1: the milking robots
    auto boxes = std::make_shared<std::map<int, size_t>>();
    for (const Json& l : locations.items) {
      milk::Robot r;
      r.id = l["id"].str();
      r.name = l["description"].str();
      r.group = l["locationGroup"]["name"].str();
      size_t i = s.milk.robots.size();
      (*boxes)[(int)l["locationIndex"].num()] = i;
      s.milk.robots.push_back(r);
      if (given(l["currentStatusRecordingId"].str()))
        s.get("/api/MerlinStatus/GetMerlinStatus?merlinStatusId=" + l["currentStatusRecordingId"].str(), [&s, i](const Json& status) {
          milk::Robot& robot = s.milk.robots[i];
          robot.raw = name(status["displayStatus"], kDisplay);
          robot.state = state(robot.raw);
          robot.since = status["start"].str();
        }, true);
      if (given(l["currentMerlinAlarmId"].str()))
        s.get("/api/MerlinAlarm/GetMerlinAlarm?merlinAlarmId=" + l["currentMerlinAlarmId"].str(), [&s, i](const Json& a) {
          milk::Alarm alarm;
          alarm.time = a["timestamp"].str();
          alarm.severity = severity(name(a["alarmClass"], kAlarmClass));
          alarm.code = name(a["alarmCode"], kAlarm);
          alarm.text = words(alarm.code);
          if (alarm.code.empty()) alarm.code = a["alarmCode"].str();   // a code newer than this list
          if (alarm.code != "NoAlarm") s.milk.robots[i].alarms.push_back(alarm);
        }, true);
    }
    if (s.now) withFarm(s, [&s, boxes](const std::string& farm) { readMilkings(s, farm, boxes, 1); }, true);
  });
}

// ---- identity: the cow a robot has now, from its current milking (milk recording)
void readIdentity(Session& s) {
  s.get("/api/Location/GetLocations?locationType=1", [&s](const Json& locations) {
    for (const Json& l : locations.items) {
      const std::string id = l["id"].str(), milking = l["currentMilkRecordingId"].str();
      identity::Place& p = s.identity.place(id, identity::kind::MilkingRobot);
      p.name = l["description"].str();
      if (!given(milking)) {
        if (p.visitSince) s.identity.visitEnded(p, s.now);
        continue;
      }
      s.get("/api/Milking/GetMilkRecording?id=" + milking, [&s, id](const Json& m) {
        identity::Place& p = s.identity.place(id, identity::kind::MilkingRobot);
        const Json& cow = m["animal"];
        const identity::Animal a{cow["animalNumber"].str(), cow["name"].str(), cow["tags"].at(0)["tagNumber"].str()};
        int64_t start = timeMs(m["milkingStartTime"].str());
        if (!start) start = s.now;
        const std::string decision = name(m["merlinDecision"], kDecision);
        const bool finished = m["isFinished"].str() == "true";
        const bool fresh = !p.occupied || p.since != start || p.animal.number != a.number;   // not seen at the last read
        if (fresh) s.identity.identified(p, a, start);
        if (fresh && decision == "NotAllowed") s.identity.refused(p, a.responder, start);
        if (decision == "Milking" && (fresh || (!finished && p.visitSince != start))) s.identity.visitStarted(p, start);
        if (finished && p.visitSince == start) s.identity.visitEnded(p, s.now);
        s.identity.settle(s.now);
      }, true);
    }
    s.identity.settle(s.now);
  });
}

// ---- the herd, from ICAR: the animals, their events and their lactations
const char* production(const std::string& lactation) {
  if (lactation == "Lactating" || lactation == "Fresh") return cow::production::Milking;
  if (lactation == "Dry") return cow::production::Dry;
  return cow::production::Unknown;
}

const char* reproduction(const std::string& r) {
  if (r == "Open" || r == "NotPregnant" || r == "Birthed") return cow::reproduction::Open;
  if (r == "Inseminated") return cow::reproduction::Inseminated;
  if (r == "Pregnant" || r == "PregnantMultipleFoetus") return cow::reproduction::Pregnant;
  if (r == "DoNotBreed") return cow::reproduction::DoNotBreed;
  return cow::reproduction::Unknown;
}

// What the server does not count itself, counted from the events on the day of the read (UTC).
void count(cow::Animal& x, int64_t now) {
  const int64_t today = timeMs(timeText(now).substr(0, 10));
  auto days = [today](const std::string& date) { return date.empty() ? -1.0 : (double)((today - timeMs(date)) / kDayMs); };
  std::string insemination, heat;
  double sinceCalving = 0;
  for (const auto& n : x.inseminations) {
    if (n.date > insemination) insemination = n.date;
    sinceCalving += n.date > x.calvingDate;
  }
  for (const auto& h : x.heats)
    if (h.date > heat) heat = h.date;
  x.daysSinceInsemination = days(insemination);
  x.daysSinceHeat = days(heat);
  if (x.reproduction == std::string(cow::reproduction::Pregnant)) x.pregnantDays = days(insemination);
  if (x.production == std::string(cow::production::Milking)) {
    x.daysInMilk = days(x.calvingDate);
    x.inseminationsThisLactation = sinceCalving;
  }
}

void readCow(Session& s) {
  withFarm(s, [&s](const std::string& farm) {
    auto h = std::make_shared<icar::Herd>();
    s.get(farm + "animals", [&s, h, farm](const Json& answer) {
      for (const Json& a : icar::items(answer).items) {
        if (a["status"].str() == "Dead" || a["status"].str() == "OffFarm") continue;
        cow::Animal x;
        x.id = a["identifier"]["id"].str();
        x.number = a["managementTag"].str();
        x.name = a["name"].str();
        x.lifeNumber = icar::idOf(a["alternativeIdentifiers"], "eu.bovine");
        x.birthDate = cow::day(a["birthDate"].str());
        x.production = production(a["lactationStatus"].str());
        x.reproductionRaw = a["reproductionStatus"].str();
        x.reproduction = reproduction(x.reproductionRaw);
        h->byId[x.id] = s.cow.animals.size();
        s.cow.animals.push_back(x);
      }
      icar::readEvents(s, h, farm, "");
      s.get(farm + "lactations", [&s, h](const Json& answer) {   // asked last: the herd is complete then
        for (const Json& l : icar::items(answer).items)
          if (cow::Animal* x = h->animal(s, l["animal"])) {
            const std::string begin = cow::day(l["beginDate"].str());
            if (l["parity"].num() > x->lactation) x->lactation = l["parity"].num();
            if (begin > x->calvingDate) x->calvingDate = begin;
          }
        for (cow::Animal& x : s.cow.animals) {
          if (x.lactation < 0) x.lactation = 0;
          if (!x.lactation && x.production != std::string(cow::production::Milking)) x.production = cow::production::Young;
          if (s.now) count(x, s.now);
        }
      }, true);
    });
  }, false);
}

// ---- setup: the program user (FULLSENSE's user management, type "Programma") Agrigate reads with.
// Made with a password used once, then set to the user's own: as a new user would on first login.

void setup(Session& s) {
  s.get("/api/Settings/GetRoles", [&s](const Json& roles) {
    std::string role, all;
    for (const Json& r : roles.items) {
      std::string lower = r.str();
      for (char& c : lower) c = (char)tolower((unsigned char)c);
      if (role.empty() && (lower.find("program") != std::string::npos || lower.find("applicat") != std::string::npos)) role = r.str();
      all += (all.empty() ? "" : ", ") + r.str();
    }
    if (role.empty()) return s.fail(SESSION_UNEXPECTED, "roles: none for a program (" + all + ")");
    s.get("/api/Settings/GetUsers", [&s, role](const Json& users) {
      std::string id;   // a user of that name already: it gets the new password
      for (const Json& u : users.items)
        if (u["userName"].str() == s.setupUser) id = u["id"].str();
      std::random_device random;
      std::string once = "Ag-";
      for (int i = 0; i < 16; i++) once += "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789"[random() % 56];
      once += "-7a";
      s.post("/api/Settings/SaveUser", "{" + (id.empty() ? "" : "\"id\":" + quote(id) + ",") + "\"isActive\":true,\"firstName\":" +
                 quote(s.setupUser) + ",\"lastName\":\"\",\"userName\":" + quote(s.setupUser) + ",\"role\":" + quote(role) +
                 ",\"oneTimePassword\":" + quote(once) + "}", [&s, once](const Json&) {
        s.post("/api/Authentication/ChangeUserPassword", credentials(s.setupUser, once) + ",\"newPassword\":" + quote(s.setupPassword) + "}",
               [&s](const Json&) {
          s.post("/api/Authentication/Authenticate", credentials(s.setupUser, s.setupPassword) + "}", [&s](const Json& answer) {
            if (answer["token"].str().empty()) s.fail(SESSION_UNEXPECTED, "the new user does not log in");
          });
        });
      });
    });
  });
}

}  // namespace

extern const Driver fullsense = [] {
  Driver d{"fullsense", "FULLSENSE", {{"http", "http", 12080}}};
  d.login = login;
  d.milk = readMilk;
  d.cow = readCow;
  d.identity = readIdentity;
  d.setup = setup;
  return d;
}();

}  // namespace agrigate
