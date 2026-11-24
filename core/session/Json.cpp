#include "Json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace agrigate {
namespace {

const Json kNull;
const std::string kEmpty;

struct Parser {
  const char* p;
  const char* end;
  bool ok = true;

  void space() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
  bool take(char c) { space(); if (p < end && *p == c) { p++; return true; } return false; }
  bool word(const char* w) { size_t n = strlen(w); if ((size_t)(end - p) >= n && !memcmp(p, w, n)) { p += n; return true; } return false; }

  bool hex4(unsigned& c) {
    if (end - p < 4) return false;
    char b[5] = {p[0], p[1], p[2], p[3], 0};
    char* e;
    c = (unsigned)strtoul(b, &e, 16);
    p += 4;
    return e == b + 4;
  }
  std::string string() {   // after the opening quote
    std::string o;
    while (p < end && *p != '"') {
      char c = *p++;
      if (c != '\\') { o += c; continue; }
      if (p >= end) break;
      switch (c = *p++) {
        case 'n': o += '\n'; break;
        case 't': o += '\t'; break;
        case 'r': o += '\r'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case 'u': {
          unsigned u, low;
          if (!hex4(u)) { ok = false; return o; }
          if (u >= 0xD800 && u < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {   // a pair
            p += 2;
            if (!hex4(low)) { ok = false; return o; }
            u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
          }
          utf8(o, u);
          break;
        }
        default: o += c;   // \" \\ \/
      }
    }
    if (p >= end) ok = false; else p++;
    return o;
  }
  Json value(int depth) {
    Json j;
    space();
    if (p >= end || depth > 64) { ok = false; return j; }
    if (*p == '{') {
      p++;
      j.type = Json::Object;
      if (take('}')) return j;
      do {
        if (!take('"')) { ok = false; return j; }
        std::string key = string();
        if (!take(':')) { ok = false; return j; }
        j.members.emplace_back(std::move(key), value(depth + 1));
      } while (ok && take(','));
      if (!take('}')) ok = false;
    } else if (*p == '[') {
      p++;
      j.type = Json::Array;
      if (take(']')) return j;
      do j.items.push_back(value(depth + 1)); while (ok && take(','));
      if (!take(']')) ok = false;
    } else if (*p == '"') {
      p++;
      j.type = Json::String;
      j.text = string();
    } else if (word("true") || word("false")) {
      j.type = Json::Bool;
      j.text = p[-2] == 'u' ? "true" : "false";
    } else if (!word("null")) {
      const char* from = p;
      while (p < end && *p && (strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) p++;
      if (p == from) { ok = false; return j; }
      j.type = Json::Number;
      j.text.assign(from, p);
    }
    return j;
  }
};

}  // namespace

Json Json::parse(const std::string& text, bool* ok) {
  Parser parser{text.data(), text.data() + text.size()};
  Json j = parser.value(0);
  parser.space();
  bool good = parser.ok && parser.p == parser.end;
  if (ok) *ok = good;
  return good ? j : Json();
}

const Json& Json::operator[](const char* key) const {
  for (const auto& m : members) if (m.first == key) return m.second;
  // servers differ in case for the same field (T4C's description says "data", a real T4C answers
  // "Data"): a key not found as written is looked up again ignoring ASCII case
  for (const auto& m : members) {
    const char* k = key;
    size_t i = 0;
    for (; i < m.first.size() && k[i]; i++)
      if (tolower((unsigned char)m.first[i]) != tolower((unsigned char)k[i])) break;
    if (i == m.first.size() && !k[i]) return m.second;
  }
  return kNull;
}

const Json& Json::at(size_t index) const { return index < items.size() ? items[index] : kNull; }

const std::string& Json::str() const { return type == String || type == Number || type == Bool ? text : kEmpty; }

double Json::num(double fallback) const { return type == Number ? strtod(text.c_str(), nullptr) : fallback; }

void utf8(std::string& o, unsigned c) {
  if (c < 0x80) o += (char)c;
  else if (c < 0x800) { o += (char)(0xC0 | c >> 6); o += (char)(0x80 | (c & 0x3F)); }
  else if (c < 0x10000) { o += (char)(0xE0 | c >> 12); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
  else { o += (char)(0xF0 | c >> 18); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}

std::string quote(const std::string& s) {
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
        else o += c;
    }
  }
  return o + "\"";
}

}  // namespace agrigate
