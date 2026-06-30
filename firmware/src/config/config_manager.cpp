#include "config_manager.h"
#include "../hal/sd_hal.h"
#include <ArduinoJson.h>

// ============================================================
// STATE
// ============================================================
static bool loaded = false;
static String deviceId = "";
static std::vector<String> adminNames;
static String serverUrl = "";
static std::vector<TeacherConfig> sdTeachers;
static String wifiSsid = "";
static String wifiPassword = "";

static String normalize_server_url_from_config(const String &input) {
    String value = input;
    value.trim();
    if (value.length() == 0) {
        return "";
    }

    if (!value.startsWith("http://") && !value.startsWith("https://")) {
        if (value.indexOf(':') == -1) {
            value += ":3003";
        }
        value = "http://" + value;
    }

    while (value.endsWith("/")) {
        value.remove(value.length() - 1);
    }
    return value;
}

// ============================================================
// LOAD CONFIG FROM SD
// ============================================================
bool config_load() {
    if (!sd_is_ready()) {
        Serial.println("[CONFIG] SD card not ready");
        return false;
    }
    
    // Read config file
    String content = sd_read_file("/config/config.json");
    if (content.length() == 0) {
        Serial.println("[CONFIG] config.json not found or empty");
        return false;
    }
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    
    if (error) {
        Serial.print("[CONFIG] JSON parse error: ");
        Serial.println(error.c_str());
        return false;
    }
    
    // Extract device_id
    if (doc["device_id"].is<String>()) {
        deviceId = doc["device_id"].as<String>();
        Serial.print("[CONFIG] Device ID: ");
        Serial.println(deviceId);
    }
    
    // Extract admins array
    adminNames.clear();
    if (doc["admins"].is<JsonArray>()) {
        JsonArray admins = doc["admins"].as<JsonArray>();
        for (JsonVariant admin : admins) {
            String name = admin.as<String>();
            adminNames.push_back(name);
            Serial.print("[CONFIG] Admin: ");
            Serial.println(name);
        }
    }

    // Extract optional server URL/IP
    serverUrl = "";
    if (doc["server_url"].is<String>()) {
        serverUrl = normalize_server_url_from_config(doc["server_url"].as<String>());
    } else if (doc["server_ip"].is<String>()) {
        serverUrl = normalize_server_url_from_config(doc["server_ip"].as<String>());
    }

    if (serverUrl.length() > 0) {
        Serial.print("[CONFIG] Server URL: ");
        Serial.println(serverUrl);
    }

    // Extract optional wifi credentials
    wifiSsid = "";
    wifiPassword = "";
    if (doc["wifi_ssid"].is<String>()) {
        wifiSsid = doc["wifi_ssid"].as<String>();
    }
    if (doc["wifi_password"].is<String>()) {
        wifiPassword = doc["wifi_password"].as<String>();
    }

    if (wifiSsid.length() > 0) {
        Serial.print("[CONFIG] WiFi SSID from SD: ");
        Serial.println(wifiSsid);
    }

    // Extract teachers array (full offline config)
    sdTeachers.clear();
    if (doc["teachers"].is<JsonArray>()) {
        JsonArray teachers = doc["teachers"].as<JsonArray>();
        for (JsonVariant t : teachers) {
            if (!t["name"].is<String>()) continue;

            TeacherConfig tc;
            tc.name = t["name"].as<String>();

            // Subjects: accept string or array
            if (t["subjects"].is<JsonArray>()) {
                String subStr = "";
                JsonArray subs = t["subjects"].as<JsonArray>();
                for (JsonVariant s : subs) {
                    if (subStr.length() > 0) subStr += ",";
                    subStr += s.as<String>();
                }
                tc.subjects = subStr.length() > 0 ? subStr : "All";
            } else if (t["subjects"].is<String>()) {
                tc.subjects = t["subjects"].as<String>();
                if (tc.subjects.length() == 0) tc.subjects = "All";
            } else {
                tc.subjects = "All";
            }

            tc.isGfm = t["is_gfm"] | false;

            sdTeachers.push_back(tc);
            Serial.print("[CONFIG] Teacher: ");
            Serial.print(tc.name);
            Serial.print(" | Subjects: ");
            Serial.print(tc.subjects);
            if (tc.isGfm) Serial.print(" [GFM]");
            Serial.println();
        }
    }
    
    loaded = true;
    Serial.println("[CONFIG] Configuration loaded successfully");
    return true;
}

// ============================================================
// GETTERS
// ============================================================
bool config_is_loaded() {
    return loaded;
}

String config_get_device_id() {
    return deviceId;
}

std::vector<String> config_get_admin_names() {
    return adminNames;
}

bool config_has_server_url() {
    return serverUrl.length() > 0;
}

String config_get_server_url() {
    return serverUrl;
}

bool config_has_teachers() {
    return sdTeachers.size() > 0;
}

int config_get_teacher_count() {
    return (int)sdTeachers.size();
}

TeacherConfig config_get_teacher(int index) {
    if (index >= 0 && index < (int)sdTeachers.size()) {
        return sdTeachers[index];
    }
    TeacherConfig empty;
    empty.name = "";
    empty.subjects = "";
    empty.isGfm = false;
    return empty;
}

bool config_apply_to_remote_config(const String &fallbackDeviceId) {
    if (!loaded || !config_has_teachers()) {
        return false;
    }

    String effectiveDeviceId = deviceId;
    effectiveDeviceId.trim();
    if (effectiveDeviceId.length() == 0) {
        effectiveDeviceId = fallbackDeviceId;
    }
    if (effectiveDeviceId.length() == 0) {
        return false;
    }

    String adminsArr[5];
    int adminCount = (int)adminNames.size();
    if (adminCount > 5) adminCount = 5;
    for (int i = 0; i < adminCount; i++) {
        adminsArr[i] = adminNames[i];
    }

    TeacherConfig teacherArr[20];
    int teacherCount = (int)sdTeachers.size();
    if (teacherCount > 20) teacherCount = 20;
    for (int i = 0; i < teacherCount; i++) {
        teacherArr[i] = sdTeachers[i];
    }

    return remote_config_load_from_sd_data(effectiveDeviceId, adminsArr,
                                           adminCount, teacherArr,
                                           teacherCount);
}

bool config_has_wifi_creds() {
    return wifiSsid.length() > 0;
}

String config_get_wifi_ssid() {
    return wifiSsid;
}

String config_get_wifi_password() {
    return wifiPassword;
}

