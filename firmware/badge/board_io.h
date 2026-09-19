#pragma once
#include <cJSON.h>
#include <stdint.h>
#include "battery_policy.h"

// Board-level IO for ESP32-S3-Touch-AMOLED-1.75C: CST9217 touch, QMI8658 IMU,
// AXP2101 battery + PWR button. Direct register access on the shared I2C bus
// (SDA=15, SCL=14), no external sensor libraries. Wire must be started first.
void boardIOSetup();
void boardIOTick();

cJSON *boardBatteryStatus();
cJSON *boardTouchStatus();
cJSON *boardImuStatus();
cJSON *boardButtonsStatus();

// Latest touch point for on-screen feedback. Returns false if never touched.
bool boardTouchPoint(int16_t &x, int16_t &y, uint32_t &ageMs);
uint32_t boardPwrShortPressCount();
uint32_t boardTouchSequence();
uint32_t boardTouchTapSequence();
bool boardTouchPressed();
bool boardBattery(uint16_t &mv, bool &ok);
BatterySample boardBatterySample();
enum class PowerOffResult { Cancelled, WriteFailed, Requested };
PowerOffResult boardPowerOffIfLow(uint16_t thresholdMv);
// Explicit user-requested shutdown, main loop only.
PowerOffResult boardPowerOff();

struct BoardMotion {
  bool valid = false, temperatureValid = false;
  float ax = 0, ay = 0, az = 1, gx = 0, gy = 0, gz = 0, temperatureC = 32;
  uint32_t sampleAgeMs = UINT32_MAX, temperatureAgeMs = UINT32_MAX;
};
BoardMotion boardMotion();
void boardSetVisualActive(bool active);
