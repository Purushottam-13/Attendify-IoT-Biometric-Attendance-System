/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 3
 *  Device Info - Unique Device Identification
 * ============================================================
 */

#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <Arduino.h>

// ============================================================
// DEVICE CONFIGURATION
// ============================================================
// Change this for each device before flashing!
#define DEVICE_SERIAL "ESP32_CLASSROOM_02"
#define DEVICE_SECRET "A1B2C3D4_SECRET"

// ============================================================
// DEVICE INFO FUNCTIONS
// ============================================================

/**
 * Initialize device info
 */
void device_info_init();

/**
 * Get unique Device ID
 * Format: IDF-ESP32-XXX or MAC-based
 * @return Device ID string
 */
String device_get_id();

/**
 * Get MAC address as string
 * @return MAC address (XX:XX:XX:XX:XX:XX)
 */
String device_get_mac();

/**
 * Get firmware version
 * @return Version string
 */
String device_get_version();

/**
 * Print device info to serial
 */
void device_print_info();

#endif // DEVICE_INFO_H
