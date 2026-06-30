/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 3
 *  Remote Config Loader - Downloads config from backend
 * ============================================================
 */

#ifndef REMOTE_CONFIG_LOADER_H
#define REMOTE_CONFIG_LOADER_H

#include <Arduino.h>

// ============================================================
// CONFIGURATION
// ============================================================
// Backend server URL (change before deployment)
#define CONFIG_SERVER_URL "https://attendify-backend-apha.onrender.com"
#define CONFIG_PUBLIC_FALLBACK_URL "https://attendify-backend-apha.onrender.com"
#define CONFIG_API_ENDPOINT "/api/device/config"

// ============================================================
// CONFIG STRUCTURE
// ============================================================
struct TeacherConfig {
  String name;
  String subjects; // Comma separated
  bool isGfm;      // GFM Role Flag
};

struct DeviceConfig {
  String deviceId;
  String admins[5];
  int adminCount;
  TeacherConfig teachers[20];
  int teacherCount;
  int version;
  bool loaded;
};

// ============================================================
// FUNCTIONS
// ============================================================

/**
 * Initialize remote config loader
 */
void remote_config_init();

/**
 * Download config from backend server
 * @param serverIp IP address of server (optional, uses default if empty)
 * @return true if download successful
 */
bool remote_config_download(const String &serverIp = "");

/**
 * Check if config is loaded
 * @return true if config exists in NVS
 */
bool remote_config_is_loaded();

/**
 * Get loaded config
 * @return Pointer to config structure
 */
DeviceConfig *remote_config_get();

/**
 * Clear stored config
 * @return true if cleared
 */
bool remote_config_clear();

/**
 * Validate config matches device ID
 * @return true if valid
 */
bool remote_config_validate();

/**
 * Poll server for enrollment queue status
 * @param roll Output roll number
 * @param name Output student name
 * @return true if enrollment is authorized
 */
bool remote_config_poll_queue(int &roll, String &name);

/**
 * True when the most recent queue poll reached the backend successfully.
 */
bool remote_config_last_poll_reachable();

/**
 * HTTP status/error code from the most recent queue poll.
 */
int remote_config_last_poll_code();

/**
 * Get active backend base URL (runtime configurable)
 */
String remote_config_get_server_url();

/**
 * Set backend base URL at runtime and persist to NVS
 * Example: http://192.168.0.167:3003
 */
bool remote_config_set_server_url(const String &url);

/**
 * Auto-discover backend server URL on local network and persist it
 */
bool remote_config_auto_discover_server();

/**
 * Build full URL from active backend base URL + path
 * Example path: /api/poll-status
 */
String remote_config_build_url(const String &path);

/**
 * Load config into NVS from SD card parsed data (offline fallback).
 * @param deviceId Device ID string
 * @param admins Array of admin email strings
 * @param adminCount Number of admins
 * @param teachers Array of TeacherConfig structs
 * @param teacherCount Number of teachers
 * @return true if saved successfully
 */
bool remote_config_load_from_sd_data(const String &deviceId,
                                     const String admins[], int adminCount,
                                     const TeacherConfig teachers[], int teacherCount);

#endif // REMOTE_CONFIG_LOADER_H
