#pragma once
#include <vector>
#include <Arduino.h>
#include "remote_config_loader.h"

bool config_load();
bool config_is_loaded();
String config_get_device_id();
std::vector<String> config_get_admin_names();
bool config_has_server_url();
String config_get_server_url();

/**
 * Check if SD config included teachers array
 */
bool config_has_teachers();

/**
 * Get count of teachers parsed from SD config
 */
int config_get_teacher_count();

/**
 * Get teacher at index (parsed from SD config)
 */
TeacherConfig config_get_teacher(int index);

/**
 * Copy teacher/admin data parsed from SD config into runtime/NVS config.
 * Intended only as an offline fallback when backend download is unavailable.
 */
bool config_apply_to_remote_config(const String &fallbackDeviceId);

/**
 * WiFi configuration utilities loaded from SD
 */
bool config_has_wifi_creds();
String config_get_wifi_ssid();
String config_get_wifi_password();

