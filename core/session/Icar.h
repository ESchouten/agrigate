// The lists of the ICAR Animal Data Exchange standard (icar.org) that farm servers offer: an
// animal's events (calvings, inseminations, ...), each naming its animal by an ICAR identifier.
// For the drivers whose server has them (drivers/t4c, drivers/fullsense).
#pragma once

#include <map>
#include <memory>
#include <string>

#include "Session.h"

namespace agrigate {
namespace icar {

/** A herd being read: each animal by the id its server gives it in ICAR (a life number on one
 *  server, its own id on another), and the sires' names, if the server lists its sires. */
struct Herd {
  std::map<std::string, size_t> byId;   // -> Session::cow.animals
  std::map<std::string, std::string> sireByCode, sireByLife;
  /** The animal an ICAR identifier ({"id", "scheme"}) names; null for one not in the herd. */
  cow::Animal* animal(Session& s, const Json& identifier) const;
};

/** The items of an ICAR list: {"member": [...]} as the standard has it, or a server's own
 *  envelope {"data": [...]}. */
const Json& items(const Json& list);

/** The id of the identifier with [scheme] in a list of identifiers; "" if there is none. */
std::string idOf(const Json& identifiers, const char* scheme);

/** Asks for the calvings, inseminations, pregnancy checks, heats, dry-offs and do-not-breeds of
 *  [h]'s animals at [path] + "<list>" + [query]. A list the server does not give is left out. */
void readEvents(Session& s, std::shared_ptr<Herd> h, const std::string& path, const std::string& query);

}  // namespace icar
}  // namespace agrigate
