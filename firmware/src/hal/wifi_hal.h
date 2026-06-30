/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  WiFi HAL - Connectivity Base
 * ============================================================
 */

#ifndef WIFI_HAL_H
#define WIFI_HAL_H

#include <Arduino.h>

// ============================================================
// WIFI CONFIGURATION
// ============================================================
#define WIFI_CONNECT_TIMEOUT_MS 10000

// ============================================================
// WIFI FUNCTIONS
// ============================================================

/**
 * Initialize WiFi subsystem
 */
void wifi_init();

/**
 * Connect to a WiFi network
 * @param ssid Network name
 * @param password Network password
 * @return true if connected
 */
bool wifi_connect(const char *ssid, const char *password);

/**
 * Disconnect from current network
 */
void wifi_disconnect();

/**
 * Disconnect and clear saved credentials
 */
void wifi_clear_credentials();

/**
 * Check if currently connected
 * @return true if connected
 */
bool wifi_is_connected();

/**
 * Get current IP address as string
 * @return IP address or "0.0.0.0"
 */
String wifi_get_ip();

/**
 * Get current SSID
 * @return SSID or empty string
 */
String wifi_get_ssid();

/**
 * Get signal strength
 * @return RSSI in dBm
 */
int wifi_get_rssi();

/**
 * Scan for available networks
 * @param ssids Output array of SSIDs
 * @param maxCount Maximum networks to find
 * @return Number of networks found
 */
int wifi_scan(String *ssids, int maxCount);

/**
 * Scan for available networks with optional open/secured flags
 * @param ssids Output array of SSIDs
 * @param maxCount Maximum networks to find
 * @param openFlags Optional output array (true=open, false=secured)
 * @return Number of networks found
 */
int wifi_scan(String *ssids, int maxCount, bool *openFlags);

/**
 * Returns true if the provided SSID is open (no password)
 */
bool wifi_is_open_network(const String &ssid);

/**
 * Reconnect to WiFi using saved credentials
 * @return true if connected
 */
bool wifi_reconnect();
/**
 * Initialize HTTPClient securely or insecurely based on URL
 * @param http Reference to HTTPClient
 * @param url Full URL string
 * @return true if begin succeeded
 */
bool wifi_http_begin(class HTTPClient &http, const String &url);

// ============================================================
// BACKGROUND AUTO-CONNECT ENGINE
// ============================================================

/**
 * Non-blocking background Wi-Fi tick. Call from loop().
 * Scans, matches stored profiles, connects silently.
 * Never blocks the main thread or displays anything on screen.
 */
void wifi_background_tick();

/**
 * Load Wi-Fi profiles from NVS into memory
 */
void wifi_profiles_load();

/**
 * Save a Wi-Fi profile to NVS (max 10 profiles)
 * @param ssid Network SSID
 * @param password Network password
 */
void wifi_profiles_add(const String &ssid, const String &password);

/**
 * Save profiles received from server config download.
 * Replaces all stored profiles with the new list.
 * @param json JSON array string: [{"ssid":"...","password":"..."},...]
 */
void wifi_profiles_save_from_json(const String &jsonArray);

/**
 * Get the number of stored Wi-Fi profiles
 */
int wifi_profiles_count();

#endif // WIFI_HAL_H
