// What a driver is built from (core/session_c.h is the same thing seen from a host). A driver is
// one file in drivers/<name>/src: its link, and per contract it serves how that contract's data
// (Milk.h, Cow.h, Identity.h) is brought up to date. Over "http" it asks with get() and post(): the
// session sends one request at a time through the host and hands each answer to the request's
// own function, which may ask for more. Over "udp" it sends datagrams with send() and is handed
// what the source says (incoming). A manure robot (Clean.h) is a conversation with its own timing:
// its driver makes the session's robot, which also takes the link's bytes.
#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Clean.h"
#include "Cow.h"
#include "Identity.h"
#include "Json.h"
#include "Milk.h"
#include "session_c.h"

namespace agrigate {

class Session;

/** How a source is reached (session_c.h): what the host needs to know to open it. */
struct Link {
  const char* kind;               // "http", "udp", "ble-serial" or "bt-serial"
  const char* scheme = nullptr;   // http: for an address without one
  int port = 0;                   // http, udp: for an address without one
  const char* service = nullptr;  // ble-serial, bt-serial: the service's UUID
  const char* rx = nullptr;       // ble-serial: the characteristic written to (without response)
  const char* tx = nullptr;       // ble-serial: the one that notifies
  const char* addressPrefix = nullptr;   // serial: of the devices' addresses, lower case ("" = any)
};

struct Driver {
  const char* id;     // stored with a source: never change it
  const char* name;   // what the source is (the server software, the make of robot)
  std::vector<Link> links;   // the first is preferred; the messages are the same over each

  /** http: ask for a login and set Session::headers from the answer (or fail(SESSION_LOGIN_REJECTED)). */
  void (*login)(Session&) = nullptr;

  /** Per contract: bring that contract's data in the session up to date. Null: the driver does
   *  not serve it. A source that tells by itself has nothing to ask here: an empty function. */
  void (*milk)(Session&) = nullptr;
  void (*cow)(Session&) = nullptr;
  void (*identity)(Session&) = nullptr;
  /** "clean": the robot behind a new session (Clean.h), which the session then owns. */
  clean::Robot* (*clean)(Session&) = nullptr;

  /** udp. tick: every ~100 ms, queue what has to be sent now. incoming: a datagram from the
   *  source. stop: queue the goodbye, if there is one. */
  void (*tick)(Session&) = nullptr;
  void (*incoming)(Session&, const std::string& datagram) = nullptr;
  void (*stop)(Session&) = nullptr;
};

/** Every driver in this build (Drivers.cpp). */
extern const Driver* const kDrivers[];
extern const int kDriverCount;

class Session {
 public:
  using Answer = std::function<void(const Json&)>;

  Session(const Driver& driver, const std::string& address, std::string user, std::string password);

  const Driver& driver;
  const std::string address, user, password;
  /** The host's clock (ms since 1970), 0 as long as the host gave none. */
  int64_t now = 0;

  milk::Farm milk;
  cow::Herd cow;
  identity::Places identity;
  std::unique_ptr<clean::Robot> robot;
  std::string step;   // of the robot's running command, until the host took it (clean_next_step)

  // ---- http
  /** Sent with every request: the login, set by the driver. */
  std::map<std::string, std::string> headers;
  /** [optional]: an answer that is not there (an error status, no JSON) is left out and noted
   *  in the error text instead of failing the read. */
  void get(const std::string& path, Answer then, bool optional = false) { ask("GET", path, "", std::move(then), optional); }
  void post(const std::string& path, const std::string& body, Answer then) { ask("POST", path, body, std::move(then), false); }
  /** End the read with this result; the open requests are dropped. */
  void fail(int result, const std::string& why);

  // ---- udp
  void send(const std::string& datagram) { out_.push_back(datagram); }
  /** The source said something: it is there. */
  void heard() { heard_ = now; }
  int64_t lastHeard() const { return heard_; }
  /** For the driver's own bookkeeping (when it last registered). */
  int64_t sentAt = 0;

  // ---- the C API
  /** [start]: a contract's read; runs again after a new login. [updated]: that contract's time,
   *  set when the answers are in. */
  int read(void (*start)(Session&), int64_t* updated);
  std::string request() const;
  void response(int status, const std::string& body);
  /** The next datagram (or a robot's next bytes) to send, left on the queue; false when there is none. */
  bool outgoing(std::string& datagram);
  void sent() { out_.pop_front(); }
  int result() { return robot ? robot->result() : !queue_.empty() ? SESSION_IN_PROGRESS : result_; }
  std::string error() { return robot ? robot->error() : error_; }

 private:
  struct Request { std::string method, path, body; Answer then; bool optional; };
  void ask(const char* method, const std::string& path, const std::string& body, Answer then, bool optional);
  void next();

  std::string base_;   // http: scheme://host:port
  std::deque<Request> queue_;
  std::deque<std::string> out_;
  void (*start_)(Session&) = nullptr;
  int64_t* updated_ = nullptr;
  int64_t heard_ = 0;
  bool loggingIn_ = false, retried_ = false;
  int result_ = SESSION_OK;
  std::string error_;
};

/** [s] into a caller's buffer as the C API promises: cut to [size], the full length returned. */
int32_t copyOut(const std::string& s, char* buf, int32_t size);
/** A time for the JSON: "null" for 0. */
std::string timeJson(int64_t ms);
/** A server's time text ("2026-11-22T15:30:00+01:00", "...Z", "....123Z"; without a zone: UTC)
 *  in ms since 1970; 0 for anything else. */
int64_t timeMs(const std::string& when);
/** [ms] since 1970 as "2026-11-22T14:30:00Z". */
std::string timeText(int64_t ms);

}  // namespace agrigate

struct session : agrigate::Session {
  using agrigate::Session::Session;
};
