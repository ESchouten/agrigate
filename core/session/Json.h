// JSON as the servers answer it, read into a tree, and text quoted for the JSON we give back.
// Small and without dependencies, so it also builds for a microcontroller. A missing key or
// index gives a null value, so a driver reads a["data"]["name"].str() without checking each step.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace agrigate {

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  std::string text;   // a string's text; a number or boolean as written
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> members;

  /** Null (and [ok] false) for text that is not JSON. */
  static Json parse(const std::string& text, bool* ok = nullptr);

  /** The member [key]; exactly as written, else ignoring ASCII case. */
  const Json& operator[](const char* key) const;
  const Json& at(size_t index) const;
  bool isNull() const { return type == Null; }
  /** A string, number or boolean as text; "" for anything else. */
  const std::string& str() const;
  double num(double fallback = -1) const;
};

/** Adds the character [codepoint] to [out] as UTF-8. */
void utf8(std::string& out, unsigned codepoint);

/** [s] as a JSON string, quotes included. */
std::string quote(const std::string& s);

/** [v] as a JSON number; null for a figure that is not known (below 0). */
std::string number(double v);

}  // namespace agrigate
