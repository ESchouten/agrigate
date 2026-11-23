// What a live driver is built from (core/live_c.h is the same seen from a host): per make of
// robot, how to keep the robot handing on its messages and how a message says which animal is
// there. A driver is one file in drivers/<name>/src; it adds itself to kLiveDrivers in Drivers.cpp.
#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace robot {
namespace live {

class Session;

struct Driver {
  const char* id;     // stored with a robot: never change it
  const char* name;   // the make / protocol
  int port;           // the robot's UDP port
  /** Called every ~100 ms: queue what has to be sent now (Session::send). */
  void (*tick)(Session&, int64_t now);
  /** A datagram from the robot. */
  void (*incoming)(Session&, const std::string& datagram, int64_t now);
  /** Queue the goodbye, if there is one. */
  void (*stop)(Session&);
};

extern const Driver* const kDrivers[];
extern const int kDriverCount;

struct Animal {
  std::string number, name, responder;
  int64_t since = 0;
};

struct Event {
  int64_t seq = 0, time = 0;
  std::string kind;
  Animal animal;
};

class Session {
 public:
  Session(const Driver& d, std::string a) : driver(d), address(std::move(a)) {}

  const Driver& driver;
  const std::string address;
  /** For the driver's own bookkeeping (when it last registered, ...). */
  int64_t sentAt = 0;

  void send(const std::string& datagram) { out_.push_back(datagram); }
  /** The robot said something (it is there and hands on its messages). */
  void heard(int64_t now) { lastMessage_ = now; }
  /** The robot identified an animal. The same animal again within 5 s is the same arrival. */
  void arrival(const Animal& a, int64_t now);

  // the C API
  /** The next datagram to send (left on the queue), false when there is none. */
  bool next(std::string& datagram) const;
  void pop() { if (!out_.empty()) out_.pop_front(); }
  std::string json(int64_t now) const;
  std::string events(int64_t after) const;

 private:
  std::deque<std::string> out_;
  std::deque<Event> events_;
  int64_t seq_ = 0, lastMessage_ = 0;
  Animal animal_;
  bool hasAnimal_ = false;
};

}  // namespace live
}  // namespace robot

struct live_session : robot::live::Session {
  using robot::live::Session::Session;
};
