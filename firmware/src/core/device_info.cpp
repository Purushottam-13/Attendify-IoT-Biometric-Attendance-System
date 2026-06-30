/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 3
 *  Device Info - Implementation
 * ============================================================
 */

#include "device_info.h"
#include <WiFi.h>

// ============================================================
// CONSTANTS
// ============================================================
#define FIRMWARE_VERSION "1.0.3"

static String deviceId = "";

// ============================================================
// INITIALIZATION
// ============================================================
void device_info_init() {
    // Use hardcoded serial for easy identification
    deviceId = DEVICE_SERIAL;
    
    Serial.println("[DEVICE] Initialized");
    device_print_info();
}

// ============================================================
// GETTERS
// ============================================================
String device_get_id() {
    return deviceId;
}

String device_get_mac() {
    return WiFi.macAddress();
}

String device_get_version() {
    return FIRMWARE_VERSION;
}

// ============================================================
// DEBUG
// ============================================================
void device_print_info() {
    Serial.println("========================================");
    Serial.println("  DEVICE INFORMATION");
    Serial.println("========================================");
    Serial.print("  Device ID: ");
    Serial.println(deviceId);
    Serial.print("  MAC:       ");
    Serial.println(device_get_mac());
    Serial.print("  FW Ver:    ");
    Serial.println(FIRMWARE_VERSION);
    Serial.println("========================================");
}
