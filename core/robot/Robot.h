// What a driver for a farm's management server is built from (core/robot_c.h is the same thing
// seen from a host). A driver is one file in drivers/<name>/src: how to log in, and per kind of
// robot it knows what to ask and how the answers become that kind's data (Milking.h). It asks
// with get() and post(); the session sends one request at a time through the host and hands
// each answer to the request's own function, which may ask for more.
#pragma once

#include <deque>
#include <functional>
#include <map>
#include <string>

#include "Json.h"
#include "Milking.h"
#include "robot_c.h"

namespace robot {

class Session;

struct Driver {
  const char* id;     // stored with a robot: never change it
  const char* name;   // the server software
  const char* scheme; // for an address without one
  int port;           // for an address without one
  /** Ask for a login and set Session::headers from the answer (or fail(ROBOT_LOGIN_REJECTED)). */
  void (*login)(Session&);
  /** Per kind of robot: fill that kind's data in the session. Null: the driver has no such robots. */
  void (*milking)(Session&);
};

/** Every driver in this build (Drivers.cpp). */
extern const Driver* const kDrivers[];
extern const int kDriverCount;

class Session {
 public:
  using Answer = std::function<void(const Json&)>;

  Session(const Driver& driver, const std::string& address, std::string user, std::string password);

  const Driver& driver;
  const std::string user, password;
  /** Sent with every request: the login, set by the driver. */
  std::map<std::string, std::string> headers;

  milking::Farm milking;

  /** [optional]: an answer that is not there (an error status, no JSON) is left out and noted
   *  in the error text instead of failing the read. */
  void get(const std::string& path, Answer then, bool optional = false) { ask("GET", path, "", std::move(then), optional); }
  void post(const std::string& path, const std::string& body, Answer then) { ask("POST", path, body, std::move(then), false); }
  /** End the read with this result; the open requests are dropped. */
  void fail(int result, const std::string& why);

  // the C API
  /** [start]: a kind's read (it clears that kind's data first); runs again after a new login. */
  int read(void (*start)(Session&));
  std::string request() const;
  void response(int status, const std::string& body);
  int result() const { return !queue_.empty() ? ROBOT_IN_PROGRESS : result_; }
  const std::string& error() const { return error_; }

 private:
  struct Request { std::string method, path, body; Answer then; bool optional; };
  void ask(const char* method, const std::string& path, const std::string& body, Answer then, bool optional);
  void next();

  std::string base_;   // scheme://host:port
  std::deque<Request> queue_;
  void (*start_)(Session&) = nullptr;
  bool loggingIn_ = false, retried_ = false;
  int result_ = ROBOT_OK;
  std::string error_;
};

/** [s] into a caller's buffer as the C API promises: cut to [size], the full length returned. */
int32_t copyOut(const std::string& s, char* buf, int32_t size);

}  // namespace robot

struct robot_session : robot::Session {
  using robot::Session::Session;
};
