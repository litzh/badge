// Board-level IO: CST9217 touch, QMI8658 IMU, AXP2101 battery / PWR button.
// Protocol details were derived from the official bundled SensorLib
// (TouchDrvCST92xx, SensorQMI8658) and XPowersLib, see docs/hardware.md.
#include "board_io.h"
#include "pin_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>

namespace {

// Shared I2C devices (7-bit addresses).
constexpr uint8_t AXP2101_ADDR = 0x34;
constexpr uint8_t QMI8658_ADDR = 0x6B;
constexpr uint8_t CST9217_ADDR = 0x5A;

// AXP2101 registers.
constexpr uint8_t AXP_REG_VBAT_H = 0x34;      // battery ADC high 5 bits, mV
constexpr uint8_t AXP_REG_VBAT_L = 0x35;      // battery ADC low 8 bits
constexpr uint8_t AXP_REG_INTEN2 = 0x41;      // IRQ enable group 2
constexpr uint8_t AXP_REG_INTSTS2 = 0x49;     // IRQ status group 2
constexpr uint8_t AXP_INT_PWR_SHORT = 0x08;   // POWERON short press, bit 11 overall
constexpr uint8_t AXP_REG_BAT_DET = 0x68;     // battery detection control

// QMI8658 registers.
constexpr uint8_t QMI_REG_WHOAMI = 0x00;
constexpr uint8_t QMI_REG_CTRL1 = 0x02;
constexpr uint8_t QMI_REG_CTRL2 = 0x03; // accel config
constexpr uint8_t QMI_REG_CTRL3 = 0x04; // gyro config
constexpr uint8_t QMI_REG_CTRL7 = 0x08; // sensor enable
constexpr uint8_t QMI_REG_AX_L = 0x35;
constexpr uint8_t QMI_REG_GX_L = 0x3B;
constexpr uint8_t QMI_WHOAMI_VALUE = 0x05;
// CTRL2: range +/-4g (1 << 4) | ODR 250 Hz (5); scale 4 / 32768 g per LSB.
constexpr float QMI_ACCEL_SCALE = 4.0f / 32768.0f;
// CTRL3: range +/-512 dps (5 << 4) | ODR 224 Hz (5); scale 512 / 32768 dps per LSB.
constexpr float QMI_GYRO_SCALE = 512.0f / 32768.0f;

// CST9217 command protocol (TouchDrvCST92xx).
constexpr uint8_t CST_CMD_H = 0xD0;
constexpr uint8_t CST_CMD_L = 0x00;
constexpr uint8_t CST_ACK = 0xAB;
constexpr uint8_t CST_READ_LEN = 15; // 2 points * 5 + 5

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
    return false;
  if (Wire.requestFrom(addr, len) != len)
    return false;
  for (uint8_t i = 0; i < len; ++i)
    buf[i] = Wire.read();
  return true;
}

bool i2cWrite(uint8_t addr, const uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(addr);
  for (uint8_t i = 0; i < len; ++i)
    Wire.write(buf[i]);
  return Wire.endTransmission() == 0;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
  const uint8_t buf[2] = {reg, value};
  return i2cWrite(addr, buf, 2);
}

// ---------------------------------------------------------------- Battery --
uint16_t batteryMv = 0;
bool batteryOK = false;
uint32_t lastBattery = 0;

void sampleBattery() {
  uint8_t v[2];
  batteryOK = i2cRead(AXP2101_ADDR, AXP_REG_VBAT_H, v, 1) &&
              i2cRead(AXP2101_ADDR, AXP_REG_VBAT_L, v + 1, 1);
  if (batteryOK)
    batteryMv = ((v[0] & 0x1F) << 8) | v[1]; // 1 mV per LSB
  lastBattery = millis();
}

// ------------------------------------------------------------------ Touch --
bool touchOnline = false;
bool touchPressed = false;
int16_t touchX = 0, touchY = 0;
uint32_t lastTouch = 0;
volatile bool touchIrqPending = false;

void IRAM_ATTR onTouchIrq() { touchIrqPending = true; }

void touchReset() {
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(10);
  digitalWrite(TP_RST, HIGH);
  // Follow TouchDrvCST92xx::getAttribute: wait for boot, then enter command
  // mode before the 0xD000 point-read protocol becomes usable.
  delay(30);
  const uint8_t cmdMode[2] = {0xD1, 0x01};
  i2cWrite(CST9217_ADDR, cmdMode, 2);
  delay(10);
  // Sanity check: firmware checkcode must be 0xCACAxxxx.
  uint8_t info[4] = {0};
  Wire.beginTransmission(CST9217_ADDR);
  Wire.write(0xD1);
  Wire.write(0xFC);
  touchOnline = Wire.endTransmission(true) == 0 &&
                Wire.requestFrom(CST9217_ADDR, (uint8_t)4) == 4;
  if (touchOnline) {
    for (uint8_t i = 0; i < 4; ++i)
      info[i] = Wire.read();
    touchOnline = ((info[3] << 8) | info[2]) == 0xCACA;
    Serial.printf("TOUCH checkcode %02X%02X%02X%02X -> %s\n", info[3], info[2], info[1],
                  info[0], touchOnline ? "ok" : "bad");
  } else {
    Serial.println("TOUCH checkcode read failed");
  }
  // CST9217 pulses INT on touch events; reads outside an event return an
  // invalid frame, so reading is strictly interrupt-driven.
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchIrq, FALLING);
}

void touchPoll() {
  uint8_t buf[CST_READ_LEN];
  // The read command is a 2-byte register address (0xD0 0x00). CST9217
  // requires a STOP after the command, then a fresh read transaction
  // (SensorLib's SensorCommI2C defaults to sendStopFlag = true). NACKs are
  // normal while the controller is busy; keep the previous state.
  Wire.beginTransmission(CST9217_ADDR);
  Wire.write(CST_CMD_H);
  Wire.write(CST_CMD_L);
  if (Wire.endTransmission(true) != 0)
    return;
  if (Wire.requestFrom(CST9217_ADDR, CST_READ_LEN) != CST_READ_LEN)
    return;
  for (uint8_t i = 0; i < CST_READ_LEN; ++i)
    buf[i] = Wire.read();
  const uint8_t ack[3] = {CST_CMD_H, CST_CMD_L, CST_ACK};
  i2cWrite(CST9217_ADDR, ack, 3);
  if (buf[6] != CST_ACK) {
    static uint32_t lastDebug = 0;
    if (millis() - lastDebug >= 2000) {
      lastDebug = millis();
      Serial.printf("TOUCH raw:");
      for (uint8_t i = 0; i < CST_READ_LEN; ++i)
        Serial.printf(" %02X", buf[i]);
      Serial.println();
    }
    return;
  }
  touchOnline = true;
  uint8_t points = buf[5] & 0x7F;
  if (points >= 1 && (buf[0] & 0x0F) == 0x06) {
    uint16_t x = ((uint16_t)buf[1] << 4) | (buf[3] >> 4);
    uint16_t y = ((uint16_t)buf[2] << 4) | (buf[3] & 0x0F);
    // Panel coordinates are mirrored on this board (official example uses
    // setMirrorXY(true, true) with 466x466).
    touchX = 465 - x;
    touchY = 465 - y;
    touchPressed = true;
    lastTouch = millis();
  } else {
    touchPressed = false;
  }
}

// -------------------------------------------------------------------- IMU --
bool imuOnline = false;
float accelG[3] = {0, 0, 0};
float gyroDps[3] = {0, 0, 0};
uint32_t lastImu = 0, lastImuSample = 0;

bool imuSetup() {
  uint8_t id;
  if (!i2cRead(QMI8658_ADDR, QMI_REG_WHOAMI, &id, 1) || id != QMI_WHOAMI_VALUE)
    return false;
  // CTRL1: little-endian data + register address auto-increment.
  if (!i2cWriteReg(QMI8658_ADDR, QMI_REG_CTRL1, 0x40))
    return false;
  if (!i2cWriteReg(QMI8658_ADDR, QMI_REG_CTRL2, 0x15)) // accel +/-4g @250Hz
    return false;
  if (!i2cWriteReg(QMI8658_ADDR, QMI_REG_CTRL3, 0x55)) // gyro +/-512dps @224Hz
    return false;
  if (!i2cWriteReg(QMI8658_ADDR, QMI_REG_CTRL7, 0x03)) // enable accel + gyro
    return false;
  return true;
}

void imuSample() {
  uint8_t raw[6];
  if (!i2cRead(QMI8658_ADDR, QMI_REG_AX_L, raw, 6)) {
    imuOnline = false;
    return;
  }
  for (int i = 0; i < 3; ++i)
    accelG[i] = (int16_t)(raw[2 * i] | (raw[2 * i + 1] << 8)) * QMI_ACCEL_SCALE;
  if (!i2cRead(QMI8658_ADDR, QMI_REG_GX_L, raw, 6)) {
    imuOnline = false;
    return;
  }
  for (int i = 0; i < 3; ++i)
    gyroDps[i] = (int16_t)(raw[2 * i] | (raw[2 * i + 1] << 8)) * QMI_GYRO_SCALE;
  imuOnline = true;
  lastImu = millis();
}

// ------------------------------------------------------------------ PWR -----
uint32_t pwrShortPressCount = 0;
uint32_t lastPwrPoll = 0;

void pwrPoll() {
  uint8_t status;
  if (!i2cRead(AXP2101_ADDR, AXP_REG_INTSTS2, &status, 1))
    return;
  if (status & AXP_INT_PWR_SHORT) {
    ++pwrShortPressCount;
    i2cWriteReg(AXP2101_ADDR, AXP_REG_INTSTS2, AXP_INT_PWR_SHORT); // write 1 to clear
  }
}

} // namespace

void boardIOSetup() {
  // The CST9217 NACKs I2C transactions while busy; that is expected during
  // polling, so keep the IDF I2C driver from spamming the console.
  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  // AXP2101: battery detection + PWR short-press IRQ.
  uint8_t value;
  if (i2cRead(AXP2101_ADDR, AXP_REG_BAT_DET, &value, 1))
    i2cWriteReg(AXP2101_ADDR, AXP_REG_BAT_DET, value | 0x01);
  if (i2cRead(AXP2101_ADDR, AXP_REG_INTEN2, &value, 1))
    i2cWriteReg(AXP2101_ADDR, AXP_REG_INTEN2, value | AXP_INT_PWR_SHORT);
  i2cWriteReg(AXP2101_ADDR, AXP_REG_INTSTS2, AXP_INT_PWR_SHORT); // clear stale flag
  sampleBattery();

  touchReset();
  touchPoll(); // probe once so status is meaningful right away

  imuOnline = imuSetup();
  if (imuOnline)
    imuSample();
}

void boardIOTick() {
  uint32_t now = millis();
  if (now - lastBattery >= 5000)
    sampleBattery();
  if (touchIrqPending) {
    touchIrqPending = false;
    touchPoll();
  }
  if (now - lastImuSample >= 200) {
    lastImuSample = now;
    imuSample();
  }
  if (now - lastPwrPoll >= 200) {
    lastPwrPoll = now;
    pwrPoll();
  }
}

cJSON *boardBatteryStatus() {
  cJSON *b = cJSON_CreateObject();
  cJSON_AddStringToObject(b, "status", batteryOK ? "ok" : "read_failed");
  if (batteryOK)
    cJSON_AddNumberToObject(b, "voltage_v", batteryMv / 1000.0);
  else
    cJSON_AddNullToObject(b, "voltage_v");
  cJSON_AddNumberToObject(b, "sample_age_ms", millis() - lastBattery);
  return b;
}

cJSON *boardTouchStatus() {
  cJSON *t = cJSON_CreateObject();
  cJSON_AddStringToObject(t, "controller", "CST9217");
  cJSON_AddStringToObject(t, "status", touchOnline ? "ok" : "read_failed");
  cJSON_AddBoolToObject(t, "pressed", touchPressed);
  if (touchOnline && lastTouch) {
    cJSON_AddNumberToObject(t, "x", touchX);
    cJSON_AddNumberToObject(t, "y", touchY);
  } else {
    cJSON_AddNullToObject(t, "x");
    cJSON_AddNullToObject(t, "y");
  }
  cJSON_AddNumberToObject(t, "last_touch_age_ms", lastTouch ? millis() - lastTouch : -1);
  return t;
}

cJSON *boardImuStatus() {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "controller", "QMI8658");
  cJSON_AddStringToObject(m, "status", imuOnline ? "ok" : "read_failed");
  auto *a = cJSON_AddObjectToObject(m, "accel_g");
  auto *g = cJSON_AddObjectToObject(m, "gyro_dps");
  const char *axes[3] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) {
    if (imuOnline) {
      cJSON_AddNumberToObject(a, axes[i], accelG[i]);
      cJSON_AddNumberToObject(g, axes[i], gyroDps[i]);
    } else {
      cJSON_AddNullToObject(a, axes[i]);
      cJSON_AddNullToObject(g, axes[i]);
    }
  }
  cJSON_AddNumberToObject(m, "sample_age_ms", millis() - lastImu);
  return m;
}

cJSON *boardButtonsStatus() {
  cJSON *b = cJSON_CreateObject();
  auto *boot = cJSON_AddObjectToObject(b, "boot");
  cJSON_AddBoolToObject(boot, "pressed", digitalRead(0) == LOW);
  auto *pwr = cJSON_AddObjectToObject(b, "pwr");
  cJSON_AddNumberToObject(pwr, "short_press_count", pwrShortPressCount);
  return b;
}

bool boardTouchPoint(int16_t &x, int16_t &y, uint32_t &ageMs) {
  if (!lastTouch)
    return false;
  x = touchX;
  y = touchY;
  ageMs = millis() - lastTouch;
  return true;
}

uint32_t boardPwrShortPressCount() { return pwrShortPressCount; }

bool boardBattery(uint16_t &mv, bool &ok) {
  mv = batteryMv;
  ok = batteryOK;
  return batteryOK;
}
