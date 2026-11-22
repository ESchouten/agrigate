// JSON as the servers answer it, read into a tree, and text quoted for the JSON we give back.
// Small and without dependencies, so it also builds for a microcontroller. A missing key or
// index gives a null value, so a driver reads a["data"]["name"].str() without checking each step.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace robot {

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  std::string text;   // a string's text; a number or boolean as written
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> members;

  /** Null (and [ok] false) for text that is not JSON. */
  static Json parse(const std::string& text, bool* ok = nullptr);

  const Json& operator[](const char* key) const;
  const Json& at(size_t index) const;
  bool isNull() const { return type == Null; }
  /** A string, number or boolean as text; "" for anything else. */
  const std::string& str() const;
  double num(double fallback = -1) const;
};

/** [s] as a JSON string, quotes included. */
std::string quote(const std::string& s);

}  // namespace robot
