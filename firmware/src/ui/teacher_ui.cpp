#include "teacher_ui.h"
#include "../config/remote_config_loader.h"
#include "../core/session_manager.h"
#include "../core/system_state.h"
#include "../hal/button_hal.h"
#include "../hal/display_hal.h"
#include "../hal/fingerprint_hal.h"
#include "../hal/fp_mapping_hal.h"
#include "../hal/wifi_hal.h"
#include "../security/teacher_auth.h"
#include "lecture_history_ui.h"
#include <vector>

// ============================================================
// STATE
// ============================================================
static std::vector<String> subjects;
static int subjectSelection = 0;
static String currentSubject = "";

// GFM Enrollment State
static enum {
  SUB_MENU,
  SUB_ENROLL_WAIT,
  SUB_ENROLL_CONFIRM
} subState = SUB_MENU;
static int enrollRoll = 0;
static String enrollName = "";

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
// HELPER: Parse Subjects
// ============================================================
void parse_subjects() {
  subjects.clear();
  String subStr = "";

  std::vector<Teacher> &list = teacher_get_list();
  String currentName = teacher_get_current();

  for (const auto &t : list) {
    if (t.name == currentName) {
      subStr = t.subjects;
      break;
    }
  }

  if (subStr.length() == 0) {
    subStr = "All";
  }

  // Add Management Options at TOP for better visibility
  subjects.push_back("Upload My Sessions");
  subjects.push_back("View History");

  // Split by comma
  int lastIndex = 0;
  int index = subStr.indexOf(',');
  while (index != -1) {
    subjects.push_back(subStr.substring(lastIndex, index));
    lastIndex = index + 1;
    index = subStr.indexOf(',', lastIndex);
  }
  subjects.push_back(subStr.substring(lastIndex));

  // Add GFM Specific Options
  Teacher *t = teacher_get_current_obj();
  if (t && t->isGfm) {
    subjects.push_back("WiFi Settings");
    subjects.push_back("Upload Sessions (GFM)");
    subjects.push_back("Enroll Student");
    subjects.push_back("Reset Student FPs");
  }
}

// ============================================================
// INIT
// ============================================================
void teacher_ui_display() {
  display_clear();
  display_header("SELECT SUBJECT");

  if (subjects.empty()) {
    display_message("No subjects found");
  } else {
    // Build menu array
    const char *menuItems[subjects.size()];
    for (size_t i = 0; i < subjects.size(); i++) {
      menuItems[i] = subjects[i].c_str();
    }
    display_menu(menuItems, subjects.size(), subjectSelection);
  }
  display_update();
}

void teacher_ui_init() {
  subjectSelection = 0;
  subState = SUB_MENU;
  enrollRoll = 0;

  parse_subjects();
  teacher_ui_display();
}

// ============================================================
// HANDLE INPUT
// ============================================================
void teacher_ui_handle_input(int event) {
  SystemState state = stateManager.getState();

  // --------------------------------------------------------
  // SUBJECT SELECTION STATE
  // --------------------------------------------------------
  if (state == STATE_SUBJECT_SELECT) {
    if (event == 1) { // UP
      if (subjectSelection > 0)
        subjectSelection--;
      teacher_ui_display();
    } else if (event == 2) { // DOWN
      if (subjectSelection < (int)subjects.size() - 1)
        subjectSelection++;
      teacher_ui_display();
    } else if (event == 3) { // SELECT
      currentSubject = subjects[subjectSelection];

      // -- SYNC MY SESSIONS --
      if (currentSubject == "Upload My Sessions") {
        display_clear();
        display_header("UPLOADING");

        // Force Connect if needed
        if (!wifi_is_connected()) {
          display_message("Connecting WiFi...");
          display_update();
          if (!wifi_reconnect()) {
            display_clear();
            display_header("ERROR");
            display_message("WiFi Fail");
            display_update();
            delay(2000);
            teacher_ui_init(); // Redraw menu
            return;
          }
        }

        display_message("Connecting...");
        display_update();

        String teacher = teacher_get_current();
        int count = session_sync_pending(teacher);

        display_clear();
        display_header("SYNC COMPLETE");
        char buf[20];
        snprintf(buf, 20, "Uploaded: %d", count);
        display_message(buf);
        display_update();
        delay(2000);

        teacher_ui_init();
        return;
      }

      // -- VIEW HISTORY --
      if (currentSubject == "View History") {
        String teacher = teacher_get_current();
        lecture_history_load(teacher);
        stateManager.setState(STATE_LECTURE_HISTORY);
        return;
      }

      // -- WIFI SETTINGS --
      if (currentSubject == "WiFi Settings") {
        display_clear();
        display_header("WIFI STATUS");
        if (wifi_is_connected()) {
          display_message_multi("Connected:", wifi_get_ip().c_str(), NULL);
        } else {
          display_message("Not Connected");
        }
        display_update();
        delay(2000);
        teacher_ui_init();
        return;
      }

      // -- UPLOAD SESSIONS (GFM) --
      if (currentSubject == "Upload Sessions (GFM)") {
        display_clear();
        display_header("UPLOADING");

        // Force Connect if needed
        if (!wifi_is_connected()) {
          display_message("Connecting WiFi...");
          display_update();
          if (!wifi_reconnect()) {
            display_clear();
            display_header("ERROR");
            display_message("WiFi Fail");
            display_update();
            delay(2000);
            teacher_ui_init(); // Redraw menu
            return;
          }
        }

        display_message("Syncing all...");
        display_update();

        // Sync all pending sessions from all teachers
        int totalCount = session_sync_all_pending();

        display_clear();
        display_header("UPLOAD DONE");
        char buf[20];
        snprintf(buf, 20, "Records: %d", totalCount);
        display_message(buf);
        display_update();
        delay(2000);
        teacher_ui_init();
        return;
      }

      // -- RESET STUDENT FPS (GFM) --
      if (currentSubject == "Reset Student FPs") {
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
        teacher_ui_init();
        return;
      }

      // -- ENROLL STUDENT (GFM) --
      if (currentSubject == "Enroll Student") {
        subState = SUB_ENROLL_WAIT;
        enrollRoll = 0;

        display_clear();
        display_header("ENROLL MODE");
        display_message_multi("Waiting for", "Dashboard...", NULL);
        display_update();

        stateManager.setState(STATE_TEACHER_LOGIN);
        return;
      }

      // -- START SESSION --
      if (session_start(teacher_get_current(), currentSubject)) {
        stateManager.setState(STATE_TEACHER_LOGIN);
        display_clear();
        display_header(currentSubject.c_str());
        display_message("Ready to scan...");
        display_update();
      }
    } else if (event == 4) { // BACK
      teacher_logout();
      stateManager.setState(STATE_MENU);
    }
  }

  // --------------------------------------------------------
  // SESSION ACTIVE / ENROLL MODE STATE
  // --------------------------------------------------------
  else if (state == STATE_TEACHER_LOGIN) {

    // -- ENROLL WAITING MODE --
    if (subState == SUB_ENROLL_WAIT) {
      if (event == 4) { // BACK - Exit enroll mode
        subState = SUB_MENU;
        stateManager.setState(STATE_SUBJECT_SELECT);
        teacher_ui_init();
        return;
      }
      // Polling is handled in teacher_ui_loop()
    }

    // -- ENROLL CONFIRM MODE --
    else if (subState == SUB_ENROLL_CONFIRM) {
      if (event == 3) { // SELECT - Start enrollment
        display_clear();
        display_header("ENROLL STUDENT");
        char infoBuf[30];
        snprintf(infoBuf, 30, "Roll: %u", (unsigned int)enrollRoll);
        display_message_multi(enrollName.c_str(), infoBuf, "Place finger now");
        display_update();
        delay(150);

        // Map high roll number to internal sensor slot
        uint16_t internalId = fp_map_get_internal_id(enrollRoll);
        bool isReEnroll = (internalId != 0);
        if (internalId == 0) {
          internalId = fp_map_get_next_free_id();
        }

        if (internalId == 0) {
          display_clear();
          display_header("ENROLL FAILED");
          display_message_multi("No free FP slot", "Reset Student FPs", "Try again");
          display_update();
          delay(2000);

          subState = SUB_ENROLL_WAIT;
          enrollRoll = 0;
          display_clear();
          display_header("ENROLL MODE");
          display_message_multi("Waiting for", "Dashboard...", NULL);
          display_update();
          return;
        }

        if (isReEnroll) {
          fingerprint_delete(internalId);
        }

        if (fingerprint_enroll(internalId)) {
          // Success! Save the mapping to SD
          fp_map_register(internalId, enrollRoll, enrollName);

          display_clear();
          display_header("SUCCESS");
          char buf[30];
          snprintf(buf, 30, "Roll %u Enrolled", (unsigned int)enrollRoll);
          display_message(buf);
          display_update();
          delay(2000);
        } else {
          const char *line1 = nullptr;
          const char *line2 = nullptr;
          getFriendlyEnrollError(fingerprint_get_last_enroll_error(), &line1,
                                 &line2);
          display_clear();
          display_header("ENROLL FAILED");
          display_message_multi(line1, line2, "Try again");
          display_update();
          delay(2000);
        }

        // Return to waiting
        subState = SUB_ENROLL_WAIT;
        enrollRoll = 0;
        display_clear();
        display_header("ENROLL MODE");
        display_message_multi("Waiting for", "Dashboard...", NULL);
        display_update();

      } else if (event == 4) { // BACK - Skip this student
        subState = SUB_ENROLL_WAIT;
        enrollRoll = 0;
        display_clear();
        display_header("ENROLL MODE");
        display_message_multi("Waiting for", "Dashboard...", NULL);
        display_update();
      }
    }

    // -- NORMAL SESSION MODE --
    else {
      if (event == 4) { // BACK
        display_clear();
        display_header(currentSubject.c_str());
        display_message_multi("Scan Teacher FP", "to End Session", NULL);
        display_update();
        delay(1500);

        display_clear();
        display_header(currentSubject.c_str());
        display_message("Ready to scan...");
        display_update();
      }
    }
  }
}

// ============================================================
// LOOP (Called continuously)
// ============================================================
void teacher_ui_loop() {
  if (stateManager.getState() != STATE_TEACHER_LOGIN)
    return;

  // -- ENROLL POLLING --
  if (subState == SUB_ENROLL_WAIT) {
    static unsigned long lastPoll = 0;
    unsigned long now = millis();

    if (now - lastPoll > 2000) {
      lastPoll = now;
      int r;
      String n;
      if (remote_config_poll_queue(r, n)) {
        enrollRoll = r;
        enrollName = n;
        subState = SUB_ENROLL_CONFIRM;

        display_clear();
        display_header("AUTHORIZE?");
        char buf[20];
        snprintf(buf, 20, "Roll: %d", enrollRoll);
        display_message_multi(enrollName.c_str(), buf, "OK=Enroll, BACK=Skip");
        display_update();
      }
    }
    return;
  }

  // -- FINGERPRINT SCANNING --
  if (subState == SUB_MENU) {
    uint16_t scannedId;
    if (fingerprint_verify(&scannedId)) {
      // Check if it's the teacher ending the session
      Teacher *t = teacher_get_current_obj();
      if (t && t->fingerprintId == scannedId) {
        session_end();

        display_clear();
        display_header("SESSION ENDED");
        display_message_multi("Teacher Verified", "SELECT = Upload Now",
                              "BACK = Later");
        display_update();

        // Wait for decision
        unsigned long promptStart = millis();
        bool handled = false;
        while (millis() - promptStart < 5000) { // 5 second timeout
          ButtonEvent e = button_poll();
          if (e == BTN_SELECT) { // SELECT - Upload Now
            display_clear();
            display_header("UPLOADING");
            display_message("Connecting...");
            display_update();
            int count = session_sync_pending(teacher_get_current());
            display_clear();
            display_header("SYNC COMPLETE");
            char buf[20];
            snprintf(buf, 20, "Uploaded: %d", count);
            display_message(buf);
            display_update();
            delay(1500);
            handled = true;
            break;
          } else if (e == BTN_BACK) { // BACK - Later
            handled = true;
            break;
          }
          delay(10);
        }

        stateManager.setState(STATE_SUBJECT_SELECT);
        teacher_ui_init();
        return;
      }

      // Record Attendance (Student)
      if (session_record_attendance(scannedId)) {
        char buf[20];
        snprintf(buf, 20, "ID: %d Recorded", scannedId);

        display_clear();
        display_header(currentSubject.c_str());
        display_message(buf);
        display_update();
        delay(1000);
      } else {
        display_clear();
        display_header(currentSubject.c_str());
        display_message("Already Recorded");
        display_update();
        delay(1000);
      }

      display_clear();
      display_header(currentSubject.c_str());
      display_message("Ready to scan...");
      display_update();
    }
  }
}

// ============================================================
// REMOTE TRIGGER (GFM) - Called from main.cpp
// ============================================================
void teacher_ui_trigger_enroll(int roll, String name) {
  subState = SUB_ENROLL_CONFIRM;
  enrollRoll = roll;
  enrollName = name;

  display_clear();
  display_header("AUTHORIZE?");
  char buf[20];
  snprintf(buf, 20, "Roll: %d", roll);
  display_message_multi(name.c_str(), buf, "OK=Enroll, BACK=Skip");
  display_update();
}
