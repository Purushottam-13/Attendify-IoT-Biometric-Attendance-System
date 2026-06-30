/**
 * ============================================================
 *  ATTENDIFY HARDWARE
 *  Battery HAL - Charging Status Helpers
 * ============================================================
 */

#ifndef BATTERY_HAL_H
#define BATTERY_HAL_H

#include <Arduino.h>

// Set pins as needed for your charging module.
// Keep as -1 if not connected.
#ifndef BATTERY_CHARGING_PIN
#define BATTERY_CHARGING_PIN -1
#endif

#ifndef BATTERY_FULL_PIN
#define BATTERY_FULL_PIN -1
#endif

// Typical charger modules expose active-low status pins.
#ifndef BATTERY_CHARGING_ACTIVE_LEVEL
#define BATTERY_CHARGING_ACTIVE_LEVEL LOW
#endif

#ifndef BATTERY_FULL_ACTIVE_LEVEL
#define BATTERY_FULL_ACTIVE_LEVEL LOW
#endif

enum BatteryStatus {
  BATTERY_STATUS_UNKNOWN = 0,
  BATTERY_STATUS_CHARGING,
  BATTERY_STATUS_FULL,
  BATTERY_STATUS_DISCHARGING
};

void battery_init();
BatteryStatus battery_get_status();
const char *battery_get_status_label();
const char *battery_get_status_short_label();

#endif // BATTERY_HAL_H
