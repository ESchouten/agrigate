// The session with one source and the C API on it (core/session_c.h).
#include "Session.h"

#include <cstring>

namespace agrigate {

namespace {
constexpr int64_t kSilentMs = 10000;   // a source that tells by itself and said nothing for this long is not there
}

Session::Session(const Driver& d, const std::string& a, std::string u, std::string p)
    : driver(d), address(a), user(std::move(u)), password(std::move(p)) {
  if (driver.incoming) return;   // udp: the host sends to the address as it is
  base_ = address.find("://") == std::string::npos ? std::string(d.link.scheme) + "://" + address : address;
  while (!base_.empty() && base_.back() == '/') base_.pop_back();
  std::string host = base_.substr(base_.find("://") + 3);
  if (host.find(':', host.find(']') == std::string::npos ? 0 : host.find(']')) == std::string::npos)
    base_ += ":" + std::to_string(d.link.port);
}

void Session::ask(const char* method, const std::string& path, const std::string& body, Answer then, bool optional) {
  queue_.push_back({method, path, body, std::move(then), optional});
}

void Session::fail(int result, const std::string& why) {
  queue_.clear();
  result_ = result;
  error_ = why;
}

int Session::read(void (*start)(Session&), int64_t* updated) {
  if (!queue_.empty()) return SESSION_IN_PROGRESS;
  start_ = start;
  updated_ = updated;
  retried_ = false;
  result_ = SESSION_OK;
  error_.clear();
  if (driver.incoming) {   // the source tells by itself: nothing to ask, only whether it is still there
    if (!heard_ || now - heard_ >= kSilentMs) fail(SESSION_UNREACHABLE, "no message from " + address);
  } else {
    next();
  }
  return SESSION_OK;
}

void Session::next() {   // nothing is open: log in first, then the read itself
  loggingIn_ = headers.empty();
  if (!loggingIn_) return start_(*this);
  driver.login(*this);
  // a login that needs no request (a key the user gave, set straight into the headers): read now
  if (queue_.empty() && result_ == SESSION_OK && !headers.empty()) {
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
  if (status == 0) return fail(SESSION_UNREACHABLE, "no answer from " + base_);
  if (loggingIn_ && (status == 400 || status == 401 || status == 403))
    return fail(SESSION_LOGIN_REJECTED, what + ": HTTP " + std::to_string(status));
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
    if (!r.optional) return fail(status == 401 ? SESSION_LOGIN_REJECTED : SESSION_UNEXPECTED, why);
    error_ += (error_.empty() ? "" : "; ") + why;
  }
  if (loggingIn_ && queue_.empty() && result_ == SESSION_OK) {   // logged in: now the read
    if (headers.empty()) return fail(SESSION_LOGIN_REJECTED, what + ": no login in the answer");
    next();
  }
  if (queue_.empty() && result_ == SESSION_OK) *updated_ = now;
}

bool Session::outgoing(std::string& datagram) const {
  if (out_.empty()) return false;
  datagram = out_.front();
  return true;
}

// Every contract a driver can serve: a new one adds its line here, its read to Driver, its data
// to Session and its header to core/.
const struct { const char* name; void (*Driver::*read)(Session&); } kContracts[] = {
    {"milk", &Driver::milk}, {"cow", &Driver::cow}, {"identity", &Driver::identity}};

int32_t copyOut(const std::string& s, char* buf, int32_t size) {
  if (buf && size > 0) {
    size_t n = s.size() < (size_t)size - 1 ? s.size() : (size_t)size - 1;
    memcpy(buf, s.data(), n);
    buf[n] = 0;
  }
  return (int32_t)s.size();
}

std::string timeJson(int64_t ms) { return ms ? std::to_string(ms) : "null"; }

}  // namespace agrigate

extern "C" {

int32_t session_drivers_json(char* buf, int32_t size) {
  using namespace agrigate;
  std::string o = "[";
  for (int i = 0; i < kDriverCount; i++) {
    const Driver& d = *kDrivers[i];
    o += std::string(i ? "," : "") + "{\"id\":" + quote(d.id) + ",\"name\":" + quote(d.name) + ",\"contracts\":[";
    bool first = true;
    for (const auto& c : kContracts)
      if (d.*c.read) { o += std::string(first ? "" : ",") + quote(c.name); first = false; }
    o += "],\"link\":{\"kind\":" + quote(d.link.kind);
    if (d.link.scheme) o += ",\"scheme\":" + quote(d.link.scheme);
    o += ",\"port\":" + std::to_string(d.link.port) + "}}";
  }
  return copyOut(o + "]", buf, size);
}

session* session_open(const char* driver, const char* address, const char* user, const char* password) {
  for (int i = 0; i < agrigate::kDriverCount; i++)
    if (driver && !strcmp(agrigate::kDrivers[i]->id, driver))
      return new session(*agrigate::kDrivers[i], address ? address : "", user ? user : "", password ? password : "");
  return nullptr;
}

void session_close(session* s) { delete s; }

void session_tick(session* s, int64_t now_ms) {
  s->now = now_ms;
  if (s->driver.tick) s->driver.tick(*s);
}

int32_t session_request_json(session* s, char* buf, int32_t size) { return agrigate::copyOut(s->request(), buf, size); }

void session_response(session* s, int32_t status, const char* body, int32_t len) {
  s->response(status, body && len > 0 ? std::string(body, (size_t)len) : std::string());
}

int32_t session_outgoing(session* s, uint8_t* buf, int32_t size) {
  std::string d;
  if (!s->outgoing(d)) return 0;
  if (buf && size >= (int32_t)d.size()) {
    memcpy(buf, d.data(), d.size());
    s->sent();
  }
  return (int32_t)d.size();
}

void session_incoming(session* s, const uint8_t* data, int32_t len, int64_t now_ms) {
  s->now = now_ms;
  if (s->driver.incoming && data && len > 0) s->driver.incoming(*s, std::string((const char*)data, (size_t)len));
}

void session_stop(session* s) {
  if (s->driver.stop) s->driver.stop(*s);
}

int32_t session_result(session* s) { return s->result(); }

int32_t session_last_error(session* s, char* buf, int32_t size) { return agrigate::copyOut(s->error(), buf, size); }

}  // extern "C"
