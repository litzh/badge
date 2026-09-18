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

constexpr char FIRMWARE_VERSION[] = "badge-0.6.1";

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
bool panelSleeping = false;
const char *powerOffStatus = "idle";
uint32_t lastPowerOffAttempt = 0;
bool powerOffAttempted = false;
bool screenDirty = true;
bool statusNeedsClear = true;
Arduino_Canvas statusRow(SCREEN, 24, nullptr);
bool statusRowReady = false;
uint32_t lastSaverFrame = 0;
uint32_t lastTouchHandled = 0;
uint32_t lastTapHandled = 0, lastVoiceTap = 0;
bool voiceWasBusy = false;

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
  static CachedRow rows[14];
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

void drawScreen() {
  if (statusNeedsClear) gfx->fillScreen(RGB565_BLACK);
  drawRow(68, timeSyncDisplay(), RGB565_WHITE, 2);
  drawRow(96, "BADGE", RGB565_CYAN, 3);
  drawRow(126, bleEnabled ? "BLE setup: open provision.html" : "Hold BOOT 3s for Wi-Fi setup", RGB565_MAGENTA, 1);
  drawRow(140, "State: " + phase, RGB565_WHITE, 2);
  drawRow(168, "SSID: " + (ssid.length() ? ssid : String("--")), RGB565_WHITE, 2);
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--");
  drawRow(196, "IP: " + ip + "  HTTP:80", RGB565_GREEN, 2);
  char bat[48];
  uint16_t mv;
  bool batOK;
  boardBattery(mv, batOK);
  BatterySample sample = boardBatterySample();
  if (batOK && sample.percent >= 0)
    snprintf(bat, sizeof(bat), "Battery: %d%%  %.2f V", sample.percent, mv / 1000.0f);
  else if (batOK)
    snprintf(bat, sizeof(bat), "Battery: --%%  %.2f V", mv / 1000.0f);
  else
    snprintf(bat, sizeof(bat), "Battery: --");
  drawRow(224, bat, batteryPolicy.conserve() ? RGB565_RED : RGB565_YELLOW, 2);
  int16_t tx;
  int16_t ty;
  uint32_t touchAge;
  char touchInfo[48] = "Touch: --";
  if (boardTouchPoint(tx, ty, touchAge) && touchAge < 60000)
    snprintf(touchInfo, sizeof(touchInfo), "Touch: %d, %d", tx, ty);
  const char *batteryHint = nullptr;
  if (batteryPolicy.cutoffPending) batteryHint = "LOW VOLTAGE - CONNECT USB";
  else if (batteryPolicy.low) batteryHint = "LOW BATTERY - CONNECT USB";
  else if (sample.powerValid && sample.vbus) {
    batteryHint = sample.direction == 1 ? "Charging" :
                  sample.percent == 100 && sample.chargerStatus == 4 ? "Fully charged - USB" : "USB power";
  }
  drawRow(252, batteryHint ? String(batteryHint) : String(touchInfo),
          batteryPolicy.conserve() ? RGB565_RED : RGB565_YELLOW, 2);
  // Echo message, wrapped row by row inside the circle.
  {
    String rest = message;
    for (int y = 280; y <= 330; y += 22) {
      int maxChars = maxCharsForRow(y, 2);
      if (maxChars <= 0)
        break;
      String line = rest.substring(0, min((size_t)maxChars, rest.length()));
      rest = rest.substring(line.length());
      drawRow(y, line, RGB565_WHITE, 2);
    }
  }
  char volumeLabel[32];
  snprintf(volumeLabel, sizeof(volumeLabel), "[-] Volume: %3d [+]", voiceVolume());
  drawRow(352, volumeLabel, voiceVolume() == 0 ? RGB565_YELLOW : RGB565_CYAN, 2);
  String action = voiceBusy() ? (voiceRecording() ? "Tap: finish recording" : "Tap: cancel") :
                  voiceConfigured() ? "Tap: ask a question" : "Voice not configured";
  drawRow(388, action, voiceBusy() ? RGB565_YELLOW : RGB565_CYAN, 2);
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
  if (speaking || voiceWasBusy) {
    if (!batteryPolicy.shutdownDue) userActivity();
    if (speaking) message = "Voice: " + voiceLabel();
    else message = "Voice: " + voiceLabel() + "\n" + (voiceError().isEmpty() ? String("Ready for next question") : voiceError());
    screenDirty = true;
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
  } else if (screenDirty || millis() - lastScreen >= 1000) {
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
  if (!displayStorageOK || displayPrefs.putUChar("brightness", value) != 1) {
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

void httpSetup() {
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
    voiceCancel();
    http.send(202, "application/json", jsonText(voiceStatus()));
  });
  http.on("/voice/reset", HTTP_POST, [] {
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
      String text = cJSON_IsString(value) ? value->valuestring : "";
      cJSON_Delete(j); text.trim();
      if (text.isEmpty() || text.length() > 4096) { http.send(400, "application/json", "{\"error\":\"invalid_text\"}"); return; }
      bool ok = !batteryPolicy.shutdownDue && voiceStart(say ? VoiceMode::Say : VoiceMode::Ask, text.c_str());
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

void bootButtonTick() {
  // BOOT = GPIO0, externally pulled up, low while pressed. Hold 3s to start
  // BLE provisioning.
  static uint32_t pressedAt = 0;
  static bool fired = false;
  bool pressed = digitalRead(0) == LOW;
  if (pressed)
    userActivity();
  if (pressed && !pressedAt) {
    pressedAt = millis();
    fired = false;
  } else if (pressed && !fired && millis() - pressedAt >= 3000) {
    fired = true;
    enableProvisioning();
  } else if (!pressed) {
    pressedAt = 0;
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
  lastPwrPressHandled = boardPwrShortPressCount();
  lastTouchHandled = boardTouchSequence();
  lastTapHandled = boardTouchTapSequence();
  httpSetup();
  Serial.printf("BADGE %s ready\n", FIRMWARE_VERSION);
  userActivity();
  displayTick();
}

void appLoop() {
  // USB console offers the same provisioning action as the physical BOOT key.
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
      if (console == "voice-cancel") voiceCancel();
      if (console == "voice-reset") voiceReset();
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
  uint32_t touchSequence = boardTouchSequence();
  uint32_t tapSequence = boardTouchTapSequence();
  bool wasSleeping = screensaver.active || screensaver.screenOff;
  if (touchSequence != lastTouchHandled) {
    lastTouchHandled = touchSequence;
    userActivity();
    screenDirty = true;
  }
  if (tapSequence != lastTapHandled) {
    lastTapHandled = tapSequence;
    int16_t x, y; uint32_t age;
    if (!wasSleeping && !batteryPolicy.shutdownDue && boardTouchPoint(x, y, age) &&
        uint32_t(millis() - lastVoiceTap) >= 350) {
      bool minus = x >= 100 && x <= 175 && y >= 338 && y <= 375;
      bool plus = x >= 291 && x <= 366 && y >= 338 && y <= 375;
      bool action = x >= 120 && x <= 346 && y >= 376 && y <= 427;
      if (minus || plus || action) {
        lastVoiceTap = millis();
        if (minus || plus) {
          int value = constrain(voiceVolume() + (plus ? 10 : -10), 0, 100);
          if (!voiceSetVolume(value)) message = "Volume error\n" + voiceVolumeError();
        } else if (voiceBusy()) { if (!voiceStopRecording()) voiceCancel(); }
        else if (!voiceStart()) message = "Voice unavailable\n" + voiceError();
        userActivity(); screenDirty = true;
      }
    }
  }
  // PWR short press cycles screen brightness.
  constexpr uint8_t levels[] = {64, 160, 255};
  uint32_t pwrCount = boardPwrShortPressCount();
  while (lastPwrPressHandled != pwrCount) {
    ++lastPwrPressHandled;
    userActivity();
    size_t index = 0;
    for (size_t i = 0; i < sizeof(levels); ++i)
      if (levels[i] == brightness)
        index = i;
    setBrightness(levels[(index + 1) % sizeof(levels)]);
    Serial.printf("PWR short press, brightness -> %u\n", brightness);
  }
  displayTick();
  delay(2);
}
