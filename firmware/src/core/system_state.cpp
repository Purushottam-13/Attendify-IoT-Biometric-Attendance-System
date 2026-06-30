/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  System State Machine Implementation
 * ============================================================
 */

#include "system_state.h"
#include "../hal/fingerprint_hal.h"

// Global instance
StateManager stateManager;

// ============================================================
// CONSTRUCTOR
// ============================================================
StateManager::StateManager() {
  currentState = STATE_BOOT;
  previousState = STATE_BOOT;
}

// ============================================================
// STATE CONTROL
// ============================================================
void StateManager::setState(SystemState newState) {
  if (newState != currentState) {
    previousState = currentState;
    currentState = newState;
    Serial.print("[STATE] ");
    Serial.print(getStateName());
    Serial.println();

    // Centralized robust fingerprint LED control based on the active state
    if (newState == STATE_ATTENDANCE || 
        newState == STATE_TEACHER_LOGIN || 
        newState == STATE_ADMIN_AUTH || 
        newState == STATE_SESSION_ACTIVE ||
        newState == STATE_RESET_CONFIRM) {
      fingerprint_led_on();
    } else {
      fingerprint_led_off();
    }
  }
}

SystemState StateManager::getState() { return currentState; }

SystemState StateManager::getPreviousState() { return previousState; }

// ============================================================
// STATE INFO
// ============================================================
const char *StateManager::getStateName() {
  switch (currentState) {
  case STATE_BOOT:
    return "BOOT";
  case STATE_HOME:
    return "HOME";
  case STATE_MENU:
    return "MENU";
  case STATE_ATTENDANCE:
    return "ATTENDANCE";
  case STATE_WIFI_MENU:
    return "WIFI_MENU";
  case STATE_WIFI_SCANNING:
    return "WIFI_SCANNING";
  case STATE_WIFI_SELECT:
    return "WIFI_SELECT";
  case STATE_ABOUT:
    return "ABOUT";
  case STATE_ERROR:
    return "ERROR";
  case STATE_SETUP:
    return "SETUP";
  case STATE_TEACHER_SELECT:
    return "TEACHER_SELECT";
  case STATE_TEACHER_ENROLL:
    return "TEACHER_ENROLL";
  case STATE_TEACHER_LOGIN:
    return "TEACHER_LOGIN";
  case STATE_SUBJECT_SELECT:
    return "SUBJECT_SELECT";
  case STATE_SESSION_ACTIVE:
    return "SESSION_ACTIVE";
  case STATE_SESSION_CONFIRM:
    return "SESSION_CONFIRM";
  case STATE_SESSION_SUMMARY:
    return "SESSION_SUMMARY";
  case STATE_ADMIN_AUTH:
    return "ADMIN_AUTH";
  case STATE_ADMIN_MENU:
    return "ADMIN_MENU";
  case STATE_ADMIN_TEACHER_LIST:
    return "ADMIN_TEACHER_LIST";
  case STATE_ADMIN_TEACHER_ENROLL:
    return "ADMIN_TEACHER_ENROLL";
  case STATE_ADMIN_SD_LIST:
    return "ADMIN_SD_LIST";
  case STATE_ADMIN_SD_DETAIL:
    return "ADMIN_SD_DETAIL";
  case STATE_GFM_MODE:
    return "GFM_MODE";
  case STATE_GFM_ENROLL_WAIT:
    return "GFM_ENROLL_WAIT";
  case STATE_GFM_ENROLLING:
    return "GFM_ENROLLING";
  default:
    return "UNKNOWN";
  }
}

bool StateManager::isState(SystemState state) { return currentState == state; }
