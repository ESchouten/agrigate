// Host test for the M5's web API (Api.h): which call a request is, and the PIN check.
#include <cassert>
#include <cstdio>

#include "../src/Api.h"

int main() {
  ApiCall c = parseCall("GET", "/api/clean/status");
  assert(c.kind == ApiCall::Status && !c.fresh);
  assert(parseCall("GET", "/api/clean/status/fresh").fresh);
  assert(parseCall("GET", "/api/clean/status/now").kind == ApiCall::None);
  assert(parseCall("POST", "/api/clean/status").kind == ApiCall::None);   // reading is a GET
  c = parseCall("POST", "/api/clean/route/B");
  assert(c.kind == ApiCall::Route && c.route == 'B');
  assert(parseCall("POST", "/api/clean/route/d").route == 'D');
  assert(!c.dryRun);
  c = parseCall("POST", "/api/clean/route/A/dryRun");
  assert(c.kind == ApiCall::Route && c.route == 'A' && c.dryRun);
  for (const char* bad : {"/api/clean/route/E", "/api/clean/route/", "/api/clean/route/AB", "/api/clean/route", "/api/clean/route/A/dry", "/api/clean/route/E/dryRun"})
    assert(parseCall("POST", bad).kind == ApiCall::None);
  assert(parseCall("GET", "/api/clean/route/A").kind == ApiCall::None);   // a link in a browser starts nothing
  c = parseCall("POST", "/api/clean/planning/on");
  assert(c.kind == ApiCall::Planning && c.yes);
  c = parseCall("POST", "/api/clean/planning/off");
  assert(c.kind == ApiCall::Planning && !c.yes);
  assert(parseCall("POST", "/api/clean/planning/maybe").kind == ApiCall::None);
  c = parseCall("POST", "/api/clean/answer/yes");
  assert(c.kind == ApiCall::Answer && c.yes);
  c = parseCall("POST", "/api/clean/answer/no");
  assert(c.kind == ApiCall::Answer && !c.yes);
  assert(parseCall("GET", "/").kind == ApiCall::None);
  // an app from before /api/clean/ still finds the robot's calls under /api/
  assert(parseCall("POST", "/api/route/B").route == 'B' && parseCall("GET", "/api/status/fresh").fresh);
  assert(parseCall("POST", "/api/planning/off").kind == ApiCall::Planning && parseCall("POST", "/api/stop").kind == ApiCall::Stop);
  // the device's own calls are not the robot's
  assert(parseCall("GET", "/api/clean/log").kind == ApiCall::None && parseCall("GET", "/api/clean/crash").kind == ApiCall::None);
  assert(parseCall("GET", "/api/log").kind == ApiCall::Log);
  assert(parseCall("GET", "/api/log/live").kind == ApiCall::LogLive);
  assert(parseCall("GET", "/api/crash").kind == ApiCall::Crash);
  assert(parseCall("POST", "/api/log").kind == ApiCall::None);
  assert(parseCall("GET", "/api/log/all").kind == ApiCall::None);

  PinGate g;
  assert(g.check("Bearer 1234", "1234", 1000) == 200);
  assert(g.check("1234", "1234", 1000) == 401);        // only as a bearer token
  assert(g.check("", "", 1000) == 200);                // no PIN set: open
  for (int i = 0; i < 10; i++) assert(g.check("", "1234", 1000) == 401);   // no PIN given: not counted
  assert(g.check("Bearer 1234", "1234", 1000) == 200);
  for (int i = 0; i < 4; i++) assert(g.check("Bearer 0000", "1234", 2000) == 401);
  assert(g.check("Bearer 1234", "1234", 2000) == 200);   // a right one resets the count
  for (int i = 0; i < 5; i++) assert(g.check("Bearer 0000", "1234", 3000) == 401);
  assert(g.check("Bearer 1234", "1234", 3000) == 429);   // 5 wrong in a row: a minute closed
  assert(g.check("Bearer 1234", "1234", 62999) == 429);
  assert(g.check("Bearer 1234", "1234", 63001) == 200);
  PinGate wrap;   // the lockout also ends when millis() wraps around
  for (int i = 0; i < 5; i++) wrap.check("Bearer 0", "1", 0xFFFFFFF0u);
  assert(wrap.check("Bearer 1", "1", 0xFFFFFFF0u + 30000) == 429);
  assert(wrap.check("Bearer 1", "1", 0xFFFFFFF0u + 61000) == 200);

  assert(parseCall("POST", "/api/clean/stop").kind == ApiCall::Stop);
  assert(parseCall("GET", "/api/clean/stop").kind == ApiCall::None);
  ApiStatus st;
  assert(toJson(st) == "{\"planning\":null,\"nextRun\":\"\",\"batteryMv\":null,\"activity\":\"\",\"route\":\"\","
                       "\"busy\":false,\"driving\":false,\"finishQuestion\":false}");
  st.planning = 1;
  st.nextRun = "21:00";
  st.batteryMv = 25300;
  st.activity = "say \"hi\"\n";
  st.driving = true;
  assert(toJson(st) == "{\"planning\":true,\"nextRun\":\"21:00\",\"batteryMv\":25300,\"activity\":\"say \\\"hi\\\" \","
                       "\"route\":\"\",\"busy\":false,\"driving\":true,\"finishQuestion\":false}");
  assert(apiAnswer("", "", "", 0) == "{\"ok\":true,\"fault\":null,\"message\":\"\",\"age\":null,\"status\":null}");
  assert(apiAnswer("Busy", "busy", "{}", 12) == "{\"ok\":false,\"fault\":\"Busy\",\"message\":\"busy\",\"age\":12,\"status\":{}}");
  printf("ok   web API\n");
}
