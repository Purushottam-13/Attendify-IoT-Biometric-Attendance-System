/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 3
 *  Remote Config Loader - Implementation
 * ============================================================
 */

#include "remote_config_loader.h"
#include "../core/device_info.h"
#include "../core/session_manager.h"
#include "../hal/wifi_hal.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ============================================================
// STATE
// ============================================================
static DeviceConfig config;
static Preferences prefs;
static String serverUrl = CONFIG_SERVER_URL;
static bool lastPollReachable = true;
static int lastPollCode = 0;
static const uint16_t DISCOVERY_PORT = 4210;
static const unsigned long DISCOVERY_TIMEOUT_MS = 4000;
static const uint16_t HTTP_PROBE_CONNECT_TIMEOUT_MS = 800;
static const uint16_t HTTP_PROBE_RESPONSE_TIMEOUT_MS = 1200;
static const unsigned long HTTP_SCAN_BUDGET_MS = 20000;
static const unsigned long HTTP_SCAN_FAILURE_COOLDOWN_MS = 15000;
static const int HTTP_SCAN_MAX_PROBES = 35;
static unsigned long lastHttpScanFailedAt = 0;
static IPAddress lastHttpScanFailedIp(0, 0, 0, 0);

static bool is_same_subnet_24(const IPAddress &a, const IPAddress &b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static bool extract_ipv4_from_url(const String &url, IPAddress &ip) {
  String host = url;

  int schemeIndex = host.indexOf("://");
  if (schemeIndex >= 0) {
    host = host.substring(schemeIndex + 3);
  }

  int slashIndex = host.indexOf('/');
  if (slashIndex >= 0) {
    host = host.substring(0, slashIndex);
  }

  int colonIndex = host.indexOf(':');
  if (colonIndex >= 0) {
    host = host.substring(0, colonIndex);
  }

  return ip.fromString(host.c_str());
}

static bool server_url_matches_current_subnet(const String &url) {
  if (!wifi_is_connected()) {
    return false;
  }

  IPAddress local = WiFi.localIP();
  IPAddress remote;
  if (!extract_ipv4_from_url(url, remote)) {
    // Hostname/public URLs should still be attempted normally.
    return true;
  }

  return is_same_subnet_24(local, remote);
}

static bool probe_server_info_endpoint(const String &baseUrl) {
  HTTPClient http;
  String testUrl = baseUrl + "/api/server-info";
  wifi_http_begin(http, testUrl);
  http.setConnectTimeout(HTTP_PROBE_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_PROBE_RESPONSE_TIMEOUT_MS);

  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return false;
  }

  bool ok = doc["success"] | false;
  String url = doc["url"] | "";
  if (!ok || url.length() == 0) {
    return false;
  }

  return remote_config_set_server_url(url);
}

static bool discover_server_via_http_scan() {
  if (!wifi_is_connected()) {
    return false;
  }

  IPAddress local = WiFi.localIP();
  if (local[0] == 0) {
    return false;
  }

  if (lastHttpScanFailedIp == local &&
      (millis() - lastHttpScanFailedAt) < HTTP_SCAN_FAILURE_COOLDOWN_MS) {
    Serial.println("[DISCOVERY] Skipping HTTP scan; recent failure on this network");
    return false;
  }

  IPAddress gateway = WiFi.gatewayIP();
  uint8_t baseA = local[0];
  uint8_t baseB = local[1];
  uint8_t baseC = local[2];
  uint8_t host = local[3];

  Serial.println("[DISCOVERY] UDP failed; starting HTTP subnet scan...");

  // Priority 1: gateway, common host positions, and neighbors around current IP
  int priorityHosts[40];
  int pCount = 0;
  auto addPriorityHost = [&](int h) {
    if (h < 1 || h > 254) {
      return;
    }
    for (int i = 0; i < pCount; i++) {
      if (priorityHosts[i] == h) {
        return;
      }
    }
    if (pCount < (int)(sizeof(priorityHosts) / sizeof(priorityHosts[0]))) {
      priorityHosts[pCount++] = h;
    }
  };

  IPAddress configuredServerIp;
  if (extract_ipv4_from_url(serverUrl, configuredServerIp)) {
    if (is_same_subnet_24(local, configuredServerIp)) {
      addPriorityHost(configuredServerIp[3]);
    } else {
      // Preserve the saved host number as a cheap hint when only the subnet
      // changed, but do not spend minutes scanning for it.
      addPriorityHost(configuredServerIp[3]);
    }
  }

  if (gateway[0] == baseA && gateway[1] == baseB && gateway[2] == baseC) {
    addPriorityHost(gateway[3]);
  }

  // Hotspots commonly assign nearby IPs to connected devices, so try close
  // neighbors first before probing distant/common addresses.
  for (int distance = 1; distance <= 8; distance++) {
    addPriorityHost((int)host - distance);
    addPriorityHost((int)host + distance);
  }

  int commonHosts[] = {1, 2, 10, 20, 30, 40, 50, 100,
                       101, 110, 120, 150, 167, 200};
  for (unsigned int i = 0; i < sizeof(commonHosts) / sizeof(commonHosts[0]); i++) {
    addPriorityHost(commonHosts[i]);
  }

  unsigned long scanStart = millis();
  int probesAttempted = 0;
  bool scanBudgetExhausted = false;

  auto tryHost = [&](int h) -> bool {
    if (h < 1 || h > 254 || h == host) {
      return false;
    }
    if ((millis() - scanStart) >= HTTP_SCAN_BUDGET_MS ||
        probesAttempted >= HTTP_SCAN_MAX_PROBES) {
      scanBudgetExhausted = true;
      return false;
    }

    probesAttempted++;
    String baseUrl = "http://" + String(baseA) + "." + String(baseB) + "." +
                     String(baseC) + "." + String(h) + ":3003";
    if (probe_server_info_endpoint(baseUrl)) {
      Serial.print("[DISCOVERY] HTTP scan discovered server at: ");
      Serial.println(baseUrl);
      return true;
    }
    delay(1);
    return false;
  };

  bool tried[255] = {false};
  for (int i = 0; i < pCount; i++) {
    int h = priorityHosts[i];
    if (h >= 1 && h <= 254 && !tried[h]) {
      tried[h] = true;
      if (tryHost(h)) {
        return true;
      }
      if (scanBudgetExhausted) {
        break;
      }
    }
  }

  // Priority 2: full /24 scan fallback
  for (int h = 1; h <= 254; h++) {
    if (scanBudgetExhausted) {
      break;
    }
    if (h == host || tried[h]) {
      continue;
    }
    if (tryHost(h)) {
      return true;
    }
  }

  lastHttpScanFailedAt = millis();
  lastHttpScanFailedIp = local;

  Serial.print("[DISCOVERY] HTTP subnet scan stopped after ");
  Serial.print(probesAttempted);
  Serial.println(" probes; backend not found");
  return false;
}

static String normalize_server_url(const String &input) {
  String value = input;
  value.trim();
  if (value.length() == 0) {
    return String(CONFIG_SERVER_URL);
  }
  if (!value.startsWith("http://") && !value.startsWith("https://")) {
    value = "http://" + value;
  }
  while (value.endsWith("/")) {
    value.remove(value.length() - 1);
  }
  return value;
}

static String normalize_optional_server_url(const String &input) {
  String value = input;
  value.trim();
  if (value.length() == 0) {
    return "";
  }
  if (!value.startsWith("http://") && !value.startsWith("https://")) {
    value = "http://" + value;
  }
  while (value.endsWith("/")) {
    value.remove(value.length() - 1);
  }
  return value;
}

// ============================================================
// INITIALIZATION
// ============================================================
void remote_config_init() {
  config.loaded = false;
  config.adminCount = 0;
  config.teacherCount = 0;
  config.version = 0;

  // Try to load from NVS
  prefs.begin("config", true); // read-only
  serverUrl = normalize_server_url(prefs.getString("server_url", CONFIG_SERVER_URL));

  if (prefs.getBool("has_config", false)) {
    config.deviceId = prefs.getString("device_id", "");
    config.version = prefs.getInt("version", 0);
    config.adminCount = prefs.getInt("admin_count", 0);
    config.teacherCount = prefs.getInt("teacher_count", 0);

    for (int i = 0; i < config.adminCount && i < 5; i++) {
      String key = "admin_" + String(i);
      config.admins[i] = prefs.getString(key.c_str(), "");
    }

    for (int i = 0; i < config.teacherCount && i < 20; i++) {
      String key = "teacher_" + String(i);
      config.teachers[i].name = prefs.getString(key.c_str(), "");
      config.teachers[i].subjects =
          prefs.getString((key + "_sub").c_str(), "All");
      config.teachers[i].isGfm = prefs.getBool((key + "_gfm").c_str(), false);
    }

    config.loaded = true;
    Serial.println("[CONFIG] Loaded from NVS");
    Serial.print("[CONFIG] Device ID: ");
    Serial.println(config.deviceId);
    Serial.print("[CONFIG] Admins: ");
    Serial.println(config.adminCount);
    Serial.print("[CONFIG] Teachers: ");
    Serial.println(config.teacherCount);
  } else {
    Serial.println("[CONFIG] No stored config found, loading hardcoded fallback");
    prefs.end();
    String admins[] = {"admin@pes.edu"};
    TeacherConfig teachers[6];
    teachers[0].name = "Mrs. A. A. Kasangottuwar";
    teachers[0].subjects = "ELEC-6, DM";
    teachers[0].isGfm = true;

    teachers[1].name = "Mrs. S. G. Watve";
    teachers[1].subjects = "FOC";
    teachers[1].isGfm = true;

    teachers[2].name = "Dr. S. D. Borde";
    teachers[2].subjects = "ELEC-5, FOC";
    teachers[2].isGfm = true;

    teachers[3].name = "Mr. C. V. Patil";
    teachers[3].subjects = "FOC";
    teachers[3].isGfm = false;

    teachers[4].name = "Dr. R. S. Kamathe";
    teachers[4].subjects = "I&E";
    teachers[4].isGfm = false;

    teachers[5].name = "Ms. M. M. Deshpande";
    teachers[5].subjects = "DBM";
    teachers[5].isGfm = false;

    remote_config_load_from_sd_data("ESP32_CLASSROOM_02", admins, 1, teachers, 6);
    Serial.print("[CONFIG] Server URL: ");
    Serial.println(serverUrl);
    return;
  }

  prefs.end();

  Serial.print("[CONFIG] Server URL: ");
  Serial.println(serverUrl);
}

// ============================================================
// DOWNLOAD CONFIG
// ============================================================
bool remote_config_download(const String &serverIp) {
  if (!wifi_is_connected()) {
    Serial.println("[CONFIG] WiFi not connected");
    return false;
  }

  bool allowDiscoveryFallback = serverIp.length() == 0;
  bool publicFallbackTried = false;
  String publicFallbackUrl =
      normalize_optional_server_url(String(CONFIG_PUBLIC_FALLBACK_URL));

  for (int attempt = 0; attempt < 2; attempt++) {
    String baseUrl;
    if (serverIp.length() > 0) {
      baseUrl = normalize_server_url("http://" + serverIp + ":3003");
      remote_config_set_server_url(baseUrl);
    } else {
      baseUrl = serverUrl;
    }

    if (serverIp.length() == 0 && attempt == 0 &&
        !server_url_matches_current_subnet(baseUrl)) {
      Serial.print("[CONFIG] Skipping saved server URL on different subnet: ");
      Serial.println(baseUrl);

      if (allowDiscoveryFallback) {
        if (remote_config_auto_discover_server()) {
          Serial.println(
              "[CONFIG] Discovery fallback succeeded. Retrying download...");
          continue;
        }

        if (!publicFallbackTried && publicFallbackUrl.length() > 0 &&
            publicFallbackUrl != serverUrl) {
          publicFallbackTried = true;
          if (remote_config_set_server_url(publicFallbackUrl)) {
            Serial.println(
                "[CONFIG] Public fallback URL set. Retrying download...");
            continue;
          }
        }
      }

      return false;
    }

    String url = baseUrl + CONFIG_API_ENDPOINT + "?deviceId=" + device_get_id();

    Serial.print("[CONFIG] Downloading from: ");
    Serial.println(url);

    HTTPClient http;
    wifi_http_begin(http, url);
    http.setConnectTimeout(2000);
    http.setTimeout(10000);

    int httpCode = http.GET();
    if (httpCode != 200) {
      Serial.print("[CONFIG] HTTP error: ");
      Serial.println(httpCode);
      http.end();

      if (attempt == 0 && allowDiscoveryFallback) {
        if (remote_config_auto_discover_server()) {
          Serial.println(
              "[CONFIG] Discovery fallback succeeded. Retrying download...");
          continue;
        }

        if (!publicFallbackTried && publicFallbackUrl.length() > 0 &&
            publicFallbackUrl != serverUrl) {
          publicFallbackTried = true;
          if (remote_config_set_server_url(publicFallbackUrl)) {
            Serial.println(
                "[CONFIG] Public fallback URL set. Retrying download...");
            continue;
          }
        }
      }
      return false;
    }

    String payload = http.getString();
    http.end();

    Serial.println("[CONFIG] Response received");

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("[CONFIG] JSON parse error: ");
      Serial.println(error.c_str());
      return false;
    }

    // Check if deviceId matches
    String receivedDeviceId = doc["deviceId"].as<String>();
    if (receivedDeviceId != device_get_id()) {
      Serial.println("[CONFIG] Device ID mismatch!");
      Serial.print("[CONFIG] Expected: ");
      Serial.println(device_get_id());
      Serial.print("[CONFIG] Received: ");
      Serial.println(receivedDeviceId);
      return false;
    }

    // Parse config
    config.deviceId = receivedDeviceId;
    config.version = doc["version"] | 1;

    // Parse admins
    config.adminCount = 0;
    if (doc["admins"].is<JsonArray>()) {
      JsonArray admins = doc["admins"].as<JsonArray>();
      for (JsonVariant admin : admins) {
        if (config.adminCount < 5) {
          config.admins[config.adminCount++] = admin.as<String>();
        }
      }
    }

    // Parse teachers
    config.teacherCount = 0;
    if (doc["teachers"].is<JsonArray>()) {
      JsonArray teachers = doc["teachers"].as<JsonArray>();
      for (JsonVariant teacher : teachers) {
        if (config.teacherCount < 20) {
          // Teacher can be string or object with name
          if (teacher.is<String>()) {
            config.teachers[config.teacherCount].name = teacher.as<String>();
            config.teachers[config.teacherCount].subjects = "All";
            config.teachers[config.teacherCount].isGfm = false;
            config.teacherCount++;
          } else if (teacher["name"].is<String>()) {
            config.teachers[config.teacherCount].name =
                teacher["name"].as<String>();

            // Parse subjects array to comma-separated string
            String subjectsStr = "";
            if (teacher["subjects"].is<JsonArray>()) {
              JsonArray subs = teacher["subjects"].as<JsonArray>();
              for (JsonVariant sub : subs) {
                if (subjectsStr.length() > 0)
                  subjectsStr += ",";
                subjectsStr += sub.as<String>();
              }
            } else if (teacher["subjects"].is<String>()) {
              subjectsStr = teacher["subjects"].as<String>();
            }
            if (subjectsStr.length() == 0)
              subjectsStr = "All";

            config.teachers[config.teacherCount].subjects = subjectsStr;
            config.teachers[config.teacherCount].isGfm =
                teacher["is_gfm"] | false;
            config.teacherCount++;
          }
        }
      }
    }

    config.loaded = true;

    // Save to NVS
    prefs.begin("config", false); // read-write
    prefs.putBool("has_config", true);
    prefs.putString("device_id", config.deviceId);
    prefs.putInt("version", config.version);
    prefs.putInt("admin_count", config.adminCount);
    prefs.putInt("teacher_count", config.teacherCount);

    for (int i = 0; i < config.adminCount; i++) {
      String key = "admin_" + String(i);
      prefs.putString(key.c_str(), config.admins[i]);
    }

    for (int i = 0; i < config.teacherCount; i++) {
      String key = "teacher_" + String(i);
      prefs.putString(key.c_str(), config.teachers[i].name);
      prefs.putString((key + "_sub").c_str(), config.teachers[i].subjects);
      prefs.putBool((key + "_gfm").c_str(), config.teachers[i].isGfm);

      // Debug logging
      Serial.print("[CONFIG] Saved Teacher ");
      Serial.print(i);
      Serial.print(": ");
      Serial.print(config.teachers[i].name);
      Serial.print(" | Subjects: ");
      Serial.print(config.teachers[i].subjects);
      if (config.teachers[i].isGfm) {
        Serial.print(" [GFM]");
      }
      Serial.println();
    }

    prefs.end();

    Serial.println("[CONFIG] Config saved to NVS");
    Serial.print("[CONFIG] Admins: ");
    Serial.println(config.adminCount);
    Serial.print("[CONFIG] Teachers: ");
    Serial.println(config.teacherCount);

    // Parse and save Wi-Fi profiles for background auto-connect
    if (doc["wifiNetworks"].is<JsonArray>()) {
      String wifiJson;
      serializeJson(doc["wifiNetworks"], wifiJson);
      wifi_profiles_save_from_json(wifiJson);
      Serial.printf("[CONFIG] Synced %d Wi-Fi profiles from server\n",
                    doc["wifiNetworks"].as<JsonArray>().size());
    }

    return true;
  }
  return false;
}

// ============================================================
// GETTERS
// ============================================================
bool remote_config_is_loaded() { return config.loaded; }

DeviceConfig *remote_config_get() { return &config; }

// ============================================================
// VALIDATION
// ============================================================
bool remote_config_validate() {
  if (!config.loaded) {
    return false;
  }

  // Check device ID matches
  if (config.deviceId != device_get_id()) {
    Serial.println("[CONFIG] Stored config is for different device!");
    return false;
  }

  return true;
}

// ============================================================
// CLEAR
// ============================================================
bool remote_config_clear() {
  prefs.begin("config", false);
  prefs.clear();
  prefs.end();

  config.loaded = false;
  config.adminCount = 0;
  config.teacherCount = 0;
  serverUrl = String(CONFIG_SERVER_URL);

  Serial.println("[CONFIG] Config cleared");
  return true;
}

// ============================================================
// POLL QUEUE
// ============================================================
bool remote_config_poll_queue(int &roll, String &name) {
  lastPollReachable = false;
  lastPollCode = 0;

  if (!wifi_is_connected())
    return false;

  // Use shorter timeout to avoid blocking UI
  HTTPClient http;
  String url = serverUrl + "/api/poll-status";

  wifi_http_begin(http, url);
  http.addHeader("x-device-id", device_get_id());
  http.addHeader("x-device-secret", DEVICE_SECRET);
  http.setConnectTimeout(1500);
  http.setTimeout(2500); // Allow sufficient time for cloud/database lookups

  int code = http.GET();
  lastPollCode = code;
  if (code == 200) {
    lastPollReachable = true;
    String payload = http.getString();
    // Parse JSON: { action: "ENROLL", roll: 123, name: "John" }
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      String action = doc["action"];
      if (action == "ENROLL") {
        roll = doc["roll"];
        name = doc["name"].as<String>();
        http.end();
        return true;
      } else if (action == "LIST_FILES") {
        Serial.println("[REMOTE] List Files command received");
        // We must end HTTP to free resources before starting upload
        http.end();
        // Trigger file list upload
        session_list_files_and_upload();
        return false;
      }
    }
  }
  http.end();
  return false;
}

bool remote_config_last_poll_reachable() { return lastPollReachable; }

int remote_config_last_poll_code() { return lastPollCode; }

String remote_config_get_server_url() { return serverUrl; }

bool remote_config_set_server_url(const String &url) {
  String normalized = normalize_server_url(url);
  if (normalized.length() == 0) {
    return false;
  }

  serverUrl = normalized;
  prefs.begin("config", false);
  prefs.putString("server_url", serverUrl);
  prefs.end();

  Serial.print("[CONFIG] Server URL updated: ");
  Serial.println(serverUrl);
  return true;
}

bool remote_config_is_public_url(const String &url) {
  // If URL starts with https:// it's public/secure.
  if (url.startsWith("https://")) {
    return true;
  }
  
  // If it doesn't contain a local IP address prefix, and contains a domain suffix
  if (url.indexOf("192.168.") == -1 && 
      url.indexOf("10.") == -1 && 
      url.indexOf("172.") == -1 && 
      url.indexOf("127.0.0.1") == -1 && 
      url.indexOf("localhost") == -1) {
    if (url.indexOf('.') != -1) {
      return true;
    }
  }
  return false;
}

bool remote_config_auto_discover_server() {
  if (remote_config_is_public_url(serverUrl)) {
    Serial.println("[DISCOVERY] Server URL is a public domain; skipping local auto-discovery.");
    return true;
  }

  if (!wifi_is_connected()) {
    Serial.println("[DISCOVERY] WiFi not connected");
    return false;
  }

  WiFiUDP udp;
  if (!udp.begin(DISCOVERY_PORT)) {
    Serial.println("[DISCOVERY] UDP begin failed");
    return false;
  }

  const char *probe = "ATTENDIFY_DISCOVER";
  IPAddress broadcastIp = WiFi.broadcastIP();
  udp.beginPacket(broadcastIp, DISCOVERY_PORT);
  udp.write((const uint8_t *)probe, strlen(probe));
  udp.endPacket();

  Serial.print("[DISCOVERY] Probe sent to ");
  Serial.print(broadcastIp);
  Serial.print(":");
  Serial.println(DISCOVERY_PORT);

  unsigned long start = millis();
  while (millis() - start < DISCOVERY_TIMEOUT_MS) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buf[256];
      int len = udp.read(buf, sizeof(buf) - 1);
      if (len > 0) {
        buf[len] = '\0';
      } else {
        continue;
      }

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, buf);
      if (error) {
        continue;
      }

      String responseType = doc["type"] | "";
      if (responseType != "ATTENDIFY_DISCOVERY_RESPONSE") {
        continue;
      }

      String discoveredUrl = doc["url"] | "";
      if (discoveredUrl.length() == 0) {
        continue;
      }

      bool updated = remote_config_set_server_url(discoveredUrl);
      udp.stop();
      if (updated) {
        Serial.print("[DISCOVERY] Server discovered: ");
        Serial.println(discoveredUrl);
      }
      return updated;
    }
    delay(30);
  }

  udp.stop();
  Serial.println("[DISCOVERY] No discovery response");
  return discover_server_via_http_scan();
}

String remote_config_build_url(const String &path) {
  if (path.length() == 0) {
    return serverUrl;
  }
  if (path.startsWith("/")) {
    return serverUrl + path;
  }
  return serverUrl + "/" + path;
}

bool remote_config_load_from_sd_data(const String &deviceId,
                                     const String admins[], int adminCount,
                                     const TeacherConfig teachers[], int teacherCount) {
  config.deviceId = deviceId;
  config.version = 1;
  config.adminCount = (adminCount > 5) ? 5 : adminCount;
  config.teacherCount = (teacherCount > 20) ? 20 : teacherCount;

  for (int i = 0; i < config.adminCount; i++) {
    config.admins[i] = admins[i];
  }
  for (int i = 0; i < config.teacherCount; i++) {
    config.teachers[i] = teachers[i];
  }
  config.loaded = true;

  prefs.begin("config", false);
  prefs.putBool("has_config", true);
  prefs.putString("device_id", config.deviceId);
  prefs.putInt("version", config.version);
  prefs.putInt("admin_count", config.adminCount);
  prefs.putInt("teacher_count", config.teacherCount);

  for (int i = 0; i < config.adminCount; i++) {
    String key = "admin_" + String(i);
    prefs.putString(key.c_str(), config.admins[i]);
  }

  for (int i = 0; i < config.teacherCount; i++) {
    String key = "teacher_" + String(i);
    prefs.putString(key.c_str(), config.teachers[i].name);
    prefs.putString((key + "_sub").c_str(), config.teachers[i].subjects);
    prefs.putBool((key + "_gfm").c_str(), config.teachers[i].isGfm);

    Serial.print("[CONFIG-SD] Saved Teacher ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(config.teachers[i].name);
    Serial.print(" | Subjects: ");
    Serial.print(config.teachers[i].subjects);
    if (config.teachers[i].isGfm) {
      Serial.print(" [GFM]");
    }
    Serial.println();
  }

  prefs.end();

  Serial.println("[CONFIG-SD] Full config saved to NVS from SD card");
  Serial.print("[CONFIG-SD] Admins: ");
  Serial.println(config.adminCount);
  Serial.print("[CONFIG-SD] Teachers: ");
  Serial.println(config.teacherCount);

  return true;
}
