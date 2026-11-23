// The session with a farm's server and the C API on it (core/robot_c.h).
#include "Robot.h"

#include <cstring>

namespace robot {

Session::Session(const Driver& d, const std::string& address, std::string u, std::string p)
    : driver(d), user(std::move(u)), password(std::move(p)) {
  base_ = address.find("://") == std::string::npos ? std::string(d.scheme) + "://" + address : address;
  while (!base_.empty() && base_.back() == '/') base_.pop_back();
  std::string host = base_.substr(base_.find("://") + 3);
  if (host.find(':', host.find(']') == std::string::npos ? 0 : host.find(']')) == std::string::npos)
    base_ += ":" + std::to_string(d.port);
}

void Session::ask(const char* method, const std::string& path, const std::string& body, Answer then, bool optional) {
  queue_.push_back({method, path, body, std::move(then), optional});
}

void Session::fail(int result, const std::string& why) {
  queue_.clear();
  result_ = result;
  error_ = why;
}

int Session::read(void (*start)(Session&)) {
  if (!queue_.empty()) return ROBOT_IN_PROGRESS;
  start_ = start;
  retried_ = false;
  result_ = ROBOT_OK;
  error_.clear();
  next();
  return ROBOT_OK;
}

void Session::next() {   // nothing is open: log in first, then the read itself
  loggingIn_ = headers.empty();
  if (!loggingIn_) return start_(*this);
  driver.login(*this);
  // a login that needs no request (a key the user gave, set straight into the headers): read now
  if (queue_.empty() && result_ == ROBOT_OK && !headers.empty()) {
    loggingIn_ = false;
    start_(*this);
  }
}

std::string Session::request() const {
  if (queue_.empty()) return "";
  const Request& r = queue_.front();
  std::string o = "{\"method\":" + quote(r.method) + ",\"url\":" + quote(base_ + r.path) + ",\"headers\":{\"Accept\":\"application/json\"";
  if (!r.body.empty()) o += ",\"Content-Type\":\"application/json\"";
  for (const auto& h : headers) o += "," + quote(h.first) + ":" + quote(h.second);
  return o + "},\"body\":" + quote(r.body) + "}";
}

void Session::response(int status, const std::string& body) {
  if (queue_.empty()) return;
  Request r = std::move(queue_.front());
  queue_.pop_front();
  const std::string what = r.method + " " + r.path;
  if (status == 0) return fail(ROBOT_UNREACHABLE, "no answer from " + base_);
  if (loggingIn_ && (status == 400 || status == 401 || status == 403))
    return fail(ROBOT_LOGIN_REJECTED, what + ": HTTP " + std::to_string(status));
  if (status == 401 && !retried_) {   // the login ran out: once more from a new one
    retried_ = true;
    headers.clear();
    queue_.clear();
    return next();
  }
  bool good = status >= 200 && status < 300, json = false;
  Json answer = good ? Json::parse(body, &json) : Json();
  if (json) {
    r.then(answer);
  } else {
    const std::string why = what + (good ? ": not JSON" : ": HTTP " + std::to_string(status));
    // a 401 here is the second one: the server does not take the login it was just given
    if (!r.optional) return fail(status == 401 ? ROBOT_LOGIN_REJECTED : ROBOT_UNEXPECTED, why);
    error_ += (error_.empty() ? "" : "; ") + why;
  }
  if (loggingIn_ && queue_.empty() && result_ == ROBOT_OK) {   // logged in: now the read
    if (headers.empty()) return fail(ROBOT_LOGIN_REJECTED, what + ": no login in the answer");
    next();
  }
}

}  // namespace robot

namespace robot {

// Every kind of robot a driver can know: a new kind adds its line here, its read to Driver and
// its header to core/.
const struct { const char* name; void (*Driver::*read)(Session&); } kKinds[] = {{"milking", &Driver::milking}, {"herd", &Driver::herd}};

int32_t copyOut(const std::string& s, char* buf, int32_t size) {
  if (buf && size > 0) {
    size_t n = s.size() < (size_t)size - 1 ? s.size() : (size_t)size - 1;
    memcpy(buf, s.data(), n);
    buf[n] = 0;
  }
  return (int32_t)s.size();
}
}  // namespace robot

extern "C" {

int32_t robot_drivers_json(char* buf, int32_t size) {
  std::string o = "[";
  for (int i = 0; i < robot::kDriverCount; i++) {
    const robot::Driver& d = *robot::kDrivers[i];
    o += std::string(i ? "," : "") + "{\"id\":" + robot::quote(d.id) + ",\"name\":" + robot::quote(d.name) + ",\"kinds\":[";
    bool first = true;
    for (const auto& kind : robot::kKinds)
      if (d.*kind.read) { o += std::string(first ? "" : ",") + robot::quote(kind.name); first = false; }
    o += "],\"link\":{\"kind\":\"http\",\"scheme\":" + robot::quote(d.scheme) + ",\"port\":" + std::to_string(d.port) + "}}";
  }
  return robot::copyOut(o + "]", buf, size);
}

robot_session* robot_open(const char* driver, const char* address, const char* user, const char* password) {
  for (int i = 0; i < robot::kDriverCount; i++)
    if (driver && !strcmp(robot::kDrivers[i]->id, driver))
      return new robot_session(*robot::kDrivers[i], address ? address : "", user ? user : "", password ? password : "");
  return nullptr;
}

void robot_close(robot_session* s) { delete s; }

int32_t robot_request_json(robot_session* s, char* buf, int32_t size) { return robot::copyOut(s->request(), buf, size); }

void robot_response(robot_session* s, int32_t status, const char* body, int32_t len) {
  s->response(status, body && len > 0 ? std::string(body, (size_t)len) : std::string());
}

int32_t robot_result(robot_session* s) { return s->result(); }

int32_t robot_last_error(robot_session* s, char* buf, int32_t size) { return robot::copyOut(s->error(), buf, size); }

}  // extern "C"
