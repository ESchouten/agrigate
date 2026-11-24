// The Lely Discovery as a session's robot (core/session/Clean.h, the "clean" contract of
// core/clean_c.h), for hosts that move the bytes themselves: the apps and Python. The buttons use
// the C++ Robot directly. One worker thread per session runs the blocking Robot; bytes from the
// host are queued by incoming() and handed to the robot inside Transport::pump().
//
// The driver's own, by name. Inputs, the panel's keys: Up, Down, Enter, Back, StartStop, Prog,
// Soft1, Soft2, Soft3. Steps: Linked, Granted, Syncing (again for every part, "progress" in the
// status), ScreenReady, PlanningOff, PlanningOn, Menu, Selected, StartPressed, Answered,
// Restored. In the status' "own": "screen" (the display's text) and "highlighted" (its selected
// row), "language" (the robot's: "nl", "en" or ""), "clock", "actionNo", "rideNo", "manual"
// (on the manual control screen: the motors and direction, 1 / 0 / -1 unknown, else null) and
// "charger" (on the charger approach screen: its side and the battery, else null).
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "DiscoveryRobot.h"
#include "Session.h"
#include "clean_c.h"
#include "discovery_c.h"

using namespace discovery;

namespace {

class HostLink : public Transport {
 public:
  std::function<void(const Bytes&)> send;             // a frame for the host to write

  bool connect() override { return up_; }            // the host already opened the link
  void disconnect() override {}                       // and closes it after the session
  bool linkUp() override { return up_ && !stop_; }
  bool write(const Bytes& f) override {
    if (!linkUp()) return false;
    send(f);
    return true;
  }
  uint32_t now() override {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }
  void pump(uint32_t ms) override {
    Bytes chunk;
    {
      std::unique_lock<std::mutex> l(mu_);
      cv_.wait_for(l, std::chrono::milliseconds(ms), [this] { return !inbox_.empty() || stop_ || !up_; });
      chunk.swap(inbox_);
    }
    if (!chunk.empty() && onData) onData(chunk.data(), chunk.size());
  }

  void feed(const uint8_t* d, size_t n) {
    std::lock_guard<std::mutex> g(mu_);
    inbox_.insert(inbox_.end(), d, d + n);
    cv_.notify_all();
  }
  void down() { std::lock_guard<std::mutex> g(mu_); up_ = false; cv_.notify_all(); }
  void stop() { std::lock_guard<std::mutex> g(mu_); stop_ = true; cv_.notify_all(); }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  Bytes inbox_;
  bool up_ = true, stop_ = false;
};

std::string jsonString(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else if ((unsigned char)c >= 0x80) o += '?';   // robot font is ASCII; keep JSON valid UTF-8
        else o += c;
    }
  }
  return o + "\"";
}

const char* boolJson(bool b) { return b ? "true" : "false"; }

std::string statusJson(const Status& st, const Screen& scr, int sync, bool cancellable) {
  std::string o = "{";
  o += std::string("\"planning\":") + (st.planning < 0 ? "null" : boolJson(st.planning != 0));
  o += ",\"nextRun\":" + jsonString(st.nextRide);
  o += ",\"route\":" + jsonString(st.route);
  o += ",\"activity\":" + jsonString(st.action);
  o += ",\"batteryMv\":" + (st.batteryMv < 0 ? "null" : std::to_string(st.batteryMv));
  o += std::string(",\"busy\":") + boolJson(st.busy);
  o += std::string(",\"driving\":") + boolJson(st.driving);
  o += std::string(",\"finishQuestion\":") + boolJson(st.stopQuestion);
  o += std::string(",\"cancellable\":") + boolJson(cancellable);
  o += ",\"progress\":" + (sync < 0 ? "null" : std::to_string(sync));
  o += ",\"own\":{\"screen\":" + jsonString(scr.text());
  o += ",\"highlighted\":" + jsonString(scr.isManual() || scr.isCharger() ? "" : scr.highlighted());   // there the boxes are settings
  o += ",\"language\":" + jsonString(st.language);
  o += ",\"clock\":" + jsonString(st.clock);
  o += ",\"actionNo\":" + jsonString(st.actionNo);
  o += ",\"rideNo\":" + jsonString(st.rideNo);
  if (scr.isManual()) {
    ManualState m = scr.manual();
    o += ",\"manual\":{\"left\":" + std::to_string(m.left) + ",\"right\":" + std::to_string(m.right) +
         ",\"forward\":" + std::to_string(m.forward) + "}";
  } else {
    o += ",\"manual\":null";
  }
  if (scr.isCharger()) {
    ChargerState c = scr.charger();
    o += ",\"charger\":{\"left\":" + std::to_string(c.left) + ",\"right\":" + std::to_string(c.right) +
         ",\"volt\":" + jsonString(c.volt) + ",\"amp\":" + jsonString(c.amp) + "}";
  } else {
    o += ",\"charger\":null";
  }
  return o + "}}";
}

const struct { const char* name; Key key; } kKeys[] = {
    {"Back", Key::Back}, {"Soft1", Key::Soft1}, {"Soft2", Key::Soft2}, {"Soft3", Key::Soft3}, {"Up", Key::Up},
    {"Down", Key::Down}, {"Prog", Key::Prog}, {"Enter", Key::Enter}, {"StartStop", Key::StartStop}};

const char* stepName(Step s) {
  static const char* const names[] = {"", "Linked", "Granted", "ScreenReady", "PlanningOff", "PlanningOn", "Menu",
                                      "Selected", "StartPressed", "Answered", "Restored", "Syncing"};
  return (int)s > 0 && (int)s < int(sizeof names / sizeof *names) ? names[(int)s] : "";
}

int code(Result r) {
  switch (r) {
    case Result::Ok: return SESSION_OK;
    case Result::ConnectFailed: return SESSION_UNREACHABLE;
    case Result::PinRejected: return SESSION_LOGIN_REJECTED;
    case Result::Busy: return CLEAN_BUSY;
    case Result::StartedNoAnswer: return CLEAN_NO_CONFIRMATION;
    case Result::Cancelled: return CLEAN_CANCELLED;
    case Result::NoScreen:
    case Result::Unexpected: break;
  }
  return SESSION_UNEXPECTED;
}

int64_t clockMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// What the robot is owed after a dropped link is forgotten after this long: someone may have
// changed the robot on purpose since.
constexpr int64_t kOwedForMs = 6 * 60 * 60 * 1000;

class SessionRobot : public agrigate::clean::Robot {
 public:
  explicit SessionRobot(const std::string& pin) : robot_(link_, encryptPin(pin.c_str()), version_) {
    robot_.lang = Lang::En;
    link_.send = [this](const Bytes& f) {
      std::lock_guard<std::mutex> g(mu_);
      out_.emplace_back(f.begin(), f.end());
      changed();
    };
    auto robotHandler = link_.onData;                 // set by Robot; add a snapshot after it
    link_.onData = [this, robotHandler](const uint8_t* b, size_t n) {
      robotHandler(b, n);
      snapshot();
    };
    link_.cancelled = [this] { return cancelGen_.load() == runningGen_.load(); };
    robot_.onStep = [this](Step st) {
      snapshot();
      std::lock_guard<std::mutex> g(mu_);
      steps_.push_back(stepName(st));
      changed();
    };
    worker_ = std::thread([this] { run(); });
  }

  ~SessionRobot() override {
    {
      std::lock_guard<std::mutex> g(mu_);
      quit_ = true;
    }
    link_.stop();
    cv_.notify_all();
    worker_.join();
  }

  int connect() override {
    return submit(true, [this] {
      recall();
      Result r = robot_.connect();
      if (r == Result::Ok) connected_ = true;
      return r;
    });
  }
  int startRoute(char letter, bool dryRun) override {
    return submit(true, [this, letter, dryRun] { return robot_.startRoute(letter, dryRun); });
  }
  int stopRoute() override { return submit(false, [this] { return robot_.stopRoute(); }); }
  int answerStop(bool finish) override { return submit(false, [this, finish] { return robot_.answerStop(finish); }); }
  int setPlanning(bool on) override { return submit(true, [this, on] { return robot_.setPlanning(on); }); }
  int finish() override { return submit(false, [this] { return robot_.finish(); }); }

  int readRoutes() override {
    return submit(true, [this] {
      std::vector<std::string> out;
      Result r = robot_.readRoutes(out);
      if (r == Result::Ok) {
        std::string js = "{";
        for (int i = 0; i < 4; i++) {
          std::string v;
          for (char c : out[i]) if (c != '"' && c != '\\' && (unsigned char)c >= 32) v += c;
          js += std::string(i ? "," : "") + "\"" + char('A' + i) + "\":\"" + v + "\"";
        }
        js += "}";
        std::lock_guard<std::mutex> g(mu_);
        routes_ = js;
      }
      return r;
    });
  }

  int input(const std::string& name, bool hold) override {
    const Key* found = nullptr;
    for (const auto& k : kKeys)
      if (name == k.name) found = &k.key;
    if (!found) return SESSION_UNEXPECTED;
    const Key key = *found;
    if (!hold) return submit(false, [this, key] { return robot_.press(key); });
    int gen = ++holdGen_;
    return submit(false, [this, key, gen] {
      uint32_t start = link_.now();
      robot_.holdBegin(key);
      // the robot needs a press of some length, also for a quick tap on a touch screen
      while ((releasedGen_ < gen || link_.now() - start < 80) && robot_.linked() && link_.now() - start < 120000)
        robot_.driveHold(20);
      robot_.holdEnd();
      return Result::Ok;
    });
  }
  void release() override { releasedGen_ = holdGen_.load(); }

  void cancel() override {
    std::lock_guard<std::mutex> g(mu_);
    if (busy_) cancelGen_ = cmdGen_;
  }

  int result() override {
    std::lock_guard<std::mutex> g(mu_);
    return busy_ ? SESSION_IN_PROGRESS : result_;
  }
  std::string error() override {
    std::lock_guard<std::mutex> g(mu_);
    return error_;
  }
  void language(const std::string& code) override {
    std::lock_guard<std::mutex> g(mu_);
    robot_.lang = langFromCode(code.c_str());
  }
  std::string status() override {
    std::lock_guard<std::mutex> g(mu_);
    return status_;
  }
  std::string routes() override {
    std::lock_guard<std::mutex> g(mu_);
    return routes_;
  }
  std::string nextStep() override {
    std::lock_guard<std::mutex> g(mu_);
    if (steps_.empty()) return "";
    std::string step = steps_.front();
    steps_.pop_front();
    return step;
  }
  std::string memory() override {
    std::lock_guard<std::mutex> g(mu_);
    return memory_;
  }
  void remember(const std::string& memory) override {
    std::lock_guard<std::mutex> g(mu_);
    memory_ = remembered_ = memory;
  }

  void incoming(const uint8_t* data, size_t len) override { link_.feed(data, len); }
  bool outgoing(std::string& bytes) override {
    std::lock_guard<std::mutex> g(mu_);
    if (out_.empty()) return false;
    bytes = std::move(out_.front());
    out_.pop_front();
    return true;
  }
  void linkDown() override {
    link_.down();
    std::lock_guard<std::mutex> g(mu_);
    changed();
  }
  void wait(int ms) override {
    std::unique_lock<std::mutex> l(mu_);
    hostCv_.wait_for(l, std::chrono::milliseconds(ms), [this] { return changes_ != seen_; });
    seen_ = changes_;
  }

 private:
  void changed() {   // with mu_ held
    changes_++;
    hostCv_.notify_all();
  }

  int submit(bool cancellable, std::function<Result()> job) {
    std::lock_guard<std::mutex> g(mu_);
    if (busy_) return SESSION_IN_PROGRESS;
    busy_ = true;
    pendingCancellable_ = cancellable;
    pendingGen_ = ++cmdGen_;
    pending_ = std::move(job);
    cv_.notify_all();
    return SESSION_OK;
  }

  // Worker thread, before connecting: what the last session with this robot left to remember.
  void recall() {
    std::string text;
    {
      std::lock_guard<std::mutex> g(mu_);
      text = remembered_;
    }
    agrigate::Json m = agrigate::Json::parse(text);
    const std::string& image = m["image"].str();
    if (image.size() == 18)
      for (int i = 0; i < 9; i++) version_[i] = (uint8_t)std::stoi(image.substr(i * 2, 2), nullptr, 16);
    const int64_t at = (int64_t)m["owedAt"].num(0);
    if (!m["owedPlanning"].isNull() && clockMs() - at >= 0 && clockMs() - at <= kOwedForMs) {
      owed_ = m["owedPlanning"].str() == "true";
      owedAt_ = at;
      robot_.setOwedPlanning(owed_);
    }
  }

  void snapshot() {   // worker thread
    bool canCancel = runningCancellable_ && robot_.cancellable();
    std::string js = statusJson(robot_.status(), robot_.screen, robot_.syncPercent(), canCancel);
    if (robot_.owedPlanning() != owed_) {
      owed_ = robot_.owedPlanning();
      owedAt_ = clockMs();
    }
    std::string memory = "{";
    if (version_[0]) {
      static const char* const hex = "0123456789abcdef";
      memory += "\"image\":\"";
      for (uint8_t b : version_) memory += std::string(1, hex[b >> 4]) + hex[b & 15];
      memory += "\"";
    }
    if (owed_ >= 0)
      memory += std::string(memory.size() > 1 ? "," : "") + "\"owedPlanning\":" + boolJson(owed_ != 0) + ",\"owedAt\":" + std::to_string(owedAt_);
    memory += "}";
    std::lock_guard<std::mutex> g(mu_);
    if (js != status_ || memory != memory_) changed();
    status_ = js;
    memory_ = memory;
  }

  void run() {   // worker thread
    for (;;) {
      std::function<Result()> job;
      {
        std::lock_guard<std::mutex> g(mu_);
        if (quit_) break;
        if (pending_) {
          job.swap(pending_);
          runningGen_ = pendingGen_;
          runningCancellable_ = pendingCancellable_;
        }
      }
      if (!job) {
        robot_.idle(50);   // keep-alive while connected and idle
        continue;
      }
      robot_.clearCancel();
      Result r = job();
      runningCancellable_ = false;
      snapshot();
      std::string why = r == Result::Ok ? "" : resultText(r, robot_.lang);
      if (r != Result::Ok && !robot_.lastError().empty()) why += ": " + robot_.lastError();
      std::lock_guard<std::mutex> g(mu_);
      result_ = code(r);
      error_ = why;
      busy_ = false;
      changed();
    }
    if (connected_) robot_.disconnect();
  }

  HostLink link_;
  uint8_t version_[9] = {0};
  discovery::Robot robot_;

  std::mutex mu_;
  std::condition_variable cv_, hostCv_;   // for the worker (a command, quit), for the host (wait)
  std::function<Result()> pending_;
  std::deque<std::string> out_, steps_;
  int result_ = SESSION_OK;
  unsigned changes_ = 0, seen_ = 0;
  bool busy_ = false, quit_ = false, connected_ = false;
  int owed_ = -1;        // worker thread: the planning the robot is owed, and since when
  int64_t owedAt_ = 0;
  std::atomic<int> holdGen_{0}, releasedGen_{0};   // a hold runs until its number is released
  // cancel() marks the command with this number (0 = none yet): only that one stops
  int pendingGen_ = 0, cmdGen_ = 0;
  std::atomic<int> runningGen_{-1}, cancelGen_{0};
  bool pendingCancellable_ = false;
  bool runningCancellable_ = false;   // worker thread: the running command takes cancel()
  std::string status_ = "{}", error_, routes_ = "{}", memory_ = "{}", remembered_;
  std::thread worker_;
};

std::vector<agrigate::Link> links() {
  agrigate::Link ble{"ble-serial"}, classic{"bt-serial"};
  ble.service = kBle.service;
  ble.rx = kBle.rx;
  ble.tx = kBle.tx;
  classic.service = kSppService;
  ble.addressPrefix = classic.addressPrefix = kBle.addressPrefix;
  return {ble, classic};
}

}  // namespace

namespace agrigate {

extern const Driver discovery = [] {
  Driver d{"discovery", "Discovery", links()};
  d.clean = [](Session& s) -> clean::Robot* { return new SessionRobot(s.password); };
  return d;
}();

}  // namespace agrigate

int32_t discovery_encrypt_pin(const char* pin, uint8_t* out, int32_t size) {
  Bytes b = encryptPin(pin ? pin : "");
  if (out) memcpy(out, b.data(), b.size() < (size_t)size ? b.size() : (size_t)size);
  return (int32_t)b.size();
}
