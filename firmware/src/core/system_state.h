/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  System State Machine
 * ============================================================
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>

// ============================================================
// SYSTEM STATES - Screen/Mode definitions
// ============================================================
enum SystemState {
  STATE_BOOT,
  STATE_HOME,
  STATE_MENU,
  STATE_ATTENDANCE,
  STATE_WIFI_MENU,
  STATE_WIFI_SCANNING,
  STATE_WIFI_SELECT,
  STATE_ABOUT,
  STATE_ERROR,
  STATE_SETUP,                // Phase 2: Setup mode
  STATE_TEACHER_SELECT,       // Phase 3: Teacher selection
  STATE_TEACHER_ENROLL,       // Phase 3: Teacher enrollment
  STATE_TEACHER_LOGIN,        // Phase 3: Teacher login
  STATE_SUBJECT_SELECT,       // Phase 7: Select subject
  STATE_SESSION_ACTIVE,       // Phase 7: Active class session
  STATE_SESSION_CONFIRM,      // Phase 12: Confirm session end (Fingerprint)
  STATE_SESSION_SUMMARY,      // Phase 7: Session summary
  STATE_ADMIN_AUTH,           // Phase 6: Admin fingerprint verification
  STATE_ADMIN_MENU,           // Phase 6: Admin mode menu
  STATE_ADMIN_TEACHER_LIST,   // Phase 6: Select teacher to authorize
  STATE_ADMIN_TEACHER_ENROLL, // Phase 6: Teacher fingerprint enrollment
  STATE_ADMIN_SD_LIST,        // New: Browse files on SD
  STATE_ADMIN_SD_DETAIL,      // New: View contents of a file
  STATE_RESET_CONFIRM,        // Admin Factory Reset
  // Phase 10: GFM Mode
  STATE_GFM_MODE,        // GFM main menu
  STATE_GFM_ENROLL_WAIT, // Waiting for enrollment queue
  STATE_GFM_ENROLLING,   // Fingerprint enrollment in progress
  // Phase 10: Lecture History
  STATE_LECTURE_HISTORY, // List of past lectures
  STATE_LECTURE_DETAIL   // Viewing attendance of one lecture
};

// ============================================================
// STATE MANAGER CLASS
// ============================================================
class StateManager {
public:
  StateManager();

  // State control
  void setState(SystemState newState);
  SystemState getState();
  SystemState getPreviousState();

  // State info
  const char *getStateName();
  bool isState(SystemState state);

private:
  SystemState currentState;
  SystemState previousState;
};

// Global instance
extern StateManager stateManager;

#endif // SYSTEM_STATE_H
