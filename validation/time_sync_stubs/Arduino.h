#pragma once
#include <cstdint>
#include <string>
using String = std::string;
inline uint32_t fakeMillis = 0;
inline uint32_t millis() { return fakeMillis; }
struct FakeSerial {
  void println(const char *) {}
  template <class... Args> void printf(const char *, Args...) {}
};
inline FakeSerial Serial;
inline unsigned ntpStarts = 0;
inline std::string primaryServer, backupServer;
inline void configTzTime(const char *, const char *first, const char *second) {
  ++ntpStarts;
  primaryServer = first;
  backupServer = second;
}
