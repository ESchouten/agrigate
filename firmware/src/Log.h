// The M5's log, no hardware code (tested on a PC): the last few KB of what it prints, for
// GET /api/log and /api/log/live (docs/API.md). main.cpp keeps it in memory a restart does not
// wipe (RTC), so after a crash the log still shows what led up to it.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

struct LogRing {
  static constexpr uint32_t kSize = 4096, kMagic = 0x4c4f4731;
  uint32_t magic, end;   // end: bytes written since the log was cleared; byte i is at buf[i % kSize]
  char buf[kSize];

  // Clears it unless it holds a log already (after a restart; power off leaves noise).
  void begin() {
    if (magic != kMagic) {
      magic = kMagic;
      end = 0;
    }
  }

  void add(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) buf[(end + i) % kSize] = s[i];
    end += uint32_t(n);
  }

  // Where the oldest whole line starts.
  uint32_t firstLine() const {
    if (end <= kSize) return 0;
    for (uint32_t p = end - kSize; p != end; p++)
      if (buf[p % kSize] == '\n') return p + 1;
    return end - kSize;
  }

  // Copies up to max bytes from pos into out and moves pos past them. A pos the log has
  // already written over moves up to the oldest byte still kept.
  size_t read(uint32_t& pos, char* out, size_t max) const {
    if (end - pos > kSize) pos = end - kSize;
    size_t n = std::min<size_t>(end - pos, max);
    for (size_t i = 0; i < n; i++) out[i] = buf[(pos + i) % kSize];
    pos += uint32_t(n);
    return n;
  }
};
