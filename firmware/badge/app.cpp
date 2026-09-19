// Badge firmware for ESP32-S3-Touch-AMOLED-1.75C.
// Ported from the RLCD project: Wi-Fi provisioning (NVS / build-time default /
// BLE), HTTP API (GET /status, POST /echo, display brightness) and a status
// screen. The display is a round 466x466 AMOLED: the corners are not visible,
// so every row is clipped to the chord width at its y position.
#include "config.h"
#include "board_io.h"
#include "screensaver_state.h"
#include "microphone.h"
#include "visualizer.h"
#include "display_frame.h"
#include "time_sync.h"
#include "voice.h"
#include "interaction.h"
#include "local_tools.h"
#include "device_tool_protocol.h"
#include "shutdown_state.h"
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <cJSON.h>
#include <atomic>
#include <math.h>

namespace {

constexpr char FIRMWARE_VERSION[] = "badge-0.9.0";

// Round-screen geometry: 466x466 panel, visible area is the inscribed circle.
constexpr int16_t SCREEN = 466;
constexpr int16_t CENTER = SCREEN / 2;
constexpr int16_t RADIUS = SCREEN / 2;
constexpr int16_t EDGE_MARGIN = 12;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1,
                                             LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx =
    new Arduino_CO5300(bus, LCD_RESET, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

WebServer http(80);
Preferences prefs;
Preferences displayPrefs;
bool storageOK = false, displayStorageOK = false;
uint8_t brightness = 160;
String displayError;
ScreensaverState screensaver;
BatteryPolicy batteryPolicy;
ShutdownState shutdown;
bool panelSleeping = false;
const char *powerOffStatus = "idle";
uint32_t lastPowerOffAttempt = 0;
bool powerOffAttempted = false;
bool screenDirty = true;
bool statusNeedsClear = true;
Arduino_Canvas statusRow(SCREEN, 24, nullptr);
Arduino_Canvas buttonCanvas(280, 64, nullptr);
bool statusRowReady = false;
bool buttonCanvasReady = false;
uint32_t lastSaverFrame = 0;
bool voiceWasBusy = false;
using BadgeUI::Page;
using BadgeUI::Control;
Page page = Page::Home;
BadgeUI::Touch touch;
BadgeUI::BootButton bootButton;
BadgeUI::VoiceAction touchVoiceAction = BadgeUI::VoiceAction::Start;
Control feedbackButton = Control::None;
uint32_t feedbackUntil = 0, noticeUntil = 0;
String notice;

String ascii(const String &s);
bool controlEnabled(Control id);
void activateControl(Control id);
void performVoiceAction(BadgeUI::VoiceAction action);
void showPage(Page next) {
  page = next; touch.cancel(); feedbackButton = Control::None;
  statusNeedsClear = screenDirty = true;
}
void showNotice(const String &text) { notice = text; noticeUntil = millis() + 2500; screenDirty = true; }

uint8_t effectiveBrightness() {
  uint8_t value = screensaver.effectiveBrightness(brightness);
  return batteryPolicy.conserve() && value > 64 ? 64 : value;
}

void sleepPanel() {
  microphoneSetEnabled(false);
  boardSetVisualActive(false);
  if (!panelSleeping) {
    gfx->setBrightness(0);
    gfx->displayOff();
    panelSleeping = true;
  }
}

void userActivity() {
  if (batteryPolicy.shutdownDue) return;
  bool wasActive = screensaver.active || screensaver.screenOff;
  screensaver.activity(millis());
  if (panelSleeping) {
    gfx->displayOn();
    panelSleeping = false;
  }
  if (wasActive)
    gfx->setBrightness(effectiveBrightness());
  if (wasActive) {
    showPage(Page::Home);
    statusNeedsClear = true;
    microphoneSetEnabled(false);
    boardSetVisualActive(false);
  }
  screenDirty = screenDirty || wasActive;
}

void addDisplayState(cJSON *j) {
  cJSON_AddNumberToObject(j, "brightness", brightness);
  cJSON_AddNumberToObject(j, "effective_brightness", effectiveBrightness());
  cJSON_AddNumberToObject(j, "screensaver_brightness_limit", ScreensaverState::BRIGHTNESS_LIMIT);
  cJSON_AddBoolToObject(j, "screensaver", screensaver.active);
  cJSON_AddBoolToObject(j, "screen_off", screensaver.screenOff);
  cJSON_AddBoolToObject(j, "manual_off", screensaver.manualOff);
  cJSON_AddStringToObject(j, "page", page == Page::Home ? "home" : page == Page::Settings ? "settings" : "info");
  cJSON_AddBoolToObject(j, "touch_button_pressed", touch.captured != Control::None);
  cJSON_AddStringToObject(j, "mode", screensaver.screenOff ? "off" : screensaver.active ? "screensaver" : "status");
  cJSON_AddNumberToObject(j, "screensaver_max_display_ms", ScreensaverState::MAX_DISPLAY_MS);
  cJSON_AddNumberToObject(j, "idle_ms", uint32_t(millis() - screensaver.lastActivity));
  cJSON_AddNumberToObject(j, "screensaver_timeout_ms", ScreensaverState::TIMEOUT_MS);
}

void addBatteryManagement(cJSON *b) {
  cJSON_AddBoolToObject(b, "low", batteryPolicy.low);
  cJSON_AddBoolToObject(b, "shutdown_pending", batteryPolicy.cutoffPending);
  cJSON_AddStringToObject(b, "shutdown_status", powerOffStatus);
  cJSON_AddNumberToObject(b, "low_voltage_v", BatteryPolicy::LOW_MV / 1000.0);
  cJSON_AddNumberToObject(b, "shutdown_voltage_v", BatteryPolicy::CUTOFF_MV / 1000.0);
  cJSON_AddNumberToObject(b, "shutdown_confirm_ms", BatteryPolicy::CONFIRM_MS);
  cJSON_AddStringToObject(b, "percent_source", "pmu_estimate");
}

void batteryTick() {
  bool wasConserving = batteryPolicy.conserve();
  batteryPolicy.update(boardBatterySample(), millis());
  if (wasConserving != batteryPolicy.conserve()) {
    screenDirty = true;
    if (!panelSleeping) gfx->setBrightness(effectiveBrightness());
  }
  if (!batteryPolicy.shutdownDue) {
    powerOffAttempted = false;
    powerOffStatus = "idle";
    return;
  }
  screensaver.active = false;
  screensaver.screenOff = true;
  shutdown.cancel("low_battery_shutdown");
  voiceCancel();
  sleepPanel();
  if (!powerOffAttempted || uint32_t(millis() - lastPowerOffAttempt) >= 5000) {
    powerOffAttempted = true;
    lastPowerOffAttempt = millis();
    PowerOffResult result = boardPowerOffIfLow(BatteryPolicy::CUTOFF_MV);
    powerOffStatus = result == PowerOffResult::Requested ? "requested" :
                     result == PowerOffResult::WriteFailed ? "write_failed" : "cancelled";
    Serial.printf("BATTERY protective shutdown: %s\n", powerOffStatus);
  }
}

bool bleEnabled = false, bleReady = false;
BLECharacteristic *bleStatus;
std::atomic<bool> restartAdvertising{false};
std::atomic<int> lastWifiReason{0};
std::atomic<bool> retryRequested{false};
struct Packet {
  char data[1024];
};
QueueHandle_t commands;

String ssid, password, source, phase = "starting", provisionResult = "idle";
String message;
bool connecting = false, candidate = false;
uint32_t connectStarted, lastConnectAttempt, lastScreen = 0;
uint32_t lastPwrPressHandled = 0;

String jsonText(cJSON *json) {
  char *raw = cJSON_PrintUnformatted(json);
  String result = raw ? raw : "{}";
  cJSON_free(raw);
  cJSON_Delete(json);
  return result;
}

cJSON *parseObject(const String &body) {
  // cJSON strings are NUL terminated; reject embedded NUL instead of truncating input.
  for (size_t i = 0; i < body.length(); ++i) {
    if (body[i] == '\0')
      return nullptr;
    if (body[i] == '\\') {
      if (body.substring(i, i + 6) == "\\u0000")
        return nullptr;
      ++i;
    }
  }
  const char *end = nullptr;
  cJSON *root = cJSON_ParseWithLengthOpts(body.c_str(), body.length() + 1, &end, true);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return nullptr;
  }
  return root;
}

// ---------------------------------------------------------------------------
// Round-screen text: clip every row to the chord width so nothing lands in
// the invisible corners. Default GFX font is 6x8 px scaled by `size`.
// ---------------------------------------------------------------------------
int maxCharsForRow(int16_t y, uint8_t size) {
  int16_t h = 8 * size;
  int dy = max(abs(y - CENTER), abs(y + h - CENTER));
  if (dy >= RADIUS - EDGE_MARGIN)
    return 0;
  float half = sqrtf((float)RADIUS * RADIUS - (float)dy * dy) - EDGE_MARGIN;
  return (int)(2 * half) / (6 * size);
}

void drawRow(int16_t y, const String &text, uint16_t color, uint8_t size) {
  int maxChars = maxCharsForRow(y, size);
  if (maxChars <= 0)
    return;
  String clipped = (int)text.length() > maxChars ? text.substring(0, maxChars) : text;
  struct CachedRow { int16_t y = -1; String text; uint16_t color = 0; uint8_t size = 0; };
  static CachedRow rows[32];
  CachedRow *cached = nullptr;
  for (auto &row : rows) {
    if (row.y == y || row.y == -1) { cached = &row; break; }
  }
  if (cached && !statusNeedsClear && cached->y == y && cached->text == clipped &&
      cached->color == color && cached->size == size) return;
  if (cached) { cached->y=y; cached->text=clipped; cached->color=color; cached->size=size; }
  int16_t x = CENTER - (int)clipped.length() * 6 * size / 2;
  if (statusRowReady) {
    statusRow.fillScreen(RGB565_BLACK);
    statusRow.setTextSize(size);
    statusRow.setTextColor(color);
    statusRow.setCursor(x, 0);
    statusRow.print(clipped);
    presentFrame(gfx, 0, y, statusRow.getFramebuffer(), SCREEN, 8 * size);
  } else {
    gfx->fillRect(0, y, SCREEN, 8 * size, RGB565_BLACK);
    gfx->setTextSize(size);
    gfx->setTextColor(color);
    gfx->setCursor(x, y);
    gfx->print(clipped);
  }
}

String voiceTitle() {
  if (shutdown.phase == ShutdownState::Phase::Countdown) return String("POWER OFF IN ") + shutdown.remaining(millis());
  String state = voiceLabel();
  if (state == "recording") return "LISTENING";
  if (state == "recognizing") return "RECOGNIZING";
  if (state == "thinking" || state == "shortening") return "THINKING";
  if (state == "reading_device") return "READING DEVICE";
  if (state == "setting_device") return "SETTING DEVICE";
  if (state == "synthesizing" || state == "downloading") return "PREPARING AUDIO";
  if (state == "playing") return "SPEAKING";
  if (state == "starting") return "STARTING";
  if (state == "error") return "TRY AGAIN";
  return "READY";
}

String voiceHint() {
  String error = voiceError();
  if (error.isEmpty()) return "Press BOOT to talk";
  if (error == "wifi_not_connected") return "Wi-Fi offline: open Settings";
  if (error == "waiting_for_ntp") return "Waiting for network time";
  if (error == "no_speech_recognized") return "No speech heard. Try again";
  if (error == "recording_too_short") return "Speak a little longer";
  if (error == "keys_or_prompt_missing") return "Voice setup missing";
  return "Voice failed: see Info";
}

void drawButton(const BadgeUI::Button &b, const String &label) {
  bool enabled = controlEnabled(b.id);
  bool pressed = enabled && (touch.captured == b.id || feedbackButton == b.id);
  uint16_t edge = !enabled ? 0x52AA : pressed ? RGB565_WHITE : RGB565_CYAN;
  uint16_t fill = !enabled ? 0x1082 : pressed ? 0x2576 : 0x1125;
  uint16_t ink = !enabled ? 0x7BEF : RGB565_WHITE;
  struct Cache { Page page; Control id = Control::None; String label; bool enabled = false, pressed = false; };
  static Cache cache[10];
  Cache *entry = nullptr;
  for (auto &item : cache) if (item.id == Control::None || (item.id == b.id && item.page == b.page)) { entry = &item; break; }
  if (entry && !statusNeedsClear && entry->id == b.id && entry->page == b.page && entry->label == label &&
      entry->enabled == enabled && entry->pressed == pressed) return;
  if (entry) { entry->page = b.page; entry->id = b.id; entry->label = label; entry->enabled = enabled; entry->pressed = pressed; }
  Arduino_GFX *target = buttonCanvasReady ? static_cast<Arduino_GFX *>(&buttonCanvas) : gfx;
  int x = buttonCanvasReady ? 0 : b.x, y = buttonCanvasReady ? 0 : b.y;
  if (buttonCanvasReady) buttonCanvas.fillScreen(RGB565_BLACK);
  target->fillRoundRect(x, y, b.w, b.h, 12, fill);
  target->drawRoundRect(x, y, b.w, b.h, 12, edge);
  target->drawRoundRect(x + 1, y + 1, b.w - 2, b.h - 2, 11, edge);
  target->setTextSize(2); target->setTextColor(ink);
  target->setCursor(x + (b.w - label.length() * 12) / 2, y + (b.h - 16) / 2);
  target->print(label);
  if (buttonCanvasReady) {
    // Canvas stride is 280; narrower buttons must be transferred row by row.
    auto *pixels = buttonCanvas.getFramebuffer();
    for (int row = 0; row < b.h; ++row) gfx->draw16bitRGBBitmap(b.x, b.y + row, pixels + row * 280, b.w, 1);
  }
}

void drawScreen() {
  if (statusNeedsClear) gfx->fillScreen(RGB565_BLACK);
  if (page == Page::Home) {
    drawRow(58, timeSyncDisplay(), RGB565_WHITE, 2);
    auto battery = boardBatterySample();
    String power = battery.percent >= 0 ? String(battery.percent) + "%" : "--%";
    if (battery.powerValid && battery.vbus) power += " USB";
    if (batteryPolicy.conserve()) power += " LOW";
    drawRow(94, String(WiFi.status() == WL_CONNECTED ? "Wi-Fi OK   " : "OFFLINE   ") + power, RGB565_YELLOW, 2);
    drawRow(156, voiceTitle(), voiceRecording() ? RGB565_RED : RGB565_CYAN, 3);
    String detail = shutdown.active() ? (shutdown.phase == ShutdownState::Phase::Waiting ? "Shutdown queued" : "Tap below to cancel") :
                    voiceRecording() ? String("Recording: ") + voiceRecordedMs() / 1000 + "s" :
                    !notice.isEmpty() ? notice : voiceBusy() ? "Please wait..." : voiceHint();
    drawRow(204, detail, RGB565_WHITE, 2);
    drawRow(234, shutdown.active() ? "BOOT: cancel shutdown" : voiceRecording() ? "BOOT: send" : voiceBusy() ? "BOOT: cancel" : "BOOT: start recording", RGB565_WHITE, 2);
    drawRow(420, "PWR: screen on/off", RGB565_WHITE, 1);
  } else if (page == Page::Settings) {
    drawRow(64, "SETTINGS", RGB565_CYAN, 3);
    drawRow(106, notice.isEmpty() ? "Touch a button" : notice, RGB565_WHITE, 2);
    drawRow(128, String("Volume: ") + voiceVolume() + (voiceVolume() == 0 ? " (muted)" : ""), RGB565_WHITE, 2);
    drawRow(224, String("Brightness: ") + brightness, RGB565_WHITE, 2);
  } else {
    drawRow(64, "DEVICE INFO", RGB565_CYAN, 3);
    drawRow(110, FIRMWARE_VERSION, RGB565_WHITE, 2);
    drawRow(148, "Wi-Fi: " + phase, RGB565_WHITE, 2);
    drawRow(180, "SSID: " + ascii(ssid), RGB565_WHITE, 2);
    drawRow(212, "IP: " + WiFi.localIP().toString(), RGB565_GREEN, 2);
    String bleName = "BADGE-" + WiFi.macAddress().substring(12); bleName.replace(":", "");
    drawRow(252, bleEnabled ? "BLE: " + bleName : "BLE provisioning off", RGB565_YELLOW, 2);
    drawRow(282, bleEnabled ? "Open provision.html" : "Enable in Settings", RGB565_WHITE, 2);
    drawRow(320, "Voice: " + voiceLabel(), RGB565_WHITE, 2);
    drawRow(350, voiceError().isEmpty() ? ascii(message) : voiceError(), RGB565_WHITE, 1);
  }
  for (const auto &b : BadgeUI::buttons) {
    if (b.page != page) continue;
    String label;
    switch (b.id) {
      case Control::Talk: label = shutdown.active() ? "Cancel shutdown" : voiceRecording() ? "Send question" : voiceBusy() ? "Cancel" : "Start talking"; break;
      case Control::Settings: label = "Settings"; break;
      case Control::VolumeDown: case Control::BrightnessDown: label = "-"; break;
      case Control::VolumeUp: case Control::BrightnessUp: label = "+"; break;
      case Control::Wifi: label = bleEnabled ? "Wi-Fi ON" : "Wi-Fi setup"; break;
      case Control::Info: label = "Info"; break;
      case Control::Back: label = "Back"; break;
      default: break;
    }
    drawButton(b, label);
  }
  statusNeedsClear = false;
  lastScreen = millis();
}

// Sparse moving lights on true black, with no static text or border. Erase
// only the previous small shapes so idle animation doesn't repaint 466x466.
void drawScreensaver(bool first) {
  uint32_t started = millis();
  if (visualizerDraw(gfx, first)) {
    lastSaverFrame = started;
    return;
  }
  constexpr int COUNT = 7;
  static int16_t oldX[COUNT], oldY[COUNT];
  static uint32_t frame = 0;
  if (first) {
    gfx->fillScreen(RGB565_BLACK);
  } else {
    for (int i = 0; i < COUNT; ++i)
      gfx->fillCircle(oldX[i], oldY[i], 7 + i % 3, RGB565_BLACK);
  }
  float t = frame++ * 0.1f;
  for (int i = 0; i < COUNT; ++i) {
    float angle = t * (0.13f + i * 0.017f) + i * 2.4f;
    float radius = 35.0f + 150.0f * (0.5f + 0.5f * sinf(t * 0.071f + i * 1.7f));
    oldX[i] = CENTER + (int16_t)(cosf(angle) * radius);
    oldY[i] = CENTER + (int16_t)(sinf(angle) * radius);
    uint8_t r = 45 + (uint8_t)(35 * (1 + sinf(t * 0.19f + i)));
    uint8_t g = 55 + (uint8_t)(40 * (1 + sinf(t * 0.17f + i + 2)));
    uint8_t b = 65 + (uint8_t)(40 * (1 + sinf(t * 0.11f + i + 4)));
    uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    gfx->fillCircle(oldX[i], oldY[i], 5 + i % 3, color);
  }
  lastSaverFrame = millis();
}

void displayTick() {
  bool speaking = voiceBusy();
  if (speaking || voiceWasBusy || shutdown.active()) screensaver.keepAwake(millis());
  if (!notice.isEmpty() && int32_t(millis() - noticeUntil) >= 0) { notice = ""; screenDirty = true; }
  if (feedbackButton != Control::None && int32_t(millis() - feedbackUntil) >= 0) {
    feedbackButton = Control::None; screenDirty = true;
  }
  voiceWasBusy = speaking;
  bool changed = screensaver.tick(millis(), batteryPolicy.conserve());
  if (screensaver.screenOff) {
    sleepPanel();
    return;
  }
  if (changed) {
    microphoneSetEnabled(true);
    boardSetVisualActive(true);
    gfx->setBrightness(effectiveBrightness());
    drawScreensaver(true);
    screenDirty = false;
  } else if (screensaver.active) {
    if (millis() - lastSaverFrame >= 67)
      drawScreensaver(false);
  } else if (screenDirty || millis() - lastScreen >= 250) {
    drawScreen();
    screenDirty = false;
  }
}

void updateBleStatus() {
  if (!bleReady)
    return;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "state", phase.c_str());
  cJSON_AddStringToObject(j, "result", provisionResult.c_str());
  cJSON_AddStringToObject(j, "ip",
                          WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "");
  cJSON_AddBoolToObject(j, "provisioning", bleEnabled);
  bleStatus->setValue(jsonText(j).c_str());
}

class InputCallbacks : public BLECharacteristicCallbacks {
  String buffer;
  bool overflow = false;

public:
  void onWrite(BLECharacteristic *ch) override {
    String chunk = ch->getValue();
    for (size_t i = 0; i < chunk.length(); ++i) {
      char c = chunk[i];
      if (c == '\n') {
        Packet p = {};
        if (overflow)
          strcpy(p.data, "{}");
        else
          buffer.toCharArray(p.data, sizeof(p.data));
        if (buffer.length() || overflow)
          xQueueSend(commands, &p, 0);
        buffer = "";
        overflow = false;
      } else if (buffer.length() < 1023 && !overflow && c != '\0')
        buffer += c;
      else
        overflow = true;
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onDisconnect(BLEServer *) override { restartAdvertising.store(true); }
};

void enableProvisioning() {
  if (!bleReady) {
    String name = "BADGE-" + WiFi.macAddress().substring(12);
    name.replace(":", "");
    BLEDevice::init(name.c_str());
    BLEServer *bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());
    BLEService *service = bleServer->createService(SERVICE_UUID);
    auto *input = service->createCharacteristic(WRITE_UUID, BLECharacteristic::PROPERTY_WRITE);
    input->setCallbacks(new InputCallbacks());
    bleStatus = service->createCharacteristic(STATUS_UUID, BLECharacteristic::PROPERTY_READ);
    service->start();
    BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
    BLEDevice::getAdvertising()->setScanResponse(true);
    bleReady = true;
  }
  bleEnabled = true;
  BLEDevice::startAdvertising();
  updateBleStatus();
  Serial.println("BLE provisioning enabled");
}

void connectWifi(const String &newSSID, const String &newPassword, bool isCandidate,
                 const String &newSource) {
  ssid = newSSID;
  password = newPassword;
  candidate = isCandidate;
  source = newSource;
  WiFi.STA.disconnect(false, 1000);
  // Arduino disconnect() is a no-op while connecting, so cancel at IDF level
  // before changing credentials (otherwise esp_wifi_set_config may fail).
  esp_wifi_disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());
  connecting = true;
  connectStarted = millis();
  phase = "connecting";
  lastConnectAttempt = connectStarted;
  retryRequested.store(false);
  if (candidate)
    provisionResult = "connecting";
  updateBleStatus();
  Serial.printf("WIFI connecting source=%s\n", source.c_str());
}

bool validCredentials(cJSON *j, String &s, String &p) {
  auto *a = cJSON_GetObjectItemCaseSensitive(j, "ssid");
  auto *b = cJSON_GetObjectItemCaseSensitive(j, "password");
  if (!cJSON_IsString(a) || !cJSON_IsString(b))
    return false;
  s = a->valuestring;
  p = b->valuestring;
  if (!s.length() || s.length() > 32)
    return false;
  if (!p.length())
    return true; // Open Wi-Fi
  if (p.length() >= 8 && p.length() <= 63)
    return true;
  if (p.length() != 64)
    return false;
  for (char c : p)
    if (!isxdigit(static_cast<unsigned char>(c)))
      return false;
  return true;
}

void networkTick() {
  Packet packet;
  if (xQueueReceive(commands, &packet, 0) == pdTRUE) {
    userActivity();
    cJSON *j = parseObject(packet.data);
    String s, p;
    if (!bleEnabled)
      provisionResult = "provisioning_closed";
    else if (connecting)
      provisionResult = "busy";
    else if (!j || !validCredentials(j, s, p))
      provisionResult = "invalid_credentials";
    else
      connectWifi(s, p, true, "provisioned");
    cJSON_Delete(j);
    updateBleStatus();
  }
  if (restartAdvertising.exchange(false) && bleEnabled)
    BLEDevice::startAdvertising();
  // Retry transient AP/auth failures inside the same bounded connection window.
  if (connecting && WiFi.status() != WL_CONNECTED && retryRequested.load() &&
      millis() - lastConnectAttempt >= 2000 && millis() - connectStarted < WIFI_TIMEOUT_MS) {
    retryRequested.store(false);
    lastConnectAttempt = millis();
    WiFi.STA.connect();
    Serial.println("WIFI retrying within connection deadline");
  }
  if (connecting && WiFi.status() == WL_CONNECTED) {
    connecting = false;
    phase = "connected";
    if (candidate) {
      // Single NVS value: the old pair remains intact until successful replacement.
      cJSON *j = cJSON_CreateObject();
      cJSON_AddStringToObject(j, "ssid", ssid.c_str());
      cJSON_AddStringToObject(j, "password", password.c_str());
      String saved = jsonText(j);
      bool ok = storageOK && prefs.putString("credentials", saved) == saved.length();
      provisionResult = ok ? "saved" : "save_failed";
      if (ok) {
        source = "saved";
        bleEnabled = false;
        BLEDevice::getAdvertising()->stop();
      }
    } else
      provisionResult = "idle";
    candidate = false;
    Serial.printf("WIFI connected ip=%s source=%s result=%s\n", WiFi.localIP().toString().c_str(),
                  source.c_str(), provisionResult.c_str());
    updateBleStatus();
  } else if (connecting && millis() - connectStarted >= WIFI_TIMEOUT_MS) {
    connecting = false;
    esp_wifi_disconnect();
    phase = "waiting_for_ble";
    if (candidate)
      provisionResult = "connection_failed";
    candidate = false;
    enableProvisioning();
    Serial.println("WIFI timeout; waiting for BLE provisioning");
  } else if (!connecting && phase == "connected" && WiFi.status() != WL_CONNECTED) {
    connectWifi(ssid, password, false, source);
  }
}

// ---------------------------------------------------------------------------
// HTTP API
// ---------------------------------------------------------------------------
cJSON *deviceToolSnapshot(uint32_t fields) {
  DeviceTool::Snapshot s;
  s.now = millis(); s.battery = boardBatterySample();
  auto motion = boardMotion();
  s.motionValid = motion.valid; s.motionAge = motion.sampleAgeMs;
  s.accel[0] = motion.ax; s.accel[1] = motion.ay; s.accel[2] = motion.az;
  s.gyro[0] = motion.gx; s.gyro[1] = motion.gy; s.gyro[2] = motion.gz;
  s.temperatureValid = motion.temperatureValid; s.temperatureAge = motion.temperatureAgeMs; s.temperatureC = motion.temperatureC;
  s.brightness = brightness; s.effectiveBrightness = effectiveBrightness();
  s.screenOff = screensaver.screenOff; s.screensaver = screensaver.active; s.manualOff = screensaver.manualOff;
  s.volume = voiceVolume(); s.connected = WiFi.status() == WL_CONNECTED;
  if (s.connected) s.rssi = WiFi.RSSI();
  return DeviceTool::reading(s, fields);
}
cJSON *shutdownStatus() {
  auto *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "state", shutdown.label());
  cJSON_AddNumberToObject(j, "remaining_seconds", shutdown.remaining(millis()));
  cJSON_AddNumberToObject(j, "delay_seconds", 10);
  cJSON_AddStringToObject(j, "countdown_starts", "after_reply_completed");
  cJSON_AddStringToObject(j, "reason", shutdown.reason);
  return j;
}
String statusJson() {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "firmware", FIRMWARE_VERSION);
  cJSON_AddItemToObject(j, "time_sync", timeSyncStatus());
  cJSON_AddNumberToObject(j, "uptime_seconds", millis() / 1000);
  cJSON_AddNumberToObject(j, "free_heap_bytes", ESP.getFreeHeap());
  cJSON_AddNumberToObject(j, "free_psram_bytes", ESP.getFreePsram());
  auto *w = cJSON_AddObjectToObject(j, "wifi");
  bool connected = WiFi.status() == WL_CONNECTED;
  cJSON_AddStringToObject(w, "state", phase.c_str());
  cJSON_AddStringToObject(w, "ssid", ssid.c_str());
  cJSON_AddStringToObject(w, "source", source.c_str());
  cJSON_AddNumberToObject(w, "last_disconnect_reason", lastWifiReason.load());
  cJSON_AddStringToObject(w, "ip", connected ? WiFi.localIP().toString().c_str() : "");
  if (connected)
    cJSON_AddNumberToObject(w, "rssi_dbm", WiFi.RSSI());
  else
    cJSON_AddNullToObject(w, "rssi_dbm");
  cJSON_AddBoolToObject(w, "provisioning", bleEnabled);
  cJSON_AddStringToObject(w, "provisioning_result", provisionResult.c_str());
  auto *battery = boardBatteryStatus();
  addBatteryManagement(battery);
  cJSON_AddItemToObject(j, "battery", battery);
  cJSON_AddItemToObject(j, "touch", boardTouchStatus());
  cJSON_AddItemToObject(j, "imu", boardImuStatus());
  cJSON_AddItemToObject(j, "buttons", boardButtonsStatus());
  addDisplayState(cJSON_AddObjectToObject(j, "display"));
  cJSON_AddItemToObject(j, "microphone", microphoneStatus());
  cJSON_AddItemToObject(j, "voice", voiceStatus());
  cJSON_AddItemToObject(j, "shutdown", shutdownStatus());
  cJSON_AddItemToObject(j, "visualizer", visualizerStatus());
  return jsonText(j);
}

String displayJson() {
  auto *j = cJSON_CreateObject();
  addDisplayState(j);
  cJSON_AddBoolToObject(j, "storage_ready", displayStorageOK);
  if (displayError.isEmpty())
    cJSON_AddNullToObject(j, "error");
  else
    cJSON_AddStringToObject(j, "error", displayError.c_str());
  return jsonText(j);
}

bool setBrightness(uint8_t value) {
  if (!displayStorageOK || (value != brightness && displayPrefs.putUChar("brightness", value) != 1)) {
    displayError = "display_save_failed";
    Serial.println("DISPLAY brightness save failed");
    return false;
  }
  brightness = value;
  userActivity();
  if (!panelSleeping) gfx->setBrightness(effectiveBrightness());
  displayError = "";
  return true;
}

cJSON *deviceToolExecute(const DeviceTool::Command &command) {
  using DeviceTool::Operation;
  if (command.operation == Operation::Read) return deviceToolSnapshot(command.fields);
  if (batteryPolicy.shutdownDue) return DeviceTool::error("low_battery_shutdown");
  if (command.operation == Operation::Shutdown) {
    auto progress = voiceProgress();
    if (!progress.busy || progress.cancelled || !shutdown.schedule(progress.turn))
      return DeviceTool::error("shutdown_cancelled_or_busy");
    userActivity(); showPage(Page::Home);
    auto *result = shutdownStatus();
    cJSON_AddBoolToObject(result, "ok", true);
    return result;
  }
  bool volume = command.operation == Operation::Volume;
  bool ok = volume ? voiceSetVolume(command.value) : setBrightness((command.value * 255 + 50) / 100);
  if (!ok) return DeviceTool::error(volume ? voiceVolumeError().c_str() : displayError.c_str());
  userActivity(); screenDirty = true;
  auto *result = deviceToolSnapshot(volume ? DeviceTool::Audio : DeviceTool::Display);
  cJSON_AddNumberToObject(result, "saved_percent", volume ? voiceVolume() : (brightness * 100 + 127) / 255);
  return result;
}

void cancelShutdown() {
  if (!shutdown.active()) return;
  shutdown.cancel("user_cancelled"); voiceCancel();
  userActivity(); showPage(Page::Home); showNotice("Shutdown cancelled");
}

void shutdownTick() {
  auto before = shutdown.phase;
  auto progress = voiceProgress();
  if (shutdown.tick(millis(), progress.turn, progress.busy, progress.cancelled, progress.done && progress.shutdownAccepted)) {
    screensaver.sleep(); sleepPanel();
    if (boardPowerOff() != PowerOffResult::Requested) shutdown.fail("power_off_write_failed");
  }
  // A successful register write is not proof that the PMU removed power.
  if (shutdown.phase == ShutdownState::Phase::Requested && uint32_t(millis() - shutdown.started) >= 2000)
    shutdown.fail("power_off_not_completed");
  if (before != shutdown.phase) {
    screenDirty = true;
    if (shutdown.phase == ShutdownState::Phase::Countdown) showPage(Page::Home);
    if (shutdown.phase == ShutdownState::Phase::Failed) {
      userActivity(); showPage(Page::Home); showNotice("Shutdown failed: use PWR");
    }
  }
}

void httpSetup() {
  http.on("/shutdown/cancel", HTTP_POST, [] {
    cancelShutdown();
    http.send(200, "application/json", jsonText(shutdownStatus()));
  });
  http.on("/status", HTTP_GET, [] { http.send(200, "application/json", statusJson()); });
  http.on("/display", HTTP_GET, [] { http.send(200, "application/json", displayJson()); });
  http.on("/visualizer", HTTP_GET, [] { http.send(200, "application/json", jsonText(visualizerStatus())); });
  http.on("/voice", HTTP_GET, [] { http.send(200, "application/json", jsonText(voiceStatus())); });
  http.on("/voice/volume", HTTP_POST, [] {
    String body = http.arg("plain");
    if (body.length() > 128) { http.send(413, "application/json", "{\"error\":\"body_too_large\"}"); return; }
    auto *j = parseObject(body);
    auto *v = cJSON_GetObjectItemCaseSensitive(j, "volume");
    bool valid = cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 100 && v->valuedouble == v->valueint;
    int value = valid ? v->valueint : -1;
    cJSON_Delete(j);
    if (!valid) { http.send(400, "application/json", "{\"error\":\"invalid_volume\"}"); return; }
    bool ok = voiceSetVolume(value);
    if (ok) { userActivity(); screenDirty = true; }
    http.send(ok ? 200 : 500, "application/json", jsonText(voiceStatus()));
  });
  http.on("/voice/start", HTTP_POST, [] {
    String body = http.arg("plain");
    if (body.length() > 128) { http.send(413, "application/json", "{\"error\":\"body_too_large\"}"); return; }
    auto *j = body.isEmpty() ? cJSON_CreateObject() : parseObject(body);
    auto *m = cJSON_GetObjectItemCaseSensitive(j, "mode");
    String mode = cJSON_IsString(m) ? m->valuestring : "chat";
    bool valid = j && (!m || cJSON_IsString(m)) && (mode == "chat" || mode == "echo" || mode == "loopback");
    cJSON_Delete(j);
    if (!valid) { http.send(400, "application/json", "{\"error\":\"invalid_mode\"}"); return; }
    bool ok = !batteryPolicy.shutdownDue && voiceStart(mode == "echo" ? VoiceMode::Echo : mode == "loopback" ? VoiceMode::Loopback : VoiceMode::Chat);
    if (ok) { userActivity(); screenDirty = true; }
    http.send(ok ? 202 : 409, "application/json", jsonText(voiceStatus()));
  });
  http.on("/voice/stop", HTTP_POST, [] {
    bool ok = voiceStopRecording();
    if (ok) userActivity();
    http.send(ok ? 202 : 409, "application/json", jsonText(voiceStatus()));
  });
  http.on("/voice/cancel", HTTP_POST, [] {
    cancelShutdown();
    voiceCancel();
    http.send(202, "application/json", jsonText(voiceStatus()));
  });
  http.on("/voice/reset", HTTP_POST, [] {
    cancelShutdown();
    bool ok = voiceReset();
    http.send(ok ? 202 : 409, "application/json", jsonText(voiceStatus()));
  });
  for (const char *path : {"/voice/ask", "/voice/say"}) {
    bool say = String(path) == "/voice/say";
    http.on(path, HTTP_POST, [say] {
      String body = http.arg("plain");
      if (body.length() > 8192) { http.send(413, "application/json", "{\"error\":\"body_too_large\"}"); return; }
      auto *j = parseObject(body);
      auto *value = cJSON_GetObjectItemCaseSensitive(j, "text");
      auto *speak = cJSON_GetObjectItemCaseSensitive(j, "speak");
      bool validSpeak = !speak || (!say && cJSON_IsBool(speak));
      bool textOnly = !say && cJSON_IsFalse(speak);
      String text = cJSON_IsString(value) ? value->valuestring : "";
      cJSON_Delete(j); text.trim();
      if (!validSpeak) { http.send(400, "application/json", "{\"error\":\"invalid_speak\"}"); return; }
      if (text.isEmpty() || text.length() > 4096) { http.send(400, "application/json", "{\"error\":\"invalid_text\"}"); return; }
      bool ok = !batteryPolicy.shutdownDue && voiceStart(say ? VoiceMode::Say : textOnly ? VoiceMode::AskText : VoiceMode::Ask, text.c_str());
      if (ok) { userActivity(); screenDirty = true; }
      http.send(ok ? 202 : 409, "application/json", jsonText(voiceStatus()));
    });
  }
  http.on("/voice/recording.wav", HTTP_GET, [] {
    size_t size = 0; auto *data = voiceLastRecording(size);
    if (!data || !size) { http.send(409, "application/json", "{\"error\":\"recording_unavailable_or_busy\"}"); return; }
    http.sendHeader("Cache-Control", "no-store");
    http.setContentLength(size); http.send(200, "audio/wav", "");
    for (size_t pos = 0; pos < size;) {
      size_t count = http.client().write(data + pos, std::min(size_t(2048), size - pos));
      if (!count) break;
      pos += count; delay(1);
    }
  });
  http.on("/display/brightness", HTTP_PUT, [] {
    String raw = http.arg("plain");
    if (raw.length() > 128) {
      http.send(413, "application/json", "{\"error\":\"body_too_large\"}");
      return;
    }
    auto *j = parseObject(raw);
    auto *v = cJSON_GetObjectItemCaseSensitive(j, "value");
    bool valid = cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 255;
    int value = valid ? v->valueint : 0;
    cJSON_Delete(j);
    if (!valid) {
      http.send(400, "application/json", "{\"error\":\"value_must_be_0_to_255\"}");
      return;
    }
    bool ok = setBrightness((uint8_t)value);
    http.send(ok ? 200 : 503, "application/json", displayJson());
  });
  http.on("/echo", HTTP_POST, [] {
    String body = http.arg("plain");
    if (body.length() > 2048) {
      http.send(413, "application/json", "{\"error\":\"body_too_large\"}");
      return;
    }
    cJSON *j = parseObject(body);
    auto *m = cJSON_GetObjectItemCaseSensitive(j, "message");
    bool valid = cJSON_IsString(m);
    String text = valid ? m->valuestring : "";
    cJSON_Delete(j);
    if (!valid || text.length() > 240) {
      http.send(400, "application/json", "{\"error\":\"message_must_be_ASCII_up_to_240_bytes\"}");
      return;
    }
    for (char c : text)
      if ((c < 32 || c > 126) && c != '\n') {
        http.send(400, "application/json", "{\"error\":\"ASCII_only\"}");
        return;
      }
    message = text;
    userActivity();
    screenDirty = true;
    auto *reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "message", message.c_str());
    http.send(200, "application/json", jsonText(reply));
  });
  http.onNotFound([] { http.send(404, "application/json", "{\"error\":\"not_found\"}"); });
  http.begin();
}

String ascii(const String &s) {
  String output;
  for (char c : s)
    output += (c >= 32 && c <= 126) ? c : '?';
  return output;
}

bool controlEnabled(Control id) {
  if (shutdown.active() && id != Control::Talk) return false;
  switch (id) {
    case Control::VolumeDown: return voiceVolume() > 0;
    case Control::VolumeUp: return voiceVolume() < 100;
    case Control::BrightnessDown: return brightness > 64;
    case Control::BrightnessUp: return brightness < 255;
    case Control::Wifi: return !voiceBusy();
    default: return !batteryPolicy.shutdownDue;
  }
}

void performVoiceAction(BadgeUI::VoiceAction action) {
  if (batteryPolicy.shutdownDue) return;
  if (shutdown.active()) {
    cancelShutdown(); feedbackButton = Control::Talk; feedbackUntil = millis() + 160; return;
  }
  userActivity(); showPage(Page::Home);
  // A touch released after the voice state changed must not start another job.
  if (action == BadgeUI::VoiceAction::Start) {
    if (!voiceBusy()) voiceStart();
  } else if (action == BadgeUI::VoiceAction::Send) {
    voiceStopRecording();
  } else {
    if (voiceBusy()) { voiceCancel(); showNotice("Cancelling..."); }
  }
  feedbackButton = Control::Talk; feedbackUntil = millis() + 160;
}

void activateControl(Control id) {
  if (!controlEnabled(id)) return;
  userActivity();
  switch (id) {
    case Control::Talk: performVoiceAction(touchVoiceAction); return;
    case Control::Settings: notice = ""; showPage(Page::Settings); return;
    case Control::Back: notice = ""; showPage(page == Page::Info ? Page::Settings : Page::Home); return;
    case Control::Info: showPage(Page::Info); return;
    case Control::VolumeDown: case Control::VolumeUp: {
      int volume = constrain(voiceVolume() + (id == Control::VolumeUp ? 10 : -10), 0, 100);
      showNotice(voiceSetVolume(volume) ? "Volume saved" : voiceVolumeError()); break;
    }
    case Control::BrightnessDown: case Control::BrightnessUp: {
      uint8_t level = id == Control::BrightnessUp ? (brightness < 160 ? 160 : 255) : (brightness > 160 ? 160 : 64);
      showNotice(setBrightness(level) ? "Brightness saved" : displayError); break;
    }
    case Control::Wifi: enableProvisioning(); showPage(Page::Info); return;
    default: return;
  }
  feedbackButton = id; feedbackUntil = millis() + 160; screenDirty = true;
}

void bootButtonTick() {
  if (bootButton.update(digitalRead(0) == LOW, millis()))
    performVoiceAction(BadgeUI::voiceAction(voiceBusy(), voiceRecording()));
}

void touchTick() {
  bool pressed = boardTouchPressed();
  bool newPress = pressed && !touch.down;
  bool sleeping = screensaver.active || screensaver.screenOff;
  int16_t x = -1, y = -1; uint32_t age;
  boardTouchPoint(x, y, age);
  auto previous = touch.captured;
  if (newPress) {
    touchVoiceAction = BadgeUI::voiceAction(voiceBusy(), voiceRecording());
    if (!batteryPolicy.shutdownDue) userActivity();
  }
  auto action = touch.update(pressed, x, y, page, !sleeping && !batteryPolicy.shutdownDue);
  if (touch.captured != Control::None && !controlEnabled(touch.captured)) touch.cancel();
  if (previous != touch.captured) screenDirty = true;
  if (pressed && !screensaver.screenOff) screensaver.keepAwake(millis());
  if (action != Control::None) activateControl(action);
}

void pwrButtonTick() {
  uint32_t count = boardPwrShortPressCount();
  while (lastPwrPressHandled != count) {
    ++lastPwrPressHandled;
    touch.cancel(); feedbackButton = Control::None;
    if (screensaver.active || screensaver.screenOff) userActivity();
    else { screensaver.sleep(); sleepPanel(); }
    screenDirty = true;
  }
}

} // namespace

void appSetup() {
  Serial.begin(115200);
  delay(300);
  pinMode(0, INPUT_PULLUP);
  commands = xQueueCreate(2, sizeof(Packet));
  if (!commands) {
    Serial.println("Command queue allocation failed");
    while (true)
      delay(1000);
  }
  storageOK = prefs.begin("badge-wifi", false);
  displayStorageOK = displayPrefs.begin("badge-display", false);
  brightness = displayStorageOK ? displayPrefs.getUChar("brightness", 160) : 160;
  if (!displayStorageOK)
    displayError = "display_storage_unavailable";

  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setTimeOut(50);

  if (!gfx->begin()) {
    Serial.println("DISPLAY init failed");
  }
  gfx->fillScreen(RGB565_BLACK);
  gfx->setBrightness(brightness);

  statusRowReady = statusRow.begin();
  buttonCanvasReady = buttonCanvas.begin();
  timeSyncSetup();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        lastWifiReason.store(info.wifi_sta_disconnected.reason);
        if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE)
          retryRequested.store(true);
        Serial.printf("WIFI disconnected reason=%u\n", info.wifi_sta_disconnected.reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  String saved = storageOK ? prefs.getString("credentials", "") : "";
  cJSON *j = parseObject(saved);
  String s, p;
  bool hasSaved = j && validCredentials(j, s, p);
  cJSON_Delete(j);
  if (hasSaved || DEFAULT_SSID[0]) {
    connectWifi(hasSaved ? s : DEFAULT_SSID, hasSaved ? p : DEFAULT_PASSWORD, false,
                hasSaved ? "saved" : "default");
  } else {
    source = "none";
    phase = "waiting_for_ble";
    enableProvisioning();
  }
  boardIOSetup();
  batteryTick();
  visualizerSetup();
  microphoneSetup();
  voiceSetup();
  if (!localToolsSetup()) Serial.println("TOOLS queue allocation failed");
  lastPwrPressHandled = boardPwrShortPressCount();
  bootButton.update(digitalRead(0) == LOW, millis());
  httpSetup();
  Serial.printf("BADGE %s ready\n", FIRMWARE_VERSION);
  userActivity();
  displayTick();
}

void appLoop() {
  // USB console retains provisioning and voice debugging commands.
  static String console;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (console == "provision") {
        userActivity();
        enableProvisioning();
      }
      if (console == "status")
        Serial.println(statusJson());
      if (console == "voice") {
        if (voiceStart()) userActivity();
        Serial.println(jsonText(voiceStatus()));
      }
      if (console == "voice-stop") voiceStopRecording();
      if (console == "voice-cancel") { cancelShutdown(); voiceCancel(); }
      if (console == "voice-reset") { cancelShutdown(); voiceReset(); }
      if (console == "scan" && !connecting && !voiceBusy()) {
        int count = WiFi.scanNetworks();
        int matches = 0;
        for (int i = 0; i < count; ++i)
          if (WiFi.SSID(i) == ssid) {
            ++matches;
            Serial.printf("WIFI target channel=%d rssi=%d auth=%d\n", WiFi.channel(i), WiFi.RSSI(i),
                          WiFi.encryptionType(i));
          }
        Serial.printf("WIFI scan result=%d target_matches=%d\n", count, matches);
        WiFi.scanDelete();
      }
      console = "";
    } else if (c != '\r') {
      if (console.length() < 32)
        console += c;
      else
        console = "";
    }
  }
  bootButtonTick();
  networkTick();
  if (timeSyncTick(WiFi.status() == WL_CONNECTED))
    screenDirty = true; // Do not wake the panel or reset the idle timer for NTP.
  http.handleClient();
  boardIOTick();
  batteryTick();
  touchTick();
  pwrButtonTick();
  localToolsPoll(deviceToolExecute);
  shutdownTick();
  displayTick();
  delay(2);
}
