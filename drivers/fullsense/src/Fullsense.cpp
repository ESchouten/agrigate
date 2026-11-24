// The FULLSENSE farm management server: its milking robots, read only.
// Written from the server's own API description; see ../API.md for what a real server still
// has to confirm.
#include "Robot.h"

namespace robot {
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

bool given(const std::string& id) { return !id.empty() && id != "00000000-0000-0000-0000-000000000000"; }

void login(Session& s) {
  s.post("/api/Authentication/Authenticate", "{\"userName\":" + quote(s.user) + ",\"password\":" + quote(s.password) + "}", [&s](const Json& answer) {
    if (!answer["token"].str().empty()) s.headers["Authorization"] = "Bearer " + answer["token"].str();
  });
}

void readMilk(Session& s) {
  s.get("/api/Location/GetLocations?locationType=1", [&s](const Json& locations) {   // 1: the milking robots
    for (const Json& l : locations.items) {
      milk::Robot r;
      r.id = l["id"].str();
      r.name = l["description"].str();
      r.group = l["locationGroup"]["name"].str();
      size_t i = s.milk.robots.size();
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
  });
}

}  // namespace

extern const Driver fullsense = {"fullsense", "FULLSENSE", "http", 12080, login, readMilk};

}  // namespace robot
