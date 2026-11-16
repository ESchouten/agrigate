// The buttons' side of the web API (docs/API.md), no hardware code (tested on a PC): which
// call a request is, who may make it, and the answer's body.
#pragma once
#include <cstdint>
#include <string>

struct ApiCall {
  enum Kind { None, Status, Route, Planning, Answer } kind = None;
  char route = 0;      // Route: 'A'..'D'
  bool yes = false;    // Planning: on, Answer: yes (finish the route)
  bool fresh = false;  // Status: read the robot now
};

// method "GET" / "POST", path without the query, query = the part after '?'.
inline ApiCall parseCall(const std::string& method, const std::string& path, const std::string& query) {
  ApiCall c;
  auto onOff = [&](const std::string& rest, const char* yes, const char* no, ApiCall::Kind k) {
    if (rest == yes || rest == no) {
      c.kind = k;
      c.yes = rest == yes;
    }
  };
  if (method == "GET" && path == "/api/status") {
    c.kind = ApiCall::Status;
    c.fresh = query.find("fresh") != std::string::npos;
  } else if (method == "POST" && path.size() == 12 && path.rfind("/api/route/", 0) == 0) {
    char r = path[11];
    if (r >= 'a' && r <= 'd') r = char(r - 'a' + 'A');
    if (r >= 'A' && r <= 'D') {
      c.kind = ApiCall::Route;
      c.route = r;
    }
  } else if (method == "POST" && path.rfind("/api/planning/", 0) == 0) {
    onOff(path.substr(14), "on", "off", ApiCall::Planning);
  } else if (method == "POST" && path.rfind("/api/answer/", 0) == 0) {
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

// The answer's body. error: a JSON string (driver::jsonString), status: JSON or "" (not read
// yet), age: seconds since it was read.
inline std::string apiAnswer(int result, const std::string& error, const std::string& status, long age) {
  return "{\"result\":" + std::to_string(result) + ",\"error\":" + error +
         ",\"age\":" + (status.empty() ? "null" : std::to_string(age)) +
         ",\"status\":" + (status.empty() ? "null" : status) + "}";
}
