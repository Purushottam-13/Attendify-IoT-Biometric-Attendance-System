/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 10
 *  GFM Mode UI - Implementation
 * ============================================================
 */

#include "gfm_ui.h"
#include "../core/system_state.h"
#include "../security/teacher_auth.h"

// Extern for WiFi caller state (defined in ui_renderer.cpp)
extern SystemState wifiCallerState;
#include "../config/remote_config_loader.h"
#include "../config/config_manager.h"
#include "../core/device_info.h"
#include "../core/session_manager.h"
#include "../hal/button_hal.h"
#include "../hal/display_hal.h"
#include "../hal/fingerprint_hal.h"
#include "../hal/wifi_hal.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

// ============================================================
// STATE
// ============================================================
static int menuSelection = 0;
// static const int MENU_COUNT = 4;
static String pendingStudentName = "";
static String pendingStudentRoll = "";

static const int MENU_COUNT = 5;
static const char *menuItems[] = {"Enroll Student", "Upload Sessions",
                                  "Reset Student FPs", "WiFi Connect",
                                  "Enroll Offline"};
static bool inSubMenu = false;
static int subMenuSelection = 0;

static String compactLine(const String &text, size_t maxLen) {
  if (text.length() <= maxLen) {
    return text;
  }
  if (maxLen <= 3) {
    return text.substring(0, maxLen);
  }
  return text.substring(0, maxLen - 3) + "...";
}

static void getFriendlyEnrollError(FingerprintEnrollError error,
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
    *line2 = "Use same finger twice";
    return;
  case FP_ENROLL_ERROR_IMAGE_CONVERT_1:
  case FP_ENROLL_ERROR_IMAGE_CONVERT_2:
    *line1 = "Image quality low";
    *line2 = "Clean sensor/finger";
    return;
  case FP_ENROLL_ERROR_COMMUNICATION:
    *line1 = "Sensor comm error";
    *line2 = "Check wiring";
    return;
  case FP_ENROLL_ERROR_STORE_FAILED:
    *line1 = "Store failed";
    *line2 = "Try another finger";
    return;
  case FP_ENROLL_ERROR_ID_OUT_OF_RANGE:
    *line1 = "Invalid slot ID";
    *line2 = "Contact admin";
    return;
  default:
    *line1 = "Enrollment failed";
    *line2 = "Try again";
    return;
  }
}

static void notifyEnrollmentResult(const String &roll, uint16_t fingerprintId,
                                   const char *result) {
  if (!wifi_is_connected() || roll.length() == 0) {
    return;
  }

  HTTPClient http;
  String url = remote_config_build_url("/api/confirm-enrollment");
  wifi_http_begin(http, url);
  http.setConnectTimeout(2500);
  http.setTimeout(5000); // Allow ample time for cloud database write confirmations
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-id", device_get_id());
  http.addHeader("x-device-secret", DEVICE_SECRET);

  String json = "{\"roll\":\"" + roll + "\",\"fingerprintId\":" +
                String(fingerprintId) + ",\"result\":\"" +
                String(result) + "\"}";
  int code = http.POST(json);
  if (code < 200 || code >= 300) {
    Serial.printf("[GFM_UI] confirm-enrollment failed: HTTP %d | roll=%s | result=%s\n",
                  code, roll.c_str(), result);
  } else {
    Serial.printf("[GFM_UI] confirm-enrollment ok: roll=%s | result=%s\n",
                  roll.c_str(), result);
  }
  http.end();
}

// ============================================================
// INITIALIZATION
// ============================================================
void gfm_ui_init() {
  menuSelection = 0;
  pendingStudentName = "";
  pendingStudentRoll = "";
  Serial.println("[GFM_UI] Initialized");
}

// ============================================================
// SHOW MENU
// ============================================================
void gfm_ui_show_menu() { display_menu(menuItems, MENU_COUNT, menuSelection); }

// Enrollment queue polling is now handled by remote_config_poll_queue() in
// remote_config_loader.cpp

#include "../hal/fp_mapping_hal.h"

// ... existing code ...

// ============================================================
// ENROLL STUDENT FINGERPRINT
// ============================================================
static bool enrollStudentFingerprint() {
  if (pendingStudentRoll.length() == 0) {
    return false;
  }

  uint32_t roll = (uint32_t)pendingStudentRoll.toInt();
  if (roll == 0)
    return false;

  // Map roll number to internal sensor slot
  uint16_t internalId = fp_map_get_internal_id(roll);
  bool isReEnroll = (internalId != 0);
  if (internalId == 0) {
    internalId = fp_map_get_next_free_id();
  }

  if (internalId == 0) {
    display_clear();
    display_header("ENROLL FAILED");
    display_message("No free FP slot");
    display_message_at(3, "Reset Student FPs");
    display_update();
    notifyEnrollmentResult(pendingStudentRoll, 0, "FAILED");
    delay(2000);
    pendingStudentName = "";
    pendingStudentRoll = "";
    return false;
  }

  display_clear();
  display_header("ENROLLING");
  char buf[50];
  snprintf(buf, 50, "%s (%s)", pendingStudentName.c_str(),
           pendingStudentRoll.c_str());
  display_message(buf);
  display_message_at(3, "Place finger...");
  display_update();

  if (isReEnroll) {
    fingerprint_delete(internalId);
  }

  // Enroll fingerprint using internal slot
  bool success = fingerprint_enroll(internalId);

  if (success) {
    // Report success to server with BOTH IDs
    fp_map_register(internalId, roll, pendingStudentName);
    notifyEnrollmentResult(pendingStudentRoll, internalId, "SUCCESS");

    display_clear();
    display_header("SUCCESS");
    display_message("Student enrolled!");
    display_update();
    delay(2000);
  } else {
    notifyEnrollmentResult(pendingStudentRoll, internalId, "FAILED");
    const char *errorLine1 = nullptr;
    const char *errorLine2 = nullptr;
    getFriendlyEnrollError(fingerprint_get_last_enroll_error(), &errorLine1,
                           &errorLine2);
    display_clear();
    display_header("FAILED");
    display_message_multi(errorLine1, errorLine2, "Try again");
    display_update();
    delay(2000);
  }

  pendingStudentName = "";
  pendingStudentRoll = "";

  return success;
}

// ============================================================
// EXTERNAL SETTERS
// ============================================================
void gfm_ui_set_student(int roll, const String &name) {
  pendingStudentRoll = String(roll);
  pendingStudentName = name;
  Serial.printf("[GFM_UI] Student set: %s (%d)\n", name.c_str(), roll);
}

// ============================================================
// HANDLE GFM MODE
// ============================================================
void gfm_ui_handle(ButtonEvent event) {
  SystemState state = stateManager.getState();

  static bool needsRedraw = true;

  // --------------------------------------------------------
  // GFM MAIN MENU
  // --------------------------------------------------------
  if (state == STATE_GFM_MODE) {
    // Note: display_clear() and display_update() are handled by the caller
    // (ui_render)
    display_header("GFM MODE");
    if (!inSubMenu) {
      gfm_ui_show_menu();
      display_message_at(6, wifi_is_connected() ? "[WiFi OK]" : "[No WiFi]");
    } else {
      const char *subItems[] = {"Ojas (Roll 12)", "Purushottam (Roll 13)"};
      display_menu(subItems, 2, subMenuSelection);
      display_message_at(6, "[<] Back to GFM");
    }

    if (inSubMenu) {
      if (event == BTN_UP) {
        subMenuSelection = (subMenuSelection > 0) ? subMenuSelection - 1 : 1;
      } else if (event == BTN_DOWN) {
        subMenuSelection = (subMenuSelection < 1) ? subMenuSelection + 1 : 0;
      } else if (event == BTN_SELECT) {
        if (subMenuSelection == 0) {
          gfm_ui_set_student(12, "Ojas");
        } else {
          gfm_ui_set_student(13, "Purushottam");
        }
        inSubMenu = false;
        stateManager.setState(STATE_GFM_ENROLLING);
      } else if (event == BTN_BACK) {
        inSubMenu = false;
      }
      return;
    }

    if (event == BTN_UP) {
      if (menuSelection > 0) {
        menuSelection--;
      } else {
        menuSelection = MENU_COUNT - 1; // Wrap-around to bottom
      }
    } else if (event == BTN_DOWN) {
      if (menuSelection < MENU_COUNT - 1) {
        menuSelection++;
      } else {
        menuSelection = 0; // Wrap-around to top
      }
    } else if (event == BTN_SELECT) {
      switch (menuSelection) {
      case 0: { // Enroll Student (manual)
        stateManager.setState(STATE_GFM_ENROLL_WAIT);
        break;
      }

      case 1: { // Upload Sessions
        display_clear();
        display_header("UPLOADING");
        display_message("Syncing all...");
        display_update();

        int count = session_sync_all_pending();

        display_clear();
        display_header("DONE");
        char buf[30];
        snprintf(buf, 30, "Uploaded: %d", count);
        display_message(buf);
        display_update();
        delay(2000);
        break;
      }

      case 2: { // Reset Student FPs
        display_clear();
        display_header("RESETTING");
        display_message("Deleting students...");
        display_update();

        fp_map_reset();

        display_clear();
        display_header("SUCCESS");
        display_message("FPs Cleared");
        display_update();
        delay(2000);
        break;
      }

      case 3: { // WiFi Connect - go to WiFi menu
        wifiCallerState = STATE_GFM_MODE;
        stateManager.setState(STATE_WIFI_MENU);
        break;
      }

      case 4: { // Enroll Offline
        inSubMenu = true;
        subMenuSelection = 0;
        break;
      }
      }
    }

    // Hardware BACK button - exit GFM mode
    else if (event == BTN_BACK) {
      teacher_logout(); // Logout GFM teacher
      stateManager.setState(STATE_HOME);
    }
  }

  // --------------------------------------------------------
  // ENROLL WAIT STATE (Waiting for signal from main.cpp)
  // --------------------------------------------------------
  else if (state == STATE_GFM_ENROLL_WAIT) {
    // Content only: ALWAYS draw every frame because ui_render() clears every
    // frame
    display_header("ENROLL QUEUE");
    display_message_multi("Waiting for", "student...", "Polling...");
    display_footer("[BACK] to cancel");

    // Polling is now handled in main.cpp to prevent blocking ui_render

    if (event == BTN_BACK) {
      stateManager.setState(STATE_GFM_MODE);
    }
  }

  // --------------------------------------------------------
  // ENROLLING STATE (Show student info, wait for GFM to confirm)
  // --------------------------------------------------------
  else if (state == STATE_GFM_ENROLLING) {
    // Content only: ui_render handles clear/update
    display_header("STUDENT FOUND");
    String nameLine = compactLine("Name: " + pendingStudentName, 20);
    String rollLine = compactLine("Roll: " + pendingStudentRoll, 20);
    display_message_multi(nameLine.c_str(), rollLine.c_str(),
                          "OK=Enroll BACK=Skip");

    if (event == BTN_SELECT) {
      // Start fingerprint enrollment
      enrollStudentFingerprint();
      // Return to GFM menu after enrollment
      stateManager.setState(STATE_GFM_MODE);
    }

    if (event == BTN_BACK) {
      // Notify server that enrollment was cancelled/skipped by GFM
      notifyEnrollmentResult(pendingStudentRoll, 0, "CANCELLED");
      // Skip this student
      pendingStudentName = "";
      pendingStudentRoll = "";
      stateManager.setState(STATE_GFM_MODE);
    }
  }
}
