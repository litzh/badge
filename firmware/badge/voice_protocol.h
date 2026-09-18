#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <math.h>

namespace VoiceProtocol {
inline uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
inline void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
inline void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
inline void wavHeader(uint8_t *p, size_t pcmBytes) {
  memcpy(p, "RIFF", 4); put32(p + 4, pcmBytes + 36); memcpy(p + 8, "WAVEfmt ", 8);
  put32(p + 16, 16); put16(p + 20, 1); put16(p + 22, 1);
  put32(p + 24, 16000); put32(p + 28, 32000); put16(p + 32, 2); put16(p + 34, 16);
  memcpy(p + 36, "data", 4); put32(p + 40, pcmBytes);
}
struct Wav { size_t offset = 0, bytes = 0; };
struct Channels {
  int64_t sum[2] = {};
  uint64_t squares[2] = {};
  size_t frames = 0;
  void add(const int16_t *stereo, size_t count) {
    for (size_t i = 0; i < count * 2; ++i) {
      int64_t sample = stereo[i];
      sum[i % 2] += sample; squares[i % 2] += sample * sample;
    }
    frames += count;
  }
  double rms(int channel) const {
    if (!frames) return 0;
    double mean = double(sum[channel]) / frames;
    return sqrt(std::max(0.0, double(squares[channel]) / frames - mean * mean));
  }
  int selected() const { return rms(1) > rms(0) ? 1 : 0; }
};
// Complete-file parser; supports RIFF ancillary chunks and odd-byte padding.
inline bool wav(const uint8_t *p, size_t size, Wav &out) {
  if (size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return false;
  uint64_t end = uint64_t(u32(p + 4)) + 8;
  if (end > size || end < 12) return false;
  bool fmt = false, data = false;
  Wav found;
  for (size_t pos = 12; pos + 8 <= end;) {
    uint32_t len = u32(p + pos + 4);
    size_t start = pos + 8;
    if (uint64_t(start) + len > end) return false;
    if (!memcmp(p + pos, "fmt ", 4)) {
      if (fmt || len < 16 || u16(p + start) != 1 || u16(p + start + 2) != 1 ||
          u32(p + start + 4) != 16000 || u32(p + start + 8) != 32000 ||
          u16(p + start + 12) != 2 || u16(p + start + 14) != 16) return false;
      fmt = true;
    } else if (!memcmp(p + pos, "data", 4)) {
      if (data || !len || len % 2) return false;
      data = true; found.offset = start; found.bytes = len;
    }
    pos = start + len + (len & 1);
    if (pos > end) return false;
  }
  if (!fmt || !data) return false;
  out = found;
  return true;
}
inline size_t utf8Length(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s) if ((c & 0xc0) != 0x80) ++n;
  return n;
}
inline std::string spoken(std::string s) {
  for (auto markers : {std::pair<const char *, const char *>("<think>", "</think>"),
                       {"", ""}, {"", ""}}) {
    size_t begin;
    while ((begin = s.find(markers.first)) != std::string::npos) {
      size_t end = s.find(markers.second, begin + strlen(markers.first));
      s.erase(begin, end == std::string::npos ? std::string::npos : end + strlen(markers.second) - begin);
    }
  }
  // Drop citation markers and URLs; retain readable Markdown link labels.
  for (size_t pos = 0; (pos = s.find("](", pos)) != std::string::npos;) {
    size_t begin = s.rfind('[', pos), end = s.find(')', pos + 2);
    if (begin != std::string::npos && end != std::string::npos) {
      s.erase(pos, end - pos + 1); s.erase(begin, 1); pos = begin;
    } else ++pos;
  }
  for (const char *prefix : {"http://", "https://"}) {
    size_t pos;
    while ((pos = s.find(prefix)) != std::string::npos) {
      size_t end = pos;
      while (end < s.size() && static_cast<unsigned char>(s[end]) > 32 &&
             static_cast<unsigned char>(s[end]) < 127) ++end;
      s.erase(pos, end - pos);
    }
  }
  for (size_t pos = 0; pos < s.size();) {
    if (s[pos] == '[') {
      size_t end = s.find(']', pos + 1);
      if (end != std::string::npos && end > pos + 1 &&
          s.substr(pos + 1, end - pos - 1).find_first_not_of("0123456789,- ") == std::string::npos) {
        s.erase(pos, end - pos + 1); continue;
      }
    }
    if (s[pos] == '`' || s[pos] == '*') { s.erase(pos, 1); continue; }
    if (s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t') s[pos] = ' ';
    ++pos;
  }
  auto a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
} // namespace VoiceProtocol
