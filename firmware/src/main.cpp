/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  MAIN.CPP - ORCHESTRATOR
 * ============================================================
 */

#include <Arduino.h>

// Core
#include "core/system_state.h"
#include "core/power_manager.h"

// HAL
#include "hal/button_hal.h"
#include "hal/display_hal.h"
#include "hal/fingerprint_hal.h"
#include "hal/fp_mapping_hal.h"
#include "hal/rtc_hal.h"
#include "hal/sd_hal.h"
#include "hal/wifi_hal.h"

// UI
#include "ui/gfm_ui.h"
#include "ui/ui_renderer.h"

// Utils
#include "utils/logger.h"

// Phase 2: Setup Mode
#include "setup/setup_mode.h"

// Phase 3: Teacher Auth
#include "security/teacher_auth.h"

// Phase 3: Device Info
#include "core/device_info.h"

// Phase 3: Remote Config
#include "config/remote_config_loader.h"

// Phase 7: Session Manager
#include "core/session_manager.h"

// HTTP Client for server probe
#include <HTTPClient.h>

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void handleStateLogic();
void handleAttendanceMode();
void handleSessionActive();      // Phase 7
void handleTeacherEnroll();      // Phase 3
void handleTeacherLogin();       // Phase 3
void handleAdminAuth();          // Phase 6
void handleAdminTeacherEnroll(); // Phase 6
void handleSafeResetConfirm();   // Data-only reset
void handleGfmEnrollWait();      // Phase 10
void checkAutoSync();            // Phase 13

static int clear_attendance_csv_files() {
  if (!sd_is_ready()) {
    return 0;
  }

  std::vector<String> files = sd_list_files_recursive("/attendance");
  int deletedCount = 0;

  for (const String &filePath : files) {
    if (!filePath.endsWith(".csv") && !filePath.endsWith(".CSV")) {
      continue;
    }
    if (filePath == "/attendance/fp_map.csv") {
      continue;
    }
    if (sd_delete_file(filePath.c_str())) {
      deletedCount++;
    }
  }

  return deletedCount;
}

static void getFriendlyTeacherEnrollError(FingerprintEnrollError error,
                                          const char **line1,
                                          const char **line2) {
  switch (error) {
  case FP_ENROLL_ERROR_TIMEOUT_FIRST_SCAN:
    *line1 = "No finger detected";
    *line2 = "Place finger faster";
    return;
  case FP_ENROLL_ERROR_TIMEOUT_SECOND_SCAN:
    *line1 = "Second scan timeout";
    *line2 = "Use same finger";
    return;
  case FP_ENROLL_ERROR_FINGER_MISMATCH:
    *line1 = "Finger mismatch";
    *line2 = "Use same finger";
    return;
  case FP_ENROLL_ERROR_IMAGE_CONVERT_1:
  case FP_ENROLL_ERROR_IMAGE_CONVERT_2:
    *line1 = "Image quality low";
    *line2 = "Clean sensor/finger";
    return;
  case FP_ENROLL_ERROR_COMMUNICATION:
    *line1 = "Sensor comm error";
    *line2 = "Check sensor wiring";
    return;
  case FP_ENROLL_ERROR_STORE_FAILED:
    *line1 = "Store failed";
    *line2 = "Try another finger";
    return;
  default:
    *line1 = "Enrollment failed";
    *line2 = "Try again";
    return;
  }
}

// ============================================================
// TIMING
// ============================================================
#define BOOT_DISPLAY_TIME_MS 2000
#define UI_REFRESH_INTERVAL 100

static unsigned long lastUiUpdate = 0;
static bool bootTimeSetupTriggered =
    false; // Flag to track boot-time setup entry
static SystemState lastObservedState = STATE_BOOT;

// GFM Mode flag (used by UI renderer)
bool pendingGfmMode = false;

// ============================================================
// HARDWARE STATUS
// ============================================================
struct HardwareStatus {
  bool display;
  bool fingerprint;
  bool rtc;
  bool sd;
  bool wifi;
};

static HardwareStatus hwStatus;

// ============================================================
// SETUP
// ============================================================
void setup() {
  logger_init(115200);
  delay(500);

  Serial.println("[MAIN] Starting initialization...");

  bool bootInterrupted = false;
  hwStatus.display = display_init();
  if (hwStatus.display) {
    bootInterrupted = display_boot_screen();
  }

  button_init();
  power_init();
  hwStatus.rtc = rtc_init();
  hwStatus.fingerprint = fingerprint_init();
  hwStatus.sd = sd_init();
  wifi_init();
  hwStatus.wifi = false;
  ui_init();

  Serial.println("========================================");
  Serial.println("  HARDWARE STATUS");
  Serial.println("========================================");
  Serial.print("  Display:     ");
  Serial.println(hwStatus.display ? "OK" : "FAIL");
  Serial.print("  Fingerprint: ");
  Serial.println(hwStatus.fingerprint ? "OK" : "FAIL");
  Serial.print("  RTC:         ");
  Serial.println(hwStatus.rtc ? "OK" : "FAIL");
  Serial.print("  SD Card:     ");
  Serial.println(hwStatus.sd ? "OK" : "FAIL");
  Serial.println("========================================");

  // Phase 2: Initialize and check setup mode
  setup_mode_init();

  // Phase 3: Initialize device info
  device_info_init();

  // Phase 3: Initialize remote config (Load from NVS)
  remote_config_init();

  // Phase 3: Initialize teacher auth
  teacher_init();
  teacher_load_from_config();

  // Phase 7: Initialize session manager
  session_init();

  // Phase 14: ID Mapping
  fp_map_init();

  // Biometric Auto-Restore from SD Card
  if (hwStatus.fingerprint && hwStatus.sd) {
    uint16_t count = fingerprint_count();
    Serial.printf("[MAIN] Initial fingerprint count in sensor: %d\n", count);
    if (count == 0) {
      Serial.println("[MAIN] Sensor database is empty. Checking SD card for backups...");
      
      bool has_backups = false;
      for (uint16_t id = 1; id < 512; id++) {
        char filename[32];
        snprintf(filename, sizeof(filename), "/fps/template_%d.dat", id);
        if (sd_file_exists(filename)) {
          has_backups = true;
          break;
        }
      }

      if (has_backups) {
        display_clear();
        display_header("RESTORE SYSTEM");
        display_message("Restoring templates");
        display_update();

        if (fingerprint_restore_all_from_sd()) {
          Serial.println("[MAIN] Biometric restore completed successfully!");
          display_clear();
          display_header("RESTORE SYSTEM");
          display_message("Success!");
          display_update();
          delay(1000);
        } else {
          Serial.println("[MAIN] Biometric restore failed or partially completed.");
          display_clear();
          display_header("RESTORE SYSTEM");
          display_message("Failed!");
          display_update();
          delay(1000);
        }
      } else {
        Serial.println("[MAIN] No biometric backups found on SD card.");
      }
    }
  }

  // ============================================================
  // BOOT-TIME SETUP ENTRY: Hold SELECT for 5 seconds
  // ============================================================
  // Silent check - no message unless button is pressed

  unsigned long bootStart = millis();
  bool forceSetup = false;
  bool buttonHeld = bootInterrupted;

  // Check if button is pressed during boot
  // Add small delay for power stabilization
  delay(100);

  if (digitalRead(BTN_SELECT_PIN) == LOW) {
    buttonHeld = true;
    Serial.println("[MAIN] Button press detected at boot");
  } else {
    // Check window (3000ms) - gives user plenty of time
    while (millis() - bootStart < 3000) {
      if (digitalRead(BTN_SELECT_PIN) == LOW) {
        buttonHeld = true;
        Serial.println("[MAIN] Button press detected");
        break;
      }
      delay(10);
    }
  }

  // If button is pressed, check if held for 5 seconds
  if (buttonHeld) {
    Serial.println("[MAIN] Checking for 5s hold...");

    // Full display clear to prevent ghosting/overlap from boot screen
    display_clear();
    display_update();
    delay(50); // Allow display to fully clear

    display_clear();
    display_header("SETUP ENTRY");
    display_message("Keep holding...");
    display_update();

    unsigned long holdStart = millis();
    bool earlyRelease = false;

    while (millis() - holdStart < 5000) {
      // Debounced release check - require release for 100ms contiguous
      if (digitalRead(BTN_SELECT_PIN) == HIGH) {
        unsigned long releaseStart = millis();
        bool confirmedRelease = true;
        while (millis() - releaseStart < 100) {
          if (digitalRead(BTN_SELECT_PIN) == LOW) {
            confirmedRelease = false;
            break;
          }
          delay(10);
        }

        if (confirmedRelease) {
          earlyRelease = true;
          Serial.println("[MAIN] Button released too early");
          break;
        }
      }

      // Show countdown
      int remaining = 5 - (millis() - holdStart) / 1000;
      static int lastRemaining = -1;
      if (remaining != lastRemaining) {
        lastRemaining = remaining;

        // Clear to prevent overlap with "Keep holding..."
        display_clear();
        display_header("SETUP ENTRY");

        char buf[20];
        snprintf(buf, 20, "Hold: %d sec...", remaining);
        display_message(buf);
        display_update();
      }
      delay(50);
    }

    if (!earlyRelease) {
      forceSetup = true;
      Serial.println("[MAIN] SELECT held 5 sec - forcing SETUP MODE");

      // Full clear before showing success
      display_clear();
      display_update();
      delay(50);

      display_clear();
      display_header("SETUP ENTRY");
      display_message("ACTIVATED!");
      display_update();
      delay(1000); // Show success message

      // Clear and show release message
      display_clear();
      display_header("SETUP ENTRY");
      display_message("Release button!");
      display_update();
    }
  }

  // Wait for button release to prevent triggering main loop actions
  if (digitalRead(BTN_SELECT_PIN) == LOW) {
    Serial.println("[MAIN] Waiting for release...");
    while (digitalRead(BTN_SELECT_PIN) == LOW) {
      delay(50);
    }
    Serial.println("[MAIN] Released");
  }

  delay(1000); // Extended delay for proper button isolation

  // Reset button HAL state to prevent ghost events in loop()
  button_reset();
  delay(200); // Additional settle time

  // Decide which mode to enter
  if (forceSetup) {
    setup_reset(); // Reset setup completion flag
    stateManager.setState(STATE_SETUP);
    bootTimeSetupTriggered =
        true; // Set flag to prevent button handler interference
    Serial.println("[MAIN] Entering SETUP MODE (forced)");
  } else if (!setup_is_completed()) {
    stateManager.setState(STATE_SETUP);
    Serial.println("[MAIN] First boot - entering SETUP MODE");
  } else {
    // Check for active session recovery
    if (session_check_and_resume_recovery()) {
      stateManager.setState(STATE_SESSION_ACTIVE);
      Serial.println("[MAIN] Active session recovered - entering SESSION MODE");
    } else {
      stateManager.setState(STATE_HOME);
      Serial.println("[MAIN] Setup completed - entering HOME MODE");
      fingerprint_led_off();          // Keep LED off in home screen
      bootTimeSetupTriggered = false; // Clear flag for normal operation
    }
  }

  lastObservedState = stateManager.getState();
  power_notify_interaction();

  if (power_woke_from_deep_sleep()) {
    ui_show_status("WAKE", power_get_wakeup_cause_text(), 1000);
  }

  Serial.println("[MAIN] Initialization complete!");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  ButtonEvent event = button_poll();
  if (event != BTN_NONE) {
    power_notify_interaction();
  }

  ui_handle_input(event);
  handleStateLogic();

  SystemState currentState = stateManager.getState();
  if (currentState != lastObservedState) {
    lastObservedState = currentState;
    power_notify_interaction();
  }

  // Background Wi-Fi connection scanner/connector
  wifi_background_tick();

  // Phase 13: Auto-sync pending files
  checkAutoSync();

  unsigned long now = millis();
  if (now - lastUiUpdate >= UI_REFRESH_INTERVAL) {
    ui_render();
    lastUiUpdate = now;
  }

  power_tick(currentState);

  delay(10);
}

// ============================================================
// BOOT SETUP STATE GETTER
// ============================================================
bool is_boot_setup_triggered() { return bootTimeSetupTriggered; }

// ============================================================
// STATE LOGIC
// ============================================================
void handleStateLogic() {
  SystemState state = stateManager.getState();

  switch (state) {
  case STATE_ATTENDANCE:
    handleAttendanceMode();
    break;
  case STATE_SETUP: // Phase 2
    setup_mode_loop();
    break;
  case STATE_TEACHER_ENROLL: // Phase 3
    handleTeacherEnroll();
    break;
  case STATE_TEACHER_LOGIN: // Phase 3
    handleTeacherLogin();
    break;
  // Phase 6: Admin Mode
  case STATE_ADMIN_AUTH:
    handleAdminAuth();
    break;
  case STATE_ADMIN_TEACHER_ENROLL:
    handleAdminTeacherEnroll();
    break;
  case STATE_RESET_CONFIRM:
    handleSafeResetConfirm();
    break;
  // Phase 7: Session Logic
  case STATE_SESSION_ACTIVE:
    handleSessionActive();
    break;
  // Phase 10: GFM Mode logic
  case STATE_GFM_MODE:
    break;
  case STATE_GFM_ENROLL_WAIT:
    handleGfmEnrollWait();
    break;
  default:
    break;
  }
}

// ============================================================
// ATTENDANCE MODE
// ============================================================
void handleAttendanceMode() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck < 200)
    return;
  lastCheck = now;

  uint16_t matchedId;
  if (fingerprint_verify(&matchedId)) {
    Serial.print("[MAIN] Attendance for ID: ");
    Serial.println(matchedId);

    String record = rtc_now_string() + "," + String(matchedId);

    if (hwStatus.sd) {
      String filePath = "/attendance/" + rtc_get_date_string() + ".csv";
      sd_write_attendance(filePath, record);
    }

    ui_show_status("SUCCESS", "Attendance Marked!", 2000);
    stateManager.setState(STATE_HOME);
  }
}

// ============================================================
// PHASE 7: ACTIVE SESSION
// ============================================================
void handleSessionActive() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck < 200)
    return;
  lastCheck = now;

  uint16_t matchedId;
  if (fingerprint_verify(&matchedId)) {
    // Check if this is the logged-in teacher's fingerprint
    Teacher *current = teacher_get_current_obj();
    if (current && matchedId == current->fingerprintId) {
      // Teacher scanned finger -> End session
      Serial.println("[SESSION] Teacher fingerprint detected - ending session");
      ui_show_status("ENDING", "Session Closed", 1500);
      session_end();
      stateManager.setState(STATE_SESSION_SUMMARY);
      return;
    }

    // Not teacher - record student attendance
    if (session_record_attendance(matchedId)) {
      ui_show_status("SUCCESS", ("ID: " + String(matchedId)).c_str(), 1000);
    } else {
      ui_show_status("DUPLICATE", "Already Marked", 1000);
    }
    // Stay in session active state
    stateManager.setState(STATE_SESSION_ACTIVE);
  }
}

// ============================================================
// PHASE 3: TEACHER ENROLLMENT
// ============================================================
void handleTeacherEnroll() {
  static unsigned long lastCheck = 0;
  static bool enrolling = false;
  unsigned long now = millis();

  if (now - lastCheck < 100)
    return;
  lastCheck = now;

  // Get selected teacher index from UI (use extern or pass via state)
  extern int teacherSelection;

  if (!enrolling) {
    enrolling = true;
    if (teacher_enroll(teacherSelection)) {
      ui_show_status("SUCCESS", "Teacher Enrolled!", 2000);
    } else {
      ui_show_status("FAILED", "Enrollment Failed", 2000);
    }
    enrolling = false;
    stateManager.setState(STATE_TEACHER_SELECT);
  }
}

// ============================================================
// PHASE 3: TEACHER LOGIN
// ============================================================
void handleTeacherLogin() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck < 200)
    return;
  lastCheck = now;

  if (teacher_is_logged_in()) {
    // Already logged in - wait for user action
    return;
  }

  int matchedIndex;
  if (teacher_verify(&matchedIndex)) {
    teacher_set_current(matchedIndex);
    ui_show_status("WELCOME", teacher_get_current().c_str(), 1500);

    extern bool pendingGfmMode;
    if (pendingGfmMode) {
      Teacher *t = teacher_get_current_obj();
      if (t && t->isGfm) {
        // Verified GFM teacher
        Serial.println("[AUTH] GFM access granted");
        pendingGfmMode = false; // Clear flag
        stateManager.setState(STATE_GFM_MODE);
      } else {
        // Access Denied
        Serial.println("[AUTH] GFM access denied for non-GFM teacher");
        ui_show_status("DENIED", "GFM Access Only", 2000);
        teacher_logout(); // Force logout
        pendingGfmMode = false;
        stateManager.setState(STATE_MENU);
      }
    } else {
      // Normal teacher mode - show teacher menu
      stateManager.setState(STATE_TEACHER_LOGIN);
    }
  }
}

// ============================================================
// PHASE 6: ADMIN MODE - AUTHENTICATION
// ============================================================
void handleAdminAuth() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck < 200)
    return;
  lastCheck = now;

  // Check for admin fingerprint (ID 1)
  uint16_t matchedId;
  if (fingerprint_verify(&matchedId)) {
    if (matchedId == ADMIN_FP_ID) {
      Serial.println("[ADMIN] Admin verified! Entering admin menu");
      ui_show_status("VERIFIED", "Admin Access", 1000);
      stateManager.setState(STATE_ADMIN_MENU);
    } else {
      Serial.println("[ADMIN] Not admin fingerprint");
      ui_show_status("DENIED", "Admin Only", 1500);
      stateManager.setState(STATE_HOME);
    }
  }
}

// ============================================================
// PHASE 6: ADMIN MODE - TEACHER ENROLLMENT
// ============================================================
void handleAdminTeacherEnroll() {
  static unsigned long lastCheck = 0;
  static bool enrolling = false;
  unsigned long now = millis();

  if (now - lastCheck < 100)
    return;
  lastCheck = now;

  extern int teacherSelection;

  if (!enrolling) {
    enrolling = true;
    Serial.print("[ADMIN] Enrolling teacher index: ");
    Serial.println(teacherSelection);

    std::vector<Teacher> &teachers = teacher_get_list();
    const char *teacherName = "Selected Teacher";
    if (teacherSelection >= 0 && teacherSelection < (int)teachers.size()) {
      teacherName = teachers[teacherSelection].name.c_str();
    }

    // Force a clear pre-enroll prompt before blocking fingerprint flow starts
    display_clear();
    display_header("ENROLL TEACHER");
    display_message_multi(teacherName, "Place finger now", "Keep steady");
    display_update();
    delay(150);

    if (teacher_enroll(teacherSelection)) {
      ui_show_status("SUCCESS", "Teacher Enrolled!", 2000);
      Serial.println("[ADMIN] Teacher fingerprint enrolled successfully");
    } else {
      const char *line1 = nullptr;
      const char *line2 = nullptr;
      getFriendlyTeacherEnrollError(fingerprint_get_last_enroll_error(), &line1,
                                    &line2);

      display_clear();
      display_header("ENROLL FAILED");
      display_message_multi(line1, line2, "Try again");
      display_update();
      delay(2000);
      Serial.println("[ADMIN] Teacher enrollment failed");
    }

    // Safety: Ensure LED is off after enrollment (success or fail)
    fingerprint_led_off();

    enrolling = false;
    stateManager.setState(STATE_ADMIN_TEACHER_LIST);
  }
}

void handleSafeResetConfirm() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck < 250)
    return;
  lastCheck = now;

  uint16_t matchedId;
  if (!fingerprint_verify(&matchedId)) {
    return;
  }

  if (matchedId != ADMIN_FP_ID) {
    ui_show_status("DENIED", "Admin Only", 1500);
    stateManager.setState(STATE_ADMIN_MENU);
    return;
  }

  fp_map_reset();
  int deletedCsv = clear_attendance_csv_files();

  char msg[24];
  snprintf(msg, sizeof(msg), "CSV cleared: %d", deletedCsv);
  ui_show_status("SAFE RESET", msg, 1800);
  Serial.print("[ADMIN] Safe reset complete (data only), CSV deleted: ");
  Serial.println(deletedCsv);

  stateManager.setState(STATE_ADMIN_MENU);
}

// ============================================================
// PHASE 10: GFM ENROLLMENT POLLING
// ============================================================
void handleGfmEnrollWait() {
  static unsigned long lastPoll = 0;
  static int unreachablePolls = 0;
  unsigned long now = millis();

  // Poll every 1.5 seconds for faster responsiveness
  if (now - lastPoll > 1500) {
    lastPoll = now;

    int roll = 0;
    String name = "";
    Serial.println("[MAIN] Polling GFM enrollment queue...");

    if (remote_config_poll_queue(roll, name)) {
      unreachablePolls = 0;
      // Found student!
      gfm_ui_set_student(roll, name);
      stateManager.setState(STATE_GFM_ENROLLING);
    } else if (!remote_config_last_poll_reachable()) {
      unreachablePolls++;
      Serial.print("[MAIN] GFM queue poll failed, code=");
      Serial.print(remote_config_last_poll_code());
      Serial.print(" misses=");
      Serial.println(unreachablePolls);

      if (unreachablePolls >= 3) {
        ui_show_status("SERVER", "Queue offline", 1500);
        unreachablePolls = 0;
        stateManager.setState(STATE_GFM_MODE);
      }
    } else {
      unreachablePolls = 0;
    }
  }
}

// ============================================================
// PHASE 13: AUTO-SYNC
// ============================================================
void checkAutoSync() {
  static unsigned long lastSyncCheck = 0;
  static unsigned long lastStatusUpdate = 0;
  static unsigned long lastDiscoveryCheck = 0;
  unsigned long now = millis();

  // 1. Update Pending Status Cache (Every 10 seconds, regardless of WiFi)
  // This ensures the UI "(!) Data Pending" indicator is accurate without
  // flooding SD
  if (now - lastStatusUpdate > 10000) {
    session_update_pending_status();
    lastStatusUpdate = now;
  }

  // 2. Auto-Sync Upload (Every 60 seconds, only if WiFi connected)
  if (now - lastSyncCheck < 60000)
    return;
  lastSyncCheck = now;

  SystemState state = stateManager.getState();
  if (state != STATE_HOME && state != STATE_MENU)
    return;

  if (wifi_is_connected()) {
    // 2a. Check if server URL is stale (different subnet) every 5 minutes
    if (now - lastDiscoveryCheck > 300000) {
      lastDiscoveryCheck = now;
      // Quick probe: try the current server URL
      // If it fails, run discovery in the background
      String currentUrl = remote_config_get_server_url();
      if (currentUrl.length() > 0) {
        HTTPClient http;
        wifi_http_begin(http, currentUrl + "/api/server-info");
        http.setConnectTimeout(1000);
        http.setTimeout(2000);
        int code = http.GET();
        http.end();
        if (code != 200) {
          Serial.println("[SYNC] Server unreachable, running discovery...");
          remote_config_auto_discover_server();
        }
      }
    }

    // 2b. Sync NTP time every 12 hours
    static unsigned long lastNtpSync = 0;
    if (lastNtpSync == 0 || (now - lastNtpSync > 43200000)) {
      Serial.println("[MAIN] Performing background NTP time sync...");
      if (rtc_sync_ntp()) {
        lastNtpSync = now;
      } else {
        // If it failed, retry in 1 hour (retry delay = 1 hour, so lastNtpSync offset by 11 hours)
        lastNtpSync = now - 39600000;
      }
    }

    // Use the cached state first to avoid SD access if not needed
    if (session_get_pending_state()) {
      power_set_busy(true);
      Serial.println("[SYNC] Auto-sync started...");
      ui_show_status("SYNC", "Uploading...", 1000);

      // This function will access SD, which is fine here (infrequent)
      int count = session_sync_all_pending();

      if (count > 0) {
        Serial.printf("[SYNC] Auto-sync complete. Uploaded %d sessions.\n",
                      count);
        ui_show_status("SYNC", "Complete", 2000);

        // Update status immediately after sync
        session_update_pending_status();
      }
      power_set_busy(false);
    }
  }
}
