#include "../firmware/badge/voice_protocol.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace VoiceProtocol;
int main() {
  std::vector<uint8_t> file(44 + 3200);
  wavHeader(file.data(), 3200);
  Wav parsed;
  assert(wav(file.data(), file.size(), parsed));
  assert(parsed.offset == 44 && parsed.bytes == 3200);
  assert(!wav(file.data(), file.size() - 1, parsed));
  auto unsupported = file;
  put32(unsupported.data() + 24, 32000);
  assert(!wav(unsupported.data(), unsupported.size(), parsed));
  unsupported = file; put16(unsupported.data() + 22, 2);
  assert(!wav(unsupported.data(), unsupported.size(), parsed));
  unsupported = file; put32(unsupported.data() + 40, 0xffffffff);
  assert(!wav(unsupported.data(), unsupported.size(), parsed));
  // An odd-length ancillary chunk between fmt and data.
  file.insert(file.begin() + 36, {'J','U','N','K', 1,0,0,0, 42,0});
  put32(file.data() + 4, file.size() - 8);
  assert(wav(file.data(), file.size(), parsed) && parsed.offset == 54);
  assert(utf8Length("你好，ESP32！") == 9);
  assert(spoken("**答案**见[官网](https://example.org)。[1]") == "答案见官网。");
  assert(spoken("你好。 https://example.org/你好") == "你好。 你好");
  assert(spoken("你好。citesource") == "你好。");
  Channels channels;
  const int16_t test[] = {30000,100,30000,-100,30000,200,30000,-200};
  channels.add(test, 4);
  assert(channels.selected() == 1 && channels.rms(0) == 0);
  assert(channels.rms(1) > 150 && channels.rms(1) < 160);
  std::cout << "PASS WAV bounds, PCM format, ancillary chunks, UTF-8 length, spoken text cleanup\n";
}
