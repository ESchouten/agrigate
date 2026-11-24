// Astronaut milking robots: which cow is in the robot's box, from the robot's own messages (LFW).
// Serves "identity" (core/identity_c.h): the robot is one place.
//
// The robot's own computer runs a proxy on UDP port 6000 that hands every internal LFW message to the clients
// registered with it (the farm software T4C registers the same way). This driver registers, renews
// that every 2 s and reads one message: "cow number and name", which the robot sends when it has
// read a cow's responder at the start of a visit. It never sends an LFW message itself.
// See ../API.md for the format and what was checked on real robots.
#include <cstring>

#include "Session.h"

namespace agrigate {
namespace {

constexpr uint32_t kMagicMessage = 0xD156;    // a message the proxy hands on
constexpr uint32_t kMagicClient = 0xD157;     // a client (de)registering
constexpr uint16_t kCowNumberName = 0x116;    // MSG_REQUEST_COW_NR_NAME: responder, cow number, name
constexpr int64_t kRenewMs = 2000;
const char kClientName[] = "agrigate";  // how the proxy lists this client (22 bytes at most)

uint32_t u32(const std::string& d, size_t at) {
  return (uint32_t)(uint8_t)d[at] | (uint32_t)(uint8_t)d[at + 1] << 8 | (uint32_t)(uint8_t)d[at + 2] << 16 | (uint32_t)(uint8_t)d[at + 3] << 24;
}
uint16_t u16(const std::string& d, size_t at) { return (uint16_t)((uint8_t)d[at] | (uint8_t)d[at + 1] << 8); }

std::string registration(bool on) {
  std::string p(4 + 2 + 22, '\0');
  p[0] = (char)(kMagicClient & 0xFF); p[1] = (char)(kMagicClient >> 8 & 0xFF);
  p[4] = on ? 1 : 0;
  memcpy(&p[6], kClientName, strlen(kClientName));
  return p;
}

// UTF-16LE up to the first NUL, as UTF-8
std::string text(const std::string& d, size_t from) {
  std::string o;
  for (size_t i = from; i + 1 < d.size(); i += 2) {
    unsigned c = u16(d, i);
    if (!c) break;
    if (c >= 0xD800 && c < 0xDC00 && i + 3 < d.size()) {   // a pair
      unsigned low = u16(d, i + 2);
      if (low >= 0xDC00 && low < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00); i += 2; }
    }
    utf8(o, c);
  }
  while (!o.empty() && o.back() == ' ') o.pop_back();
  return o;
}

identity::Place& box(Session& s) { return s.identity.place(s.address, identity::kind::MilkingRobot); }

void tick(Session& s) {
  box(s);   // the robot is a place from the start, also while it says nothing
  if (s.now - s.sentAt < kRenewMs) return;
  s.send(registration(true));
  s.sentAt = s.now;
}

void incoming(Session& s, const std::string& d) {
  // <u32 magic 0xD156> <u16 message> <u16 sender> <u16 destination> <u16 length> data
  if (d.size() < 12 || u32(d, 0) != kMagicMessage) return;
  s.heard();
  s.identity.updated = s.now;
  const uint16_t id = u16(d, 4), length = u16(d, 10);
  if (id != kCowNumberName || length < 12 || d.size() < 12u + length) return;
  const std::string data = d.substr(12, length);
  const uint32_t responder = u32(data, 0), number = u32(data, 4);
  identity::Animal a;
  a.name = text(data, 8);
  if (a.name.empty() || responder == 0 || responder >= 100000000) return;   // not a cow
  a.responder = std::to_string(responder);
  a.number = std::to_string(number);
  s.identity.identified(box(s), a, s.now);
}

void stop(Session& s) { s.send(registration(false)); }

}  // namespace

extern const Driver astronaut = [] {
  Driver d{"astronaut", "Astronaut", {"udp", nullptr, 6000}};
  d.identity = [](Session&) {};   // the robot tells by itself: nothing to ask
  d.tick = tick;
  d.incoming = incoming;
  d.stop = stop;
  return d;
}();

}  // namespace agrigate
