#include "setup_mode.h"
#include "../config/config_manager.h"
#include "../config/remote_config_loader.h"
#include "../core/device_info.h"
#include "../hal/display_hal.h"
#include "../hal/fingerprint_hal.h"
#include "../hal/fp_mapping_hal.h"
#include "../hal/rtc_hal.h"
#include "../hal/sd_hal.h"
#include "../hal/wifi_hal.h"
#include "../security/admin_auth.h"
#include <Preferences.h>


// ============================================================
// STATE
// ============================================================
static Preferences prefs;
static bool setupDone = false;
static SetupStep currentStep = SETUP_MENU;
static int menuSelection = 0;

static int setup_clear_attendance_csv_files() {
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

static void setup_clear_attendify_namespace() {
  prefs.begin("attendify", false);
  prefs.clear();
  prefs.end();
}

static void setup_show_config_status(const char *line1, const char *line2,
                                     const char *line3) {
  display_clear();
  display_update();
  delay(50);

  display_clear();
  display_header("CONFIG");
  display_message_multi(line1, line2, line3);
  display_update();
}

// Setup menu items
#define SETUP_MENU_COUNT 7
static const char *setupMenuItems[SETUP_MENU_COUNT] = {
    "1. WiFi Config", "2. Load Config", "3. Admin Auth",
  "4. Set Time",    "5. Safe Reset",  "6. Full Reset",
  "7. Finish Setup"};

// WiFi state
String wifiNetworks[10]; // Non-static for extern access
int wifiNetworkCount = 0;
int wifiSelection = 0;
bool wifiNetworkOpen[10] = {false};

// ============================================================
// INITIALIZATION
// ============================================================
void setup_mode_init() {
  prefs.begin("attendify", true); // read-only
  setupDone = prefs.getBool("setup_done", false);
  prefs.end();

  currentStep = SETUP_MENU;
  menuSelection = 0;
  wifiNetworkCount = 0;
}

// ============================================================
// GETTERS
// ============================================================
bool setup_is_completed() { return setupDone; }

SetupStep setup_get_step() { return currentStep; }

void setup_set_step(SetupStep step) {
  currentStep = step;
  Serial.print("[SETUP] Step: ");
  Serial.println((int)step);
}

int setup_get_selection() { return menuSelection; }

int setup_get_wifi_count() { return wifiNetworkCount; }

int setup_get_wifi_selection() { return wifiSelection; }

String setup_get_wifi_ssid(int index) {
  if (index >= 0 && index < wifiNetworkCount) {
    return wifiNetworks[index];
  }
  return "";
}

bool setup_get_wifi_is_open(int index) {
  if (index >= 0 && index < wifiNetworkCount) {
    return wifiNetworkOpen[index];
  }
  return false;
}

// ============================================================
// INPUT HANDLING
// ============================================================
void setup_handle_input(int event) {
  if (event == 0)
    return; // BTN_NONE

  switch (currentStep) {
  case SETUP_MENU:
    if (event == 1) { // UP
      menuSelection =
          (menuSelection > 0) ? menuSelection - 1 : SETUP_MENU_COUNT - 1;
    } else if (event == 2) { // DOWN
      menuSelection =
          (menuSelection < SETUP_MENU_COUNT - 1) ? menuSelection + 1 : 0;
    } else if (event == 3) { // SELECT
      switch (menuSelection) {
      case 0:
        currentStep = SETUP_WIFI;
        wifiNetworkCount = 0;
        break;
      case 1:
        currentStep = SETUP_CONFIG;
        break;
      case 2:
        currentStep = SETUP_ADMIN;
        break;
      case 3:
        // Sync RTC via NTP (requires WiFi)
        if (wifi_is_connected()) {
          display_clear();
          display_header("TIME SYNC");
          display_message("Syncing NTP...");
          display_update();
          if (rtc_sync_ntp()) {
            display_clear();
            display_header("TIME SYNC");
            display_message_multi("Time Updated!", rtc_time_string().c_str(), rtc_get_date_string().c_str());
            display_update();
            delay(2000);
          } else {
            display_clear();
            display_header("TIME SYNC");
            display_message("NTP Sync Failed");
            display_update();
            delay(1500);
          }
        } else {
          display_clear();
          display_header("TIME SYNC");
          display_message_multi("WiFi required!", "Connect WiFi first", "(Menu option 1)");
          display_update();
          delay(2000);
        }
        break;
      case 4: {
        // Safe reset: clear student fingerprint data and attendance CSV only
        display_clear();
        display_header("SAFE RESET");
        display_message("Clearing data...");
        display_update();

        fp_map_reset();
        int deletedCsv = setup_clear_attendance_csv_files();

        char line2[24];
        snprintf(line2, sizeof(line2), "CSV deleted: %d", deletedCsv);

        display_clear();
        display_header("SAFE RESET");
        display_message_multi("Student data cleared", line2, "FW unchanged");
        display_update();
        Serial.print("[SETUP] Safe reset complete, CSV deleted: ");
        Serial.println(deletedCsv);
        delay(2000);
        break;
      }
      case 5:
        // Full reset: clear all biometric and local config data
        display_clear();
        display_header("FULL RESET");
        display_message_multi("Clearing all FP...", "Please wait", NULL);
        display_update();

        fingerprint_clear_all();
        fp_map_reset();
        remote_config_clear();
        setup_clear_attendify_namespace();
        setupDone = false;

        {
          int deletedCsv = setup_clear_attendance_csv_files();
          char line2[24];
          snprintf(line2, sizeof(line2), "CSV deleted: %d", deletedCsv);

          display_clear();
          display_header("FULL RESET");
          display_message_multi("All biometric cleared", line2,
                                "Firmware kept");
          display_update();
          Serial.print("[SETUP] Full reset done, CSV deleted: ");
          Serial.println(deletedCsv);
          delay(2200);
        }
        break;
      case 6:
        currentStep = SETUP_COMPLETE;
        break;
      }
    }
    break;

  case SETUP_WIFI:
    if (event == 3) { // SELECT - start scan
      currentStep = SETUP_WIFI_SCANNING;
    } else if (event == 4) { // BACK
      currentStep = SETUP_MENU;
    }
    break;

  case SETUP_WIFI_SCANNING:
    // Scanning happens in loop
    break;

  case SETUP_WIFI_CONNECT:
    // Show networks - navigate with UP/DOWN
    if (event == 1) { // UP
      wifiSelection =
          (wifiSelection > 0) ? wifiSelection - 1 : wifiNetworkCount;
    } else if (event == 2) { // DOWN
      wifiSelection =
          (wifiSelection < wifiNetworkCount) ? wifiSelection + 1 : 0;
    } else if (event == 3) { // SELECT - Connect (placeholder)
      if (wifiSelection == wifiNetworkCount) {
        currentStep = SETUP_WIFI_SCANNING;
        break;
      }
      if (!setup_get_wifi_is_open(wifiSelection)) {
        Serial.println("[SETUP] Secured network selected; password input is not available in setup UI");
        display_clear();
        display_header("WiFi");
        display_message_multi("Secured network", "not supported here", "Use open hotspot");
        display_update();
        delay(1800);
        break;
      }

      display_clear();
      display_header("WiFi");
      display_message_multi(
          "Connecting to:", wifiNetworks[wifiSelection].c_str(), NULL);
      display_update();

      // For now, just show selected - real connection needs password
      bool connected = wifi_connect(wifiNetworks[wifiSelection].c_str(), "");
      delay(2000);

      if (connected && wifi_is_connected()) {
        display_clear();
        display_header("WiFi");
        display_message("Connected!");
        display_update();
        delay(1000);

        // Auto Sync Time
        display_clear();
        display_header("TIME SYNC");
        display_message("Updating NTP...");
        display_update();
        if (rtc_sync_ntp()) {
          display_clear(); // Fix overlap
          display_header("TIME SYNC");
          display_message("Time Updated!");
        } else {
          display_clear(); // Fix overlap
          display_header("TIME SYNC");
          display_message("Sync Failed");
        }
        display_update();
        delay(1500);

        // Auto-discover server on new network
        display_clear();
        display_header("SERVER");
        display_message("Finding server...");
        display_update();
        if (remote_config_auto_discover_server()) {
          display_clear();
          display_header("SERVER");
          display_message_multi("Server Found!", remote_config_get_server_url().c_str(), NULL);
        } else {
          display_clear();
          display_header("SERVER");
          display_message_multi("Server not found", "Start backend on", "same network");
        }
        display_update();
        delay(1500);
      } else {
        display_clear();
        display_header("WiFi");
        display_message_multi("Connect failed", "Check hotspot auth", "Try open hotspot");
        display_update();
        delay(1500);
      }
      currentStep = SETUP_MENU;
    } else if (event == 4) { // BACK
      currentStep = SETUP_WIFI;
    }
    break;

  case SETUP_CONFIG:
    if (event == 3) { // SELECT - load config
      currentStep = SETUP_CONFIG_LOADING;
    } else if (event == 4) { // BACK
      currentStep = SETUP_MENU;
    }
    break;

  case SETUP_ADMIN:
    if (event == 3) { // SELECT - start enrollment
      currentStep = SETUP_ADMIN_ENROLL;
    } else if (event == 4) { // BACK
      currentStep = SETUP_MENU;
    }
    break;

  case SETUP_COMPLETE:
    if (event == 3) { // SELECT - finish
      setup_finish();
    } else if (event == 4) { // BACK
      currentStep = SETUP_MENU;
    }
    break;

  default:
    if (event == 4) { // BACK always returns to menu
      currentStep = SETUP_MENU;
    }
    break;
  }
}

// ============================================================
// LOOP - Handle async operations
// ============================================================
void setup_mode_loop() {
  switch (currentStep) {
  case SETUP_WIFI_SCANNING:
    // Show scanning on display
    display_clear();
    display_header("WiFi");
    display_message("Scanning...");
    display_update();

    // Scan for WiFi networks
    wifiNetworkCount = wifi_scan(wifiNetworks, 10, wifiNetworkOpen);
    wifiSelection = 0;

    if (wifiNetworkCount > 0) {
      Serial.print("[SETUP] Found ");
      Serial.print(wifiNetworkCount);
      Serial.println(" networks");
      currentStep = SETUP_WIFI_CONNECT; // Go to network selection
    } else {
      display_clear();
      display_header("WiFi");
      display_message("No networks found");
      display_update();
      delay(1500);
      currentStep = SETUP_WIFI;
    }
    break;

  case SETUP_CONFIG_LOADING: {
    // First try loading config from SD card
    setup_show_config_status("Checking SD card...", NULL, NULL);
    delay(1000);

    bool sdLoaded = config_load();
    if (sdLoaded) {
      Serial.println("[SETUP] Config loaded from SD card");

      // If SD card has WiFi credentials, add them to the background
      // auto-connect profile list (non-blocking - no screen popup)
      if (config_has_wifi_creds()) {
        wifi_profiles_add(config_get_wifi_ssid(), config_get_wifi_password());
        Serial.printf("[SETUP] WiFi profile added from SD: %s\n",
                      config_get_wifi_ssid().c_str());
      }

      if (config_has_server_url()) {
        String configuredServerUrl = config_get_server_url();
        if (remote_config_set_server_url(configuredServerUrl)) {
          Serial.print("[SETUP] Server URL loaded from SD config: ");
          Serial.println(configuredServerUrl);
        }
      }

      bool downloaded = false;
      if (wifi_is_connected()) {
        setup_show_config_status("SD bootstrap OK", "Downloading backend...",
                                 NULL);
        downloaded = remote_config_download();
      }

      if (downloaded) {
        DeviceConfig *cfg = remote_config_get();
        extern void teacher_load_from_config();
        teacher_load_from_config();

        char buf[30];
        snprintf(buf, 30, "Teachers: %d", cfg->teacherCount);
        display_clear();
        display_header("CONFIG OK");
        display_message_multi("Backend loaded!", buf, device_get_id().c_str());
        display_update();
        delay(2000);
      } else if (config_apply_to_remote_config(device_get_id())) {
        extern void teacher_load_from_config();
        teacher_load_from_config();

        char buf[30];
        snprintf(buf, 30, "Teachers: %d", config_get_teacher_count());
        display_clear();
        display_header("CONFIG OK");
        display_message_multi("SD fallback loaded", buf, device_get_id().c_str());
        display_update();
        delay(2000);
      } else {
        display_clear();
        display_header("CONFIG OK");
        display_message_multi("Server URL saved", "Connect WiFi then",
                              "sync backend");
        display_update();
        delay(2000);
      }
    } else {
      // Flash memory URL fallback
      setup_show_config_status("SD load failed", "Trying server...",
                               device_get_id().c_str());

      if (remote_config_download()) {
        Serial.println("[SETUP] Config downloaded successfully");
        DeviceConfig *cfg = remote_config_get();
        extern void teacher_load_from_config();
        teacher_load_from_config();

        char buf[30];
        snprintf(buf, 30, "Teachers: %d", cfg->teacherCount);
        display_clear();
        display_header("CONFIG OK");
        display_message_multi("Download complete!", buf,
                              device_get_id().c_str());
        display_update();
        delay(2000);
      } else {
        Serial.println("[SETUP] Config download failed");
        display_clear();
        display_header("CONFIG");
        display_message_multi("Download FAILED!", "Put config.json on SD",
                              "or check server.");
        display_update();
        delay(2500);
      }
    }
    currentStep = SETUP_MENU;
    break;
  }

  case SETUP_ADMIN_ENROLL:
    // Enroll admin fingerprint (ID 1 reserved for admin)
    display_clear();
    display_header("ADMIN ENROLL");
    display_message_multi("Place your finger", "on the sensor", "[<] Cancel");
    display_update();

    if (!fingerprint_check()) {
      Serial.println("[SETUP] Fingerprint sensor not responding");
      display_clear();
      display_header("FP ERROR");
      display_message_multi("Sensor not found", "Check wiring", "Restart device");
      display_update();
      delay(2000);
      currentStep = SETUP_MENU;
      break;
    }

    // Allow re-enrollment after reset by clearing existing admin slot first
    fingerprint_delete(1);
    delay(120);

    if (fingerprint_enroll(1)) {
      Serial.println("[SETUP] Admin fingerprint enrolled");
      admin_authorize("Admin");
      display_clear();
      display_header("SUCCESS");
      display_message("Admin Enrolled!");
      display_update();
      delay(2000);
    } else {
      Serial.println("[SETUP] Admin enrollment failed or cancelled");
      const char *err = fingerprint_get_last_enroll_error_text();
      display_clear();
      display_header("ENROLL FAILED");
      display_message_multi(err, "Try again", "Use same finger");
      display_update();
      delay(2000);
    }
    currentStep = SETUP_MENU;
    break;

  default:
    break;
  }
}

// ============================================================
// FINISH SETUP
// ============================================================
void setup_finish() {
  prefs.begin("attendify", false); // read-write
  prefs.putBool("setup_done", true);
  prefs.end();

  setupDone = true;
  Serial.println("[SETUP] Setup completed and locked!");
}

// ============================================================
// RESET SETUP (Force re-entry)
// ============================================================
void setup_reset() {
  prefs.begin("attendify", false); // read-write
  prefs.putBool("setup_done", false);
  prefs.end();

  setupDone = false;
  currentStep = SETUP_MENU;
  menuSelection = 0;
  Serial.println("[SETUP] Setup reset - will re-enter setup mode");
}
