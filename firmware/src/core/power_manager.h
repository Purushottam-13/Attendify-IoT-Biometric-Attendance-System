/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 8
 *  Power Manager - Idle + Deep Sleep Control
 * ============================================================
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include "system_state.h"
#include "../hal/button_hal.h"

#define POWER_IDLE_TIMEOUT_MS 60000UL
#define POWER_DEEP_SLEEP_TIMEOUT_MS 120000UL
#define POWER_WAKE_PIN BTN_SELECT_PIN

void power_init();
void power_tick(SystemState currentState);

void power_notify_interaction();
void power_set_busy(bool busy);
bool power_is_busy();

bool power_woke_from_deep_sleep();
const char *power_get_wakeup_cause_text();

#endif // POWER_MANAGER_H
