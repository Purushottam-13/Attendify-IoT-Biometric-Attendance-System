/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  UI Renderer - Implementation
 * ============================================================
 */

#include "ui_renderer.h"
#include "../core/power_manager.h"
#include "../hal/display_hal.h"
#include "../hal/fingerprint_hal.h"
#include "../hal/rtc_hal.h"
#include "../hal/sd_hal.h"
#include "../hal/wifi_hal.h"
#include "../setup/setup_mode.h" // Phase 2
#include <Preferences.h>

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
static void render_boot_screen();
static void render_home_screen();
static void render_menu_screen();
static void render_attendance_screen();
static void render_wifi_screen();
static void render_about_screen();
static void render_error_screen();
static void render_setup_screen();          // Phase 2
static void render_teacher_select_screen(); // Phase 3
static void render_teacher_enroll_screen(); // Phase 3
static void render_teacher_login_screen();  // Phase 3
static void render_teacher_login_screen();  // Phase 3
static void render_admin_auth_screen();     // Phase 6
static void render_reset_confirm_screen();
static void render_admin_menu_screen();           // Phase 6
static void render_admin_teacher_list_screen();   // Phase 6
static void render_admin_teacher_enroll_screen(); // Phase 6
static void render_subject_select_screen();       // Phase 7
static void render_session_active_screen();       // Phase 7
static void render_session_summary_screen();      // Phase 7

// Phase 12: Declarations removed (logic moved to main session loop)
// Check main.cpp handleSessionActive() for implementation

// Phase 3: Teacher auth
#include "../security/teacher_auth.h"
// Phase 7: Session Manager
#include "../config/remote_config_loader.h"
#include "../core/session_manager.h"
#include "gfm_ui.h"

int teacherSelection = 0;          // Non-static for extern access from main.cpp
static int adminMenuSelection = 0; // Phase 6: Admin menu selection
static int adminTeacherSelection =
    0;                           // Phase 6: Teacher selection in admin mode
static int subjectSelection = 0; // Phase 7: Subject selection
static int subjectCount = 0;
static String subjectList[10];        // Max 10 subjects
static int teacherLoginSelection = 0; // Teacher login menu selection

// SD Card Browser State
static std::vector<String> sdFileList;
static int sdFileIndex = 0;
static bool sdListLoaded = false;
static String sdDetailFile = "";
static std::vector<String> sdDetailStudents;
static int sdDetailScrollPos = 0;
static void render_admin_sd_list_screen();
static void render_admin_sd_detail_screen();

static std::vector<String> ui_parse_csv_line(const String &line) {
  std::vector<String> fields;
  String field = "";
  bool inQuotes = false;

  for (int i = 0; i < (int)line.length(); i++) {
    char c = line.charAt(i);
    if (c == '"') {
      if (inQuotes && i + 1 < (int)line.length() && line.charAt(i + 1) == '"') {
        field += '"';
        i++;
      } else {
        inQuotes = !inQuotes;
      }
    } else if (c == ',' && !inQuotes) {
      field.trim();
      fields.push_back(field);
      field = "";
    } else {
      field += c;
    }
  }

  field.trim();
  fields.push_back(field);
  return fields;
}

static int ui_csv_column_index(const std::vector<String> &headers,
                               const char *columnName, int fallbackIndex) {
  for (int i = 0; i < (int)headers.size(); i++) {
    if (headers[i] == columnName) {
      return i;
    }
  }
  return fallbackIndex;
}

// Admin WiFi State
static String adminWifiNetworks[10];
static bool adminWifiOpen[10] = {false};
static int adminWifiCount = 0;
static int adminWifiSelection = 0;
SystemState wifiCallerState =
    STATE_HOME; // Track who called WiFi menu (non-static for extern access)

static void render_wifi_scanning_screen();
static void render_wifi_select_screen();

// Forward declaration for boot setup check
extern bool is_boot_setup_triggered();
extern void clear_boot_setup_flag();

// ============================================================
// MENU CONFIGURATION - Admin, Teacher, GFM
// ============================================================
static const char *menuItems[MENU_ITEM_COUNT] = {"Admin Mode", "Teacher Mode",
                                                 "GFM Mode"};

static int menuSelection = 0;

// ============================================================
// INITIALIZATION
// ============================================================
void ui_init() {
  menuSelection = 0;
  Serial.println("[UI] Renderer initialized");
}

// ============================================================
// INPUT HANDLING
// ============================================================
void ui_handle_input(ButtonEvent event) {
  if (event == BTN_NONE)
    return;

  power_notify_interaction();

  SystemState currentState = stateManager.getState();

  switch (currentState) {
  case STATE_HOME:
    if (event == BTN_SELECT) {
      stateManager.setState(STATE_MENU);
    }
    // Long-press removed - admin mode accessed through menu only
    break;

  case STATE_MENU:
    if (event == BTN_UP) {
      menuSelection =
          (menuSelection > 0) ? menuSelection - 1 : MENU_ITEM_COUNT - 1;
    } else if (event == BTN_DOWN) {
      menuSelection =
          (menuSelection < MENU_ITEM_COUNT - 1) ? menuSelection + 1 : 0;
    } else if (event == BTN_SELECT) {
      extern bool pendingGfmMode; // From main.cpp
      // Menu: 0=Admin Mode, 1=Teacher Mode, 2=GFM Mode
      switch (menuSelection) {
      case 0: // Admin Mode - requires admin fingerprint
        Serial.println("[UI] Selected Admin Mode");
        pendingGfmMode = false;
        stateManager.setState(STATE_ADMIN_AUTH);
        break;
      case 1: // Teacher Mode - teacher login + attendance
        Serial.println("[UI] Selected Teacher Mode");
        pendingGfmMode = false;
        stateManager.setState(STATE_TEACHER_LOGIN);
        break;
      case 2: // GFM Mode - direct GFM menu (requires GFM login)
        Serial.println("[UI] Selected GFM Mode");
        pendingGfmMode = true; // Set flag for handleTeacherLogin
        stateManager.setState(STATE_TEACHER_LOGIN); // Login first
        break;
      }
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_HOME);
    }
    break;

  case STATE_ATTENDANCE:
  case STATE_ABOUT:
    if (event == BTN_BACK) {
      stateManager.setState(STATE_MENU);
    }
    break;

  case STATE_WIFI_MENU:
    if (event == BTN_SELECT) {
      stateManager.setState(STATE_WIFI_SCANNING);
      // Force render to show "Scanning..." immediately
      render_wifi_scanning_screen();
      display_update();

      // Block and scan
      adminWifiCount = wifi_scan(adminWifiNetworks, 10, adminWifiOpen);
      adminWifiSelection = 0;

      if (adminWifiCount > 0) {
        stateManager.setState(STATE_WIFI_SELECT);
      } else {
        ui_show_status("WiFi", "No Networks", 2000);
        stateManager.setState(STATE_WIFI_MENU);
      }
    } else if (event == BTN_BACK) {
      // Return to the state that called WiFi menu
      stateManager.setState(wifiCallerState);
    }
    break;

  case STATE_WIFI_SCANNING:
    break;

  case STATE_WIFI_SELECT:
    if (event == BTN_UP) {
      adminWifiSelection = (adminWifiSelection > 0) ? adminWifiSelection - 1
                                                    : adminWifiCount;
    } else if (event == BTN_DOWN) {
      adminWifiSelection = (adminWifiSelection < adminWifiCount)
                               ? adminWifiSelection + 1
                               : 0;
    } else if (event == BTN_SELECT) {
      if (adminWifiSelection == adminWifiCount) {
        stateManager.setState(STATE_WIFI_SCANNING);
        render_wifi_scanning_screen();
        display_update();
        adminWifiCount = wifi_scan(adminWifiNetworks, 10, adminWifiOpen);
        adminWifiSelection = 0;
        if (adminWifiCount > 0) {
          stateManager.setState(STATE_WIFI_SELECT);
        } else {
          ui_show_status("WiFi", "No Networks", 2000);
          stateManager.setState(STATE_WIFI_MENU);
        }
        break;
      }
      if (!adminWifiOpen[adminWifiSelection]) {
        ui_show_status("LOCKED", "Use open hotspot", 1800);
        stateManager.setState(STATE_WIFI_SELECT);
        break;
      }

      // Connect
      ui_show_status("WiFi", "Connecting...", 0);
      if (wifi_connect(adminWifiNetworks[adminWifiSelection].c_str(), "")) {
        ui_show_status("SUCCESS", "Connected!", 1000);

        // Sync Time (Skip for teachers to save time)
        if (!teacher_is_logged_in()) {
          ui_show_status("TIME", "Syncing NTP...", 0);
          bool syncSuccess = rtc_sync_ntp();
          delay(500); // Allow I2C to settle

          if (syncSuccess) {
            ui_show_status("SUCCESS", "Time Updated", 2000);
          } else {
            ui_show_status("FAILED", "Time Sync Error", 2000);
          }
        }

        // Auto-discover server on new network
        ui_show_status("SERVER", "Finding server...", 0);
        if (remote_config_auto_discover_server()) {
          ui_show_status("SERVER", "Server Found!", 1500);
        } else {
          ui_show_status("SERVER", "Not found (offline)", 1500);
        }

        stateManager.setState(STATE_WIFI_MENU);
      } else {
        ui_show_status("FAILED", "Connection Failed", 2000);
        stateManager.setState(STATE_WIFI_SELECT); // Return to list
      }
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_WIFI_MENU);
    }
    break;

  case STATE_SETUP: // Phase 2: Route to setup handler
  {
    int setupEvent = 0;
    if (event == BTN_UP)
      setupEvent = 1;
    else if (event == BTN_DOWN)
      setupEvent = 2;
    else if (event == BTN_SELECT)
      setupEvent = 3;
    else if (event == BTN_BACK)
      setupEvent = 4;
    setup_handle_input(setupEvent);

    // Check setup completion with debounce to allow NVS to update
    static unsigned long lastSetupCheck = 0;
    if (millis() - lastSetupCheck > 500) {
      if (setup_is_completed()) {
        stateManager.setState(STATE_HOME);
      }
      lastSetupCheck = millis();
    }
  } break;

  case STATE_TEACHER_SELECT: // Phase 3
  {
    int count = teacher_get_count();
    if (event == BTN_UP) {
      teacherSelection =
          (teacherSelection > 0) ? teacherSelection - 1 : count - 1;
    } else if (event == BTN_DOWN) {
      teacherSelection =
          (teacherSelection < count - 1) ? teacherSelection + 1 : 0;
    } else if (event == BTN_SELECT) {
      stateManager.setState(STATE_TEACHER_ENROLL);
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_MENU);
    }
  } break;

  case STATE_TEACHER_ENROLL: // Phase 3
    if (event == BTN_BACK) {
      stateManager.setState(STATE_MENU);
    }
    break;

  case STATE_TEACHER_LOGIN:
    if (event == BTN_UP) {
      teacherLoginSelection =
          (teacherLoginSelection > 0) ? teacherLoginSelection - 1 : 3;
    } else if (event == BTN_DOWN) {
      teacherLoginSelection =
          (teacherLoginSelection < 3) ? teacherLoginSelection + 1 : 0;
    } else if (event == BTN_SELECT) {
      if (teacher_is_logged_in()) {
        switch (teacherLoginSelection) {
        case 0: // Take Attendance
          stateManager.setState(STATE_SUBJECT_SELECT);
          break;
        case 1: // Connect WiFi
          wifiCallerState = STATE_TEACHER_LOGIN;
          stateManager.setState(STATE_WIFI_MENU);
          break;
        case 2: // Upload My Sessions
        {
          ui_show_status("UPLOADING", "Connecting...", 0);
          String teacher = teacher_get_current();
          int count = session_sync_pending(teacher);
          char buf[30];
          snprintf(buf, 30, "Uploaded: %d", count);
          ui_show_status("SUCCESS", buf, 2000);
          stateManager.setState(STATE_TEACHER_LOGIN);
        } break;
        case 3: // Logout
          teacher_logout();
          Serial.println("[UI] Teacher logged out");
          stateManager.setState(STATE_MENU);
          break;
        }
      }
    } else if (event == BTN_BACK) {
      if (teacher_is_logged_in()) {
        teacher_logout();
        Serial.println("[UI] Teacher logged out");
      }
      stateManager.setState(STATE_MENU);
    }
    break;

  // ============================================================
  // PHASE 6: ADMIN MODE INPUT HANDLING
  // ============================================================
  case STATE_ADMIN_AUTH:
    // Admin fingerprint verification happens in main.cpp handleAdminAuth()
    if (event == BTN_BACK) {
      stateManager.setState(STATE_HOME);
    }
    break;

  case STATE_ADMIN_MENU:
    if (event == BTN_UP) {
      adminMenuSelection =
          (adminMenuSelection > 0) ? adminMenuSelection - 1 : 4;
    } else if (event == BTN_DOWN) {
      adminMenuSelection =
          (adminMenuSelection < 4) ? adminMenuSelection + 1 : 0;
    } else if (event == BTN_SELECT) {
      switch (adminMenuSelection) {
      case 0: // Authorize Teachers
        adminTeacherSelection = 0;
        stateManager.setState(STATE_ADMIN_TEACHER_LIST);
        break;
      case 1: // WiFi Settings
        wifiCallerState = STATE_ADMIN_MENU;
        stateManager.setState(STATE_WIFI_MENU);
        break;
      case 2:                 // SD Card Viewer
        sdListLoaded = false; // Force reload
        sdFileIndex = 0;
        stateManager.setState(STATE_ADMIN_SD_LIST);
        break;
      case 3: // Safe Reset (Data Only)
        stateManager.setState(STATE_RESET_CONFIRM);
        break;
      case 4: // Exit
        stateManager.setState(STATE_HOME);
        break;
      }
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_HOME);
    }
    break;

  case STATE_ADMIN_SD_LIST:
    if (event == BTN_UP) {
      sdFileIndex = (sdFileIndex > 0)
                        ? sdFileIndex - 1
                        : (sdFileList.empty() ? 0 : sdFileList.size() - 1);
    } else if (event == BTN_DOWN) {
      sdFileIndex =
          (sdFileIndex < (int)sdFileList.size() - 1) ? sdFileIndex + 1 : 0;
    } else if (event == BTN_SELECT && !sdFileList.empty()) {
      // Load Details
      sdDetailFile = sdFileList[sdFileIndex];
      sdDetailStudents.clear();
      sdDetailScrollPos = 0;

      String content = sd_read_file(sdDetailFile.c_str());
      if (content.length() > 0) {
        int curPos = 0;
        int lineNum = 0;
        int studentIdCol = 6;
        int studentNameCol = 7;
        while (curPos < (int)content.length()) {
          int nextNL = content.indexOf('\n', curPos);
          if (nextNL == -1)
            nextNL = content.length();
          String line = content.substring(curPos, nextNL);
          line.trim();
          curPos = nextNL + 1;

          if (lineNum == 0 && line.length() > 0) {
            std::vector<String> headers = ui_parse_csv_line(line);
            studentIdCol =
                ui_csv_column_index(headers, "StudentID", studentIdCol);
            studentNameCol =
                ui_csv_column_index(headers, "StudentName", studentNameCol);
          } else if (line.length() > 5) {
            std::vector<String> fields = ui_parse_csv_line(line);
            String sid = (studentIdCol >= 0 && studentIdCol < (int)fields.size())
                             ? fields[studentIdCol]
                             : "";
            String sname =
                (studentNameCol >= 0 && studentNameCol < (int)fields.size())
                    ? fields[studentNameCol]
                    : "";
            if (sid.length() > 0) {
              sdDetailStudents.push_back(sname.length() > 0 ? sid + " " + sname
                                                            : sid);
            }
          }
          lineNum++;
        }
      }
      stateManager.setState(STATE_ADMIN_SD_DETAIL);
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_ADMIN_MENU);
    }
    break;

  case STATE_ADMIN_SD_DETAIL:
    if (event == BTN_UP && sdDetailScrollPos > 0) {
      sdDetailScrollPos--;
    } else if (event == BTN_DOWN &&
               sdDetailScrollPos < (int)sdDetailStudents.size() - 3) {
      sdDetailScrollPos++;
    } else if (event == BTN_SELECT) {
      // DELETE FILE logic - directly delete and refresh
      if (sd_delete_file(sdDetailFile.c_str())) {
        ui_show_status("SD", "File Deleted", 2000);
        sdListLoaded = false; // Force re-scan on return
        stateManager.setState(STATE_ADMIN_SD_LIST);
      } else {
        ui_show_status("ERROR", "Delete Failed", 2000);
      }
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_ADMIN_SD_LIST);
    }
    break;

  case STATE_RESET_CONFIRM:
    if (event == BTN_BACK) {
      stateManager.setState(STATE_ADMIN_MENU);
    } else {
      // Check fingerprint whenever placed (looping in renderer/HAL?)
      // Actually ui_handle_input is event driven.
      // We need to poll fingerprint in main loop or trigger here?
      // The main loop calls ui_handle_input logic via state machine?
      // No, main loop usually handles sensors.
      // Assuming main.cpp handles fingerprint verify if not in specific inputs.
      // Let's implement the verification check here if logical:
      // Actually main.cpp usually has the logic for verification states.
      // For simplicity, I'll rely on the main loop calling a verification
      // helper? No, I should check fingerprint here if possible, OR main.cpp
      // needs an update. Given constraints, I'll add logic to CHECK fingerprint
      // ON SELECTION? But user wants "Place Finger". I'll assume main.cpp
      // routes fingerprint matches to specific handlers? No, main.cpp lines
      // 356+ handle `STATE_ADMIN_AUTH`. I need to add `STATE_RESET_CONFIRM` to
      // main.cpp? Wait, if I add it to main.cpp it's safer. But I can do it
      // here if I poll. Let's modify main.cpp after this to handle logic? For
      // now, I'll add the Place Finger prompt. Reset happens if Fingerprint
      // matched.
    }
    break;

  case STATE_ADMIN_TEACHER_LIST: {
    int count = teacher_get_count();
    if (event == BTN_UP) {
      adminTeacherSelection =
          (adminTeacherSelection > 0) ? adminTeacherSelection - 1 : count - 1;
    } else if (event == BTN_DOWN) {
      adminTeacherSelection =
          (adminTeacherSelection < count - 1) ? adminTeacherSelection + 1 : 0;
    } else if (event == BTN_SELECT) {
      teacherSelection = adminTeacherSelection; // Set for enrollment
      stateManager.setState(STATE_ADMIN_TEACHER_ENROLL);
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_ADMIN_MENU);
    }
  } break;

  case STATE_ADMIN_TEACHER_ENROLL:
    // Enrollment happens in main.cpp handleAdminTeacherEnroll()
    if (event == BTN_BACK) {
      stateManager.setState(STATE_ADMIN_TEACHER_LIST);
    }
    break;

  // ============================================================
  // PHASE 7: SESSION INPUT HANDLING
  // ============================================================
  case STATE_SUBJECT_SELECT: {
    // Parse subjects from current teacher
    // For now, assume subjects are comma-separated in teacher struct
    // We need to parse them dynamically or cache them
    // Simplified: Just 2 dummy subjects if not parsed

    if (event == BTN_UP) {
      subjectSelection =
          (subjectSelection > 0) ? subjectSelection - 1 : subjectCount - 1;
    } else if (event == BTN_DOWN) {
      subjectSelection =
          (subjectSelection < subjectCount - 1) ? subjectSelection + 1 : 0;
    } else if (event == BTN_SELECT) {
      // Start session with selected subject
      if (subjectCount > 0) {
        session_start(teacher_get_current(), subjectList[subjectSelection]);
        stateManager.setState(STATE_SESSION_ACTIVE);
      }
    } else if (event == BTN_BACK) {
      stateManager.setState(STATE_TEACHER_LOGIN);
    }
  } break;

  case STATE_SESSION_ACTIVE:
    if (event == BTN_BACK) {
      // Show hint
      ui_show_status("TIP", "Scan Finger to End", 2000);
    }
    break;
    // STATE_SESSION_CONFIRM case removed as requested

  case STATE_SESSION_SUMMARY:
    if (event == BTN_SELECT || event == BTN_BACK) {
      stateManager.setState(STATE_HOME);
    }
    break;

  // Phase 10: GFM Mode - delegate to gfm_ui
  case STATE_GFM_MODE:
  case STATE_GFM_ENROLL_WAIT:
  case STATE_GFM_ENROLLING:
    gfm_ui_handle(event);
    break;

  default:
    break;
  }
}

// ============================================================
// SCREEN RENDERING
// ============================================================
void ui_render() {
  display_clear();

  SystemState currentState = stateManager.getState();

  switch (currentState) {
  case STATE_BOOT:
    render_boot_screen();
    break;
  case STATE_HOME:
    render_home_screen();
    break;
  case STATE_MENU:
    render_menu_screen();
    break;
  case STATE_ATTENDANCE:
    render_attendance_screen();
    break;
  case STATE_WIFI_MENU:
    render_wifi_screen();
    break;
  case STATE_WIFI_SCANNING:
    render_wifi_scanning_screen();
    break;
  case STATE_WIFI_SELECT:
    render_wifi_select_screen();
    break;
  case STATE_ABOUT:
    render_about_screen();
    break;

  case STATE_ERROR:
    render_error_screen();
    break;
  case STATE_SETUP: // Phase 2
    render_setup_screen();
    break;
  case STATE_TEACHER_SELECT: // Phase 3
    render_teacher_select_screen();
    break;
  case STATE_TEACHER_ENROLL: // Phase 3
    render_teacher_enroll_screen();
    break;
  case STATE_TEACHER_LOGIN: // Phase 3
    render_teacher_login_screen();
    break;
  // Phase 6: Admin Mode screens
  case STATE_ADMIN_AUTH:
    render_admin_auth_screen();
    break;
  case STATE_ADMIN_MENU:
    render_admin_menu_screen();
    break;
  case STATE_ADMIN_SD_LIST:
    render_admin_sd_list_screen();
    break;
  case STATE_ADMIN_SD_DETAIL:
    render_admin_sd_detail_screen();
    break;
  case STATE_ADMIN_TEACHER_LIST:
    render_admin_teacher_list_screen();
    break;
  case STATE_ADMIN_TEACHER_ENROLL:
    render_admin_teacher_enroll_screen();
    break;
  case STATE_RESET_CONFIRM:
    render_reset_confirm_screen();
    break;
  // Phase 7: Session Screens
  case STATE_SUBJECT_SELECT:
    render_subject_select_screen();
    break;
  case STATE_SESSION_ACTIVE:
    render_session_active_screen();
    break;
  case STATE_SESSION_SUMMARY:
    render_session_summary_screen();
    break;
  // Phase 10: GFM states - rendering handled by gfm_ui_handle()
  case STATE_GFM_MODE:
  case STATE_GFM_ENROLL_WAIT:
  case STATE_GFM_ENROLLING:
    gfm_ui_handle(BTN_NONE); // Render only, no button event
    break;
  }

  display_update();
}

// ============================================================
// INDIVIDUAL SCREENS (static implementations)
// ============================================================
static void render_boot_screen() { display_boot_screen(); }

static void render_home_screen() {
  display_header("ATTENDIFY");

  String statusLine = wifi_is_connected() ? "WiFi: Connected" : "WiFi: Offline";

  if (session_has_pending()) {
    if (millis() % 1500 < 750) {
      statusLine = "(!) Data Pending";
    } else {
      statusLine = "    Data Pending";
    }
  }

  String timeStr = rtc_time_string();
  if (millis() % 1000 < 500) {
    for (int i = 0; i < timeStr.length(); i++) {
      if (timeStr[i] == ':') {
        timeStr[i] = ' ';
      }
    }
  }

  display_message_multi(timeStr.c_str(),
                        rtc_get_date_string().c_str(), statusLine.c_str());
  display_footer("[OK] Menu");
}

static void render_menu_screen() {
  // Show WiFi status in header
  char header[20];
  snprintf(header, 20, "MENU %s", wifi_is_connected() ? "[WiFi]" : "[No WiFi]");
  display_header(header);
  display_menu(menuItems, MENU_ITEM_COUNT, menuSelection);
  display_footer("[OK] Select [<] Back");
}

static void render_attendance_screen() {
  display_header("ATTENDANCE");
  display_message_multi("Place your finger", "on the sensor", NULL);
  display_footer("[<] Cancel");
}

static void render_wifi_screen() {
  display_header("WiFi STATUS");

  if (wifi_is_connected()) {
    char ipBuf[20];
    snprintf(ipBuf, sizeof(ipBuf), "IP: %s", wifi_get_ip().c_str());
    display_message_multi("Connected:", wifi_get_ssid().c_str(), ipBuf);
  } else {
    display_message_multi("Status: Disconnected", "Press OK to scan", NULL);
  }

  display_footer("[OK] Scan [<] Back");
}

static void render_wifi_scanning_screen() {
  display_clear();
  display_header("WiFi");
  display_message("Scanning...");
}

static void render_wifi_select_screen() {
  display_header("SELECT NETWORK");

  if (adminWifiCount == 0) {
    display_message("No networks");
    display_footer("[<] Back");
    return;
  }

  const char *items[11];
  char nameBuf[10][25];
  int i = 0;
  for (i = 0; i < adminWifiCount && i < 10; i++) {
    String label = adminWifiNetworks[i];
    if (!adminWifiOpen[i]) {
      label += " [L]";
    }
    label.toCharArray(nameBuf[i], 25);
    items[i] = nameBuf[i];
  }

  items[i] = "Rescan...";
  display_menu(items, min(adminWifiCount + 1, 11), adminWifiSelection);
  display_footer("[OK] Connect [<] Back");
}

static void render_session_confirm_screen() {
  display_header("END SESSION?");
  display_message_multi("Identify Teacher", "Place Finger...", NULL);
  display_footer("[<] Cancel");
}

static void render_about_screen() {
  display_header("ABOUT");
  display_message_multi("Attendify v1.0", "Phase 1 - Foundation",
                        "Modular Architecture");
  display_footer("[<] Back");
}

static void render_error_screen() {
  display_header("ERROR");
  display_message("System Error");
  display_footer("[<] Restart");
}

// Phase 2: Setup Mode Screen - Dynamic based on step
static void render_setup_screen() {
  SetupStep step = setup_get_step();
  int selection = setup_get_selection();

  switch (step) {
  case SETUP_MENU: {
    display_header("SETUP MODE");
    const char *items[] = {"1.WiFi", "2.Config", "3.Admin",
                           "4.Time", "5.Safe",   "6.Full",
                           "7.Done"};
    display_menu(items, 7, selection);
    display_footer("[OK] Select");
  } break;

  case SETUP_WIFI:
  case SETUP_WIFI_SCANNING:
    display_header("WiFi SETUP");
    if (step == SETUP_WIFI_SCANNING) {
      display_message("Scanning...");
    } else {
      display_message_multi(wifi_is_connected() ? "Status: Connected"
                                                : "Status: Offline",
                            "[OK] Scan networks", NULL);
    }
    display_footer("[<] Back");
    break;

  case SETUP_WIFI_CONNECT: {
    display_header("WiFi NETWORKS");
    int wifiCount = setup_get_wifi_count();
    int wifiSel = setup_get_wifi_selection();

    if (wifiCount > 0) {
      const char *items[11];
      char nameBuf[10][25];
      int i = 0;
      for (i = 0; i < wifiCount && i < 10; i++) {
        String ssid = setup_get_wifi_ssid(i);
        if (!setup_get_wifi_is_open(i)) {
          ssid += " [L]";
        }
        ssid.toCharArray(nameBuf[i], 25);
        items[i] = nameBuf[i];
      }
      items[i] = "Rescan...";
      display_menu(items, min(wifiCount + 1, 11), wifiSel);
    } else {
      display_message("No networks");
    }
    display_footer("[OK] Connect [<] Back");
  } break;

  case SETUP_CONFIG:
  case SETUP_CONFIG_LOADING:
    display_header("CONFIG");
    if (step == SETUP_CONFIG_LOADING) {
      display_message("Loading...");
    } else {
      display_message_multi("Load from SD card", "/config/config.json",
                            "[OK] Load");
    }
    display_footer("[<] Back");
    break;

  case SETUP_ADMIN:
  case SETUP_ADMIN_ENROLL:
    display_header("ADMIN AUTH");
    if (step == SETUP_ADMIN_ENROLL) {
      display_message("Place finger...");
    } else {
      display_message_multi("Enroll admin", "fingerprint (ID: 1)",
                            "[OK] Start");
    }
    display_footer("[<] Back");
    break;

  case SETUP_COMPLETE:
    display_header("FINISH");
    display_message_multi("Setup complete!", "[OK] Save & Exit", "[<] Go back");
    display_footer("");
    break;

  default:
    display_header("SETUP");
    display_message("Unknown step");
    break;
  }
}

// ============================================================
// UTILITY
// ============================================================
int ui_get_menu_selection() { return menuSelection; }

void ui_show_status(const char *title, const char *message,
                    unsigned long duration_ms) {
  power_notify_interaction();

  display_clear();
  display_header(title);
  display_message(message);
  display_update();

  if (strcmp(title, "SUCCESS") == 0) {
    // Invert twice quickly for positive visual flash feedback
    display_invert(true);
    delay(80);
    display_invert(false);
    delay(80);
    display_invert(true);
    delay(80);
    display_invert(false);
    
    if (duration_ms > 240) {
      duration_ms -= 240;
    } else {
      duration_ms = 0;
    }
  }

  if (duration_ms > 0) {
    delay(duration_ms);
    power_notify_interaction();
  }
}

// ============================================================
// PHASE 3: Teacher Screens
// ============================================================
static void render_teacher_select_screen() {
  display_header("TEACHER MGMT");

  std::vector<Teacher> &teachers = teacher_get_list();
  int count = teachers.size();

  if (count == 0) {
    display_message("No teachers found");
    display_footer("[<] Back");
    return;
  }

  // Build menu items dynamically
  const char *items[10];
  char nameBuf[10][25];
  for (int i = 0; i < count && i < 10; i++) {
    snprintf(nameBuf[i], 25, "%s %s", teachers[i].name.c_str(),
             teachers[i].enrolled ? "[OK]" : "");
    items[i] = nameBuf[i];
  }

  display_menu(items, min(count, 10), teacherSelection);
  display_footer("[OK] Enroll [<] Back");
}

static void render_teacher_enroll_screen() {
  display_header("ENROLL TEACHER");

  std::vector<Teacher> &teachers = teacher_get_list();
  if (teacherSelection >= 0 && teacherSelection < (int)teachers.size()) {
    display_message_multi(teachers[teacherSelection].name.c_str(),
                          "Place finger on", "sensor to enroll");
  }
  display_footer("[<] Cancel");
}

static void render_teacher_login_screen() {
  if (teacher_is_logged_in()) {
    char buf[30];
    String name = teacher_get_current_obj()->name;

    // Shorten name: Remove spaces after dots (e.g. "A. A." -> "A.A.")
    String shortName = "";
    for (int i = 0; i < name.length(); i++) {
      shortName += name[i];
      if (name[i] == '.' && i + 1 < name.length() && name[i + 1] == ' ') {
        i++; // Skip space after dot
      }
    }
    if (shortName.length() > 16)
      shortName = shortName.substring(0, 13) + "...";

    snprintf(buf, 30, "Hi %s", shortName.c_str());
    display_header(buf);

    const char *items[] = {"Take Attendance", "Connect WiFi",
                           "Upload My Sessions", "Logout"};
    display_menu(items, 4, teacherLoginSelection);
    display_footer("[OK] Select");
  } else {
    display_header("TEACHER LOGIN");
    display_message_multi("Place finger to", "login as teacher", NULL);
    display_footer("[<] Back");
  }
}

// ============================================================
// PHASE 6: ADMIN MODE SCREENS
// ============================================================
static void render_admin_auth_screen() {
  display_header("ADMIN MODE");
  display_message_multi("Place ADMIN finger", "to verify identity", NULL);
  display_footer("[<] Cancel");
}

static void render_admin_menu_screen() {
  display_header("ADMIN MENU");

  const char *items[] = {"Enroll Teachers", "WiFi Settings",
                         "SD Card Viewer", "Safe Reset",
                         "Exit Admin Mode"};

  display_menu(items, 5, adminMenuSelection);
  display_footer("[OK] Select [<] Exit");
}

static void render_admin_teacher_list_screen() {
  display_header("SELECT TEACHER");

  std::vector<Teacher> &teachers = teacher_get_list();
  int count = teachers.size();

  if (count == 0) {
    display_message("No teachers in config");
    display_footer("[<] Back");
    return;
  }

  // Build menu with enrollment status
  const char *items[10];
  char nameBuf[10][25];
  for (int i = 0; i < count && i < 10; i++) {
    snprintf(nameBuf[i], 25, "%s %s", teachers[i].name.c_str(),
             teachers[i].enrolled ? "[OK]" : "[--]");
    items[i] = nameBuf[i];
  }

  display_menu(items, min(count, 10), adminTeacherSelection);
  display_footer("[OK] Enroll [<] Back");
}

static void render_admin_teacher_enroll_screen() {
  display_header("ENROLL TEACHER");

  std::vector<Teacher> &teachers = teacher_get_list();
  if (teacherSelection >= 0 && teacherSelection < (int)teachers.size()) {
    display_message_multi(teachers[teacherSelection].name.c_str(),
                          "Place TEACHER finger", "on sensor...");
  }
  display_footer("[<] Cancel");
}

static void render_reset_confirm_screen() {
  display_header("SAFE RESET");
  display_message_multi("Data-only reset", "No firmware erase", "Admin finger");
  display_footer("[<] Cancel");
}

// ============================================================
// PHASE 7: SESSION SCREENS
// ============================================================
static void render_subject_select_screen() {
  display_header("SELECT SUBJECT");

  Teacher *t = teacher_get_current_obj();
  if (!t) {
    display_message("Error: No Teacher");
    return;
  }

  // Parse subjects dynamically
  // Reset count only if needed? Better to parse every time or check if changed.
  // For simplicity, re-parse.
  subjectCount = 0;
  String subs = t->subjects;
  int start = 0;
  int end = subs.indexOf(',');

  while (end != -1 && subjectCount < 10) {
    subjectList[subjectCount] = subs.substring(start, end);
    subjectList[subjectCount].trim();
    subjectCount++;
    start = end + 1;
    end = subs.indexOf(',', start);
  }

  if (subjectCount < 10 && start < (int)subs.length()) {
    subjectList[subjectCount] = subs.substring(start);
    subjectList[subjectCount].trim();
    subjectCount++;
  }

  // Display menu
  if (subjectCount == 0) {
    display_message("No subjects assigned");
    display_footer("[<] Back");
  } else {
    const char *items[10];
    char buf[10][21]; // Local buffer for C-strings
    for (int i = 0; i < subjectCount; i++) {
      subjectList[i].toCharArray(buf[i], 21);
      items[i] = buf[i];
    }
    display_menu(items, subjectCount, subjectSelection);
    display_footer("[OK] Start Class");
  }
}

static void render_session_active_screen() {
  ClassSession *session = session_get_current();
  if (!session || !session->active) {
    display_message("No Active Session");
    return;
  }

  display_header(session->subject.c_str());

  char timeBuf[20];
  snprintf(timeBuf, 20, "Time: %s", session_get_duration_string().c_str());

  char countBuf[20];
  snprintf(countBuf, 20, "Count: %d", session->studentCount);

  display_message_multi(session->teacherName.c_str(), timeBuf, countBuf);

  display_footer("[<] End Session");
}

static void render_session_summary_screen() {
  display_header("SESSION ENDED");

  ClassSession *session = session_get_current();

  char countBuf[20];
  snprintf(countBuf, 20, "Total: %d", session->studentCount);

  display_message_multi("Class Finished", countBuf, "Saved to SD");

  display_footer("[OK] Home");
}

// ============================================================
// ADMIN SD LIST SCREEN (Enhanced)
// ============================================================
static void render_admin_sd_list_screen() {
  if (!sdListLoaded) {
    display_clear();
    display_header("SD Files");
    display_message("Scanning...");
    display_update();

    // Filter for CSV files only
    std::vector<String> allFiles = sd_list_files_recursive("/attendance");
    sdFileList.clear();
    for (const auto &f : allFiles) {
      if (f.endsWith(".csv") || f.endsWith(".CSV")) {
        sdFileList.push_back(f);
      }
    }
    sdListLoaded = true;
  }

  display_clear();

  // Professional Header with Capacity
  char capBuf[30];
  snprintf(capBuf, 30, "SD: %u/%u MB", sd_get_used_mb(), sd_get_size_mb());
  display_header("SD EXPLORER");
  display_message_at(1, capBuf);

  if (sdFileList.empty()) {
    display_message_at(3, "No Sessions Found");
  } else {
    // Reduce items to prevent overlap with header/capacity info
    int pageSize = 3;
    int pageStart = (sdFileIndex / pageSize) * pageSize;
    int pageCount = min((int)sdFileList.size() - pageStart, pageSize);
    int relSelection = sdFileIndex - pageStart;

    const char *items[3];
    char nameBuf[3][25];

    for (int i = 0; i < pageCount; i++) {
      String fullPath = sdFileList[pageStart + i];

      // Extract Teacher and Filename
      // e.g., /attendance/TeacherName/File.csv -> TeacherName/File
      String displayPath = fullPath;
      if (displayPath.startsWith("/attendance/")) {
        displayPath = displayPath.substring(12);
      }
      if (displayPath.endsWith(".csv")) {
        displayPath = displayPath.substring(0, displayPath.length() - 4);
      }

      if (displayPath.length() > 20) {
        displayPath = displayPath.substring(0, 18) + "..";
      }

      displayPath.toCharArray(nameBuf[i], 25);
      items[i] = nameBuf[i];
    }

    // Start menu lower (y=24) to avoid overlapping "SD: 12/16 MB"
    display_menu(items, pageCount, relSelection, 24, 3);

    char countBuf[20];
    snprintf(countBuf, 20, "%d / %d", sdFileIndex + 1, (int)sdFileList.size());
    display_message_at(6, countBuf); // This puts it at footer area basically
  }

  display_footer("[OK] View [<] Back");
  display_update();
}

static void render_admin_sd_detail_screen() {
  display_clear();

  // Extract a clean header from the path
  String header = sdDetailFile;
  int lastSlash = header.lastIndexOf('/');
  if (lastSlash >= 0)
    header = header.substring(lastSlash + 1);
  if (header.endsWith(".csv"))
    header = header.substring(0, header.length() - 4);
  if (header.length() > 16)
    header = header.substring(0, 13) + "..";

  display_header(header.c_str());

  if (sdDetailStudents.empty()) {
    display_message_at(3, "No Records Found");
  } else {
    // Show scrollable IDs
    int pageSize = 3;
    int startIdx = sdDetailScrollPos;
    for (int i = 0;
         i < pageSize && (startIdx + i) < (int)sdDetailStudents.size(); i++) {
      char buf[21];
      snprintf(buf, 21, "Roll: %s", sdDetailStudents[startIdx + i].c_str());
      display_message_at(2 + i, buf);
    }

    char countBuf[20];
    snprintf(countBuf, 20, "Total: %d Present", (int)sdDetailStudents.size());
    display_message_at(6, countBuf);
  }

  display_footer("[OK] Delete [<] Back");
}
