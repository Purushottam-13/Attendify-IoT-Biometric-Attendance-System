/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  WiFi HAL - Implementation
 * ============================================================
 */

#include "wifi_hal.h"
#include <WiFi.h>

// ============================================================
// INITIALIZATION
// ============================================================
void wifi_init() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect();
  delay(100);
  Serial.println("[WIFI] Initialized in STA mode (Auto-reconnect Enabled)");
}

// ============================================================
// CONNECTION
// ============================================================
bool wifi_connect(const char *ssid, const char *password) {
  Serial.print("[WIFI] Connecting to: ");
  Serial.println(ssid);

  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
      Serial.println();
      Serial.print("[WIFI] Connection failed, status=");
      Serial.println((int)status);
      return false;
    }

    if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("[WIFI] Connection timeout");
      return false;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("[WIFI] Connected!");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());

  return true;
}

void wifi_disconnect() {
  WiFi.disconnect();
  Serial.println("[WIFI] Disconnected");
}

void wifi_clear_credentials() {
  WiFi.disconnect(true, true); // turnOff, eraseCreds
  Serial.println("[WIFI] Credentials cleared");
}

// ============================================================
// STATUS
// ============================================================
bool wifi_is_connected() { return WiFi.status() == WL_CONNECTED; }

String wifi_get_ip() {
  if (!wifi_is_connected())
    return "0.0.0.0";
  return WiFi.localIP().toString();
}

String wifi_get_ssid() {
  if (!wifi_is_connected())
    return "";
  return WiFi.SSID();
}

int wifi_get_rssi() {
  if (!wifi_is_connected())
    return -100;
  return WiFi.RSSI();
}

// ============================================================
// NETWORK SCANNING
// ============================================================
int wifi_scan(String *ssids, int maxCount, bool *openFlags) {
  Serial.println("[WIFI] Scanning networks...");

  int n = WiFi.scanNetworks();

  if (n == 0) {
    Serial.println("[WIFI] No networks found");
    return 0;
  }

  int count = min(n, maxCount);
  for (int i = 0; i < count; i++) {
    ssids[i] = WiFi.SSID(i);
    if (openFlags != nullptr) {
      openFlags[i] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
  }

  Serial.print("[WIFI] Found ");
  Serial.print(n);
  Serial.println(" networks");

  WiFi.scanDelete();

  return count;
}

int wifi_scan(String *ssids, int maxCount) { return wifi_scan(ssids, maxCount, nullptr); }

bool wifi_is_open_network(const String &ssid) {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      WiFi.scanDelete();
      return isOpen;
    }
  }
  WiFi.scanDelete();
  return false;
}

bool wifi_reconnect() {
  if (wifi_is_connected())
    return true;

  Serial.println("[WIFI] Reconnecting using saved credentials...");
  WiFi.begin(); // Uses NVS saved SSID/Pass

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("[WIFI] Reconnect timeout");
      return false;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WIFI] Reconnected!");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static WiFiClientSecure secureClient;
static bool secureClientInsecureSet = false;

bool wifi_http_begin(HTTPClient &http, const String &url) {
  if (url.startsWith("https://")) {
    if (!secureClientInsecureSet) {
      secureClient.setInsecure();
      secureClientInsecureSet = true;
    }
    return http.begin(secureClient, url);
  } else {
    return http.begin(url);
  }
}

// ============================================================
// BACKGROUND AUTO-CONNECT ENGINE
// ============================================================
#include <Preferences.h>
#include <ArduinoJson.h>

#define WIFI_MAX_PROFILES 10
#define WIFI_SCAN_INTERVAL_MS 30000
#define WIFI_CONNECT_CHECK_MS 500

struct WifiProfile {
  String ssid;
  String password;
};

static WifiProfile storedProfiles[WIFI_MAX_PROFILES];
static int profileCount = 0;
static Preferences wifiPrefs;

enum WifiBgState {
  WIFI_BG_IDLE,
  WIFI_BG_SCANNING,
  WIFI_BG_CONNECTING
};

static WifiBgState bgState = WIFI_BG_IDLE;
static unsigned long lastScanTime = 0;
static unsigned long connectStartTime = 0;
static bool profilesLoaded = false;

void wifi_profiles_load() {
  wifiPrefs.begin("wifi_profiles", true); // read-only
  profileCount = wifiPrefs.getInt("count", 0);
  if (profileCount > WIFI_MAX_PROFILES) profileCount = WIFI_MAX_PROFILES;

  for (int i = 0; i < profileCount; i++) {
    String keyS = "ssid_" + String(i);
    String keyP = "pass_" + String(i);
    storedProfiles[i].ssid = wifiPrefs.getString(keyS.c_str(), "");
    storedProfiles[i].password = wifiPrefs.getString(keyP.c_str(), "");
  }
  wifiPrefs.end();

  profilesLoaded = true;
  Serial.printf("[WIFI_BG] Loaded %d stored profiles\n", profileCount);
  for (int i = 0; i < profileCount; i++) {
    Serial.printf("[WIFI_BG]   %d: %s\n", i, storedProfiles[i].ssid.c_str());
  }
}

static void wifi_profiles_persist() {
  wifiPrefs.begin("wifi_profiles", false); // read-write
  wifiPrefs.putInt("count", profileCount);
  for (int i = 0; i < profileCount; i++) {
    String keyS = "ssid_" + String(i);
    String keyP = "pass_" + String(i);
    wifiPrefs.putString(keyS.c_str(), storedProfiles[i].ssid);
    wifiPrefs.putString(keyP.c_str(), storedProfiles[i].password);
  }
  wifiPrefs.end();
}

void wifi_profiles_add(const String &ssid, const String &password) {
  if (ssid.length() == 0) return;

  // Check if already exists - update password
  for (int i = 0; i < profileCount; i++) {
    if (storedProfiles[i].ssid == ssid) {
      storedProfiles[i].password = password;
      wifi_profiles_persist();
      Serial.printf("[WIFI_BG] Updated profile: %s\n", ssid.c_str());
      return;
    }
  }

  // Add new
  if (profileCount >= WIFI_MAX_PROFILES) {
    Serial.println("[WIFI_BG] Max profiles reached, overwriting oldest");
    // Shift everything down
    for (int i = 0; i < WIFI_MAX_PROFILES - 1; i++) {
      storedProfiles[i] = storedProfiles[i + 1];
    }
    profileCount = WIFI_MAX_PROFILES - 1;
  }

  storedProfiles[profileCount].ssid = ssid;
  storedProfiles[profileCount].password = password;
  profileCount++;
  wifi_profiles_persist();
  Serial.printf("[WIFI_BG] Added profile: %s (total: %d)\n", ssid.c_str(), profileCount);
}

void wifi_profiles_save_from_json(const String &jsonArray) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonArray);
  if (err) {
    Serial.printf("[WIFI_BG] JSON parse error: %s\n", err.c_str());
    return;
  }

  if (!doc.is<JsonArray>()) {
    Serial.println("[WIFI_BG] Expected JSON array");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  int newCount = 0;

  for (JsonVariant item : arr) {
    if (newCount >= WIFI_MAX_PROFILES) break;
    String ssid = item["ssid"] | "";
    String pass = item["password"] | "";
    if (ssid.length() == 0) continue;

    storedProfiles[newCount].ssid = ssid;
    storedProfiles[newCount].password = pass;
    newCount++;
  }

  if (newCount > 0) {
    profileCount = newCount;
    wifi_profiles_persist();
    Serial.printf("[WIFI_BG] Saved %d profiles from server\n", profileCount);
  }
}

int wifi_profiles_count() {
  return profileCount;
}

void wifi_background_tick() {
  if (!profilesLoaded) {
    wifi_profiles_load();
  }

  // If already connected, nothing to do
  if (WiFi.status() == WL_CONNECTED) {
    bgState = WIFI_BG_IDLE;
    return;
  }

  // No profiles to try
  if (profileCount == 0) {
    return;
  }

  unsigned long now = millis();

  switch (bgState) {
  case WIFI_BG_IDLE: {
    // Wait for scan interval
    if (now - lastScanTime < WIFI_SCAN_INTERVAL_MS) {
      return;
    }
    // Start async scan (non-blocking)
    WiFi.scanNetworks(true); // true = async
    bgState = WIFI_BG_SCANNING;
    lastScanTime = now;
    Serial.println("[WIFI_BG] Async scan started");
    break;
  }

  case WIFI_BG_SCANNING: {
    int16_t scanResult = WiFi.scanComplete();
    if (scanResult == WIFI_SCAN_RUNNING) {
      return; // Still scanning, come back next tick
    }

    if (scanResult <= 0) {
      // No networks found or scan failed
      WiFi.scanDelete();
      bgState = WIFI_BG_IDLE;
      return;
    }

    // Scan complete - find strongest matching profile
    int bestRssi = -999;
    int bestProfileIdx = -1;

    for (int s = 0; s < scanResult; s++) {
      String scannedSsid = WiFi.SSID(s);
      int rssi = WiFi.RSSI(s);

      for (int p = 0; p < profileCount; p++) {
        if (storedProfiles[p].ssid == scannedSsid && rssi > bestRssi) {
          bestRssi = rssi;
          bestProfileIdx = p;
        }
      }
    }

    WiFi.scanDelete();

    if (bestProfileIdx >= 0) {
      // Found a match - connect
      Serial.printf("[WIFI_BG] Connecting to: %s (RSSI: %d)\n",
                    storedProfiles[bestProfileIdx].ssid.c_str(), bestRssi);
      WiFi.setAutoReconnect(true);
      WiFi.begin(storedProfiles[bestProfileIdx].ssid.c_str(),
                 storedProfiles[bestProfileIdx].password.c_str());
      bgState = WIFI_BG_CONNECTING;
      connectStartTime = now;
    } else {
      // Check for open networks as bootstrap
      for (int s = 0; s < scanResult; s++) {
        if (WiFi.encryptionType(s) == WIFI_AUTH_OPEN) {
          String openSsid = WiFi.SSID(s);
          if (openSsid.length() > 0) {
            Serial.printf("[WIFI_BG] Connecting to open network: %s\n", openSsid.c_str());
            WiFi.begin(openSsid.c_str());
            bgState = WIFI_BG_CONNECTING;
            connectStartTime = now;
            break;
          }
        }
      }
      if (bgState != WIFI_BG_CONNECTING) {
        bgState = WIFI_BG_IDLE;
      }
    }
    break;
  }

  case WIFI_BG_CONNECTING: {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      Serial.print("[WIFI_BG] Connected! IP: ");
      Serial.println(WiFi.localIP());
      bgState = WIFI_BG_IDLE;
    } else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
      Serial.println("[WIFI_BG] Connection failed");
      bgState = WIFI_BG_IDLE;
    } else if (now - connectStartTime > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("[WIFI_BG] Connection timeout");
      WiFi.disconnect();
      bgState = WIFI_BG_IDLE;
    }
    // Otherwise still connecting - return control to main loop
    break;
  }
  }
}

