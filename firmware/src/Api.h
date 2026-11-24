// The buttons' side of the web API (docs/API.md), no hardware code (tested on a PC): which
// call a request is, who may make it, and the answer's body.
#pragma once
#include <cstdint>
#include <string>

struct ApiCall {
  enum Kind { None, Status, Route, Stop, Planning, Answer, Log, LogLive, Crash } kind = None;
  char route = 0;      // Route: 'A'..'D'
  bool yes = false;    // Planning: on, Answer: yes (finish the route)
  bool fresh = false;  // Status: read the robot now
  bool dryRun = false; // Route: only the checks, nothing starts
};

// method "GET" / "POST" and the path. Nothing is in a query: a web server that drops part of
// one (the M5's keeps only key=value) must never turn a dry run into a start.
inline ApiCall parseCall(const std::string& method, const std::string& path) {
  ApiCall c;
  auto onOff = [&](const std::string& rest, const char* yes, const char* no, ApiCall::Kind k) {
    if (rest == yes || rest == no) {
      c.kind = k;
      c.yes = rest == yes;
    }
  };
  if (method == "GET" && (path == "/api/status" || path == "/api/status/fresh")) {
    c.kind = ApiCall::Status;
    c.fresh = path.size() > 11;
  } else if (method == "GET" && (path == "/api/log" || path == "/api/log/live" || path == "/api/crash")) {
    c.kind = path == "/api/crash" ? ApiCall::Crash : path.size() > 8 ? ApiCall::LogLive : ApiCall::Log;
  } else if (method != "POST") {
    return c;   // everything else changes something: never from a link opened in a browser
  } else if (path.rfind("/api/route/", 0) == 0 && path.size() >= 12 && (path.size() == 12 || path.compare(12, std::string::npos, "/dryRun") == 0)) {
    char r = path[11];
    if (r >= 'a' && r <= 'd') r = char(r - 'a' + 'A');
    if (r >= 'A' && r <= 'D') {
      c.kind = ApiCall::Route;
      c.route = r;
      c.dryRun = path.size() > 12;
    }
  } else if (path == "/api/stop") {
    c.kind = ApiCall::Stop;
  } else if (path.rfind("/api/planning/", 0) == 0) {
    onOff(path.substr(14), "on", "off", ApiCall::Planning);
  } else if (path.rfind("/api/answer/", 0) == 0) {
    onOff(path.substr(12), "yes", "no", ApiCall::Answer);
  }
  return c;
}

// The PIN check: the robot's own PIN (no PIN set = open, the robot cannot be used then anyway).
// A request without a PIN is not counted as a wrong try.
class PinGate {
 public:
  // authorization: the header's value. 200, 401 or 429.
  int check(const std::string& authorization, const std::string& pin, uint32_t now) {
    if (lockedUntil_ && int32_t(now - lockedUntil_) < 0) return 429;
    lockedUntil_ = 0;
    const std::string bearer = "Bearer ";
    std::string given = authorization.rfind(bearer, 0) == 0 ? authorization.substr(bearer.size()) : "";
    if (pin.empty() || given == pin) {
      wrong_ = 0;
      return 200;
    }
    if (!given.empty() && ++wrong_ >= 5) {
      wrong_ = 0;
      lockedUntil_ = (now + 60000) | 1;
    }
    return 401;
  }

 private:
  int wrong_ = 0;
  uint32_t lockedUntil_ = 0;
};

// The robot's status in the apps' generic terms (CleanStatus in native/.../model/Clean.kt).
struct ApiStatus {
  int planning = -1;   // 1 on, 0 off, -1 unknown
  std::string nextRun, activity, route;
  int batteryMv = -1;  // -1 unknown
  bool busy = false, driving = false, finishQuestion = false;
};

inline std::string jsonText(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += (unsigned char)c < 0x20 || (unsigned char)c >= 0x80 ? ' ' : c;   // the robot's text is ASCII
  }
  return o + "\"";
}

inline std::string toJson(const ApiStatus& s) {
  auto flag = [](bool b) { return b ? "true" : "false"; };
  return std::string("{\"planning\":") + (s.planning < 0 ? "null" : flag(s.planning == 1)) +
         ",\"nextRun\":" + jsonText(s.nextRun) +
         ",\"batteryMv\":" + (s.batteryMv < 0 ? "null" : std::to_string(s.batteryMv)) +
         ",\"activity\":" + jsonText(s.activity) + ",\"route\":" + jsonText(s.route) +
         ",\"busy\":" + flag(s.busy) + ",\"driving\":" + flag(s.driving) +
         ",\"finishQuestion\":" + flag(s.finishQuestion) + "}";
}

// The answer's body. fault: "" when it worked, else a CleanFault name ("Busy", ...);
// status: toJson() or "" (not read yet); age: seconds since it was read.
inline std::string apiAnswer(const std::string& fault, const std::string& message, const std::string& status, long age) {
  return std::string("{\"ok\":") + (fault.empty() ? "true" : "false") +
         ",\"fault\":" + (fault.empty() ? "null" : jsonText(fault)) + ",\"message\":" + jsonText(message) +
         ",\"age\":" + (status.empty() ? "null" : std::to_string(age)) +
         ",\"status\":" + (status.empty() ? "null" : status) + "}";
}
