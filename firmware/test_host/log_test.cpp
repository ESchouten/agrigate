// Host test for the M5's log (Log.h): what a reader gets back, also after the log wrapped.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/Log.h"

static std::string readAll(const LogRing& r, uint32_t& pos) {
  std::string s;
  char b[100];
  while (size_t n = r.read(pos, b, sizeof b)) s.append(b, n);
  return s;
}

int main() {
  static LogRing r;
  r.magic = 0;
  r.begin();
  assert(r.end == 0);
  r.add("one\n", 4);
  r.begin();   // after a restart: kept
  r.add("two\n", 4);
  uint32_t pos = r.firstLine();
  assert(readAll(r, pos) == "one\ntwo\n" && pos == 8);
  assert(readAll(r, pos).empty());   // a live reader waits for the next line
  r.add("three\n", 6);
  assert(readAll(r, pos) == "three\n");

  std::string line(99, 'x');   // 99 + "\n" = 100 bytes a line: 4096 wraps after 40.96 lines
  for (int i = 0; i < 100; i++) {
    std::string l = std::to_string(i % 10) + line.substr(1) + "\n";
    r.add(l.c_str(), l.size());
  }
  pos = r.firstLine();
  std::string all = readAll(r, pos);
  assert(all.size() <= LogRing::kSize && all.size() > LogRing::kSize - 100);
  assert(all[0] != 'x' && all.back() == '\n' && all[all.size() - 100] == '9');   // whole lines, the newest last
  uint32_t old = 0;   // a reader that fell behind gets the oldest kept byte on
  assert(readAll(r, old).size() == LogRing::kSize);
  printf("ok   log\n");
}
