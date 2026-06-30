/**
 * ============================================================
 *  ATTENDIFY HARDWARE
 *  Battery HAL - Charging Status Helpers
 * ============================================================
 */

#include "battery_hal.h"

void battery_init() {
  if (BATTERY_CHARGING_PIN >= 0) {
    pinMode(BATTERY_CHARGING_PIN, INPUT_PULLUP);
  }
  if (BATTERY_FULL_PIN >= 0) {
    pinMode(BATTERY_FULL_PIN, INPUT_PULLUP);
  }
}

BatteryStatus battery_get_status() {
  if (BATTERY_CHARGING_PIN < 0 && BATTERY_FULL_PIN < 0) {
    return BATTERY_STATUS_UNKNOWN;
  }

  bool chargingActive = false;
  bool fullActive = false;

  if (BATTERY_CHARGING_PIN >= 0) {
    chargingActive =
        (digitalRead(BATTERY_CHARGING_PIN) == BATTERY_CHARGING_ACTIVE_LEVEL);
  }

  if (BATTERY_FULL_PIN >= 0) {
    fullActive = (digitalRead(BATTERY_FULL_PIN) == BATTERY_FULL_ACTIVE_LEVEL);
  }

  if (chargingActive) {
    return BATTERY_STATUS_CHARGING;
  }

  if (fullActive) {
    return BATTERY_STATUS_FULL;
  }

  return BATTERY_STATUS_DISCHARGING;
}

const char *battery_get_status_label() {
  switch (battery_get_status()) {
  case BATTERY_STATUS_CHARGING:
    return "Charging";
  case BATTERY_STATUS_FULL:
    return "Full";
  case BATTERY_STATUS_DISCHARGING:
    return "On Battery";
  default:
    return "N/A";
  }
}

const char *battery_get_status_short_label() {
  switch (battery_get_status()) {
  case BATTERY_STATUS_CHARGING:
    return "CHG";
  case BATTERY_STATUS_FULL:
    return "FULL";
  case BATTERY_STATUS_DISCHARGING:
    return "BAT";
  default:
    return "N/A";
  }
}
