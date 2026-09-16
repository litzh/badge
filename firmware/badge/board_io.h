#pragma once
#include <cJSON.h>
#include <stdint.h>

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
bool boardBattery(uint16_t &mv, bool &ok);
