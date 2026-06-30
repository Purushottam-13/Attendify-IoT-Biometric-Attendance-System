/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  RTC HAL - DS3231 Time Source
 * ============================================================
 */

#ifndef RTC_HAL_H
#define RTC_HAL_H

#include <Arduino.h>

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

// ============================================================
// RTC FUNCTIONS
// ============================================================

/**
 * Initialize RTC module
 * @return true if RTC detected
 */
bool rtc_init();

/**
 * Check if RTC is running
 * @return true if running
 */
bool rtc_is_running();

/**
 * Get current timestamp as string
 * Format: "YYYY-MM-DD HH:MM:SS"
 * @return Timestamp string
 */
String rtc_now_string();

/**
 * Get date only
 * Format: "YYYY-MM-DD"
 * @return Date string
 */
String rtc_get_date_string();

/**
 * Get time only
 * Format: "HH:MM:SS"
 * @return Time string
 */
String rtc_time_string();

/**
 * Set RTC time
 * @param year Full year (e.g., 2026)
 * @param month Month (1-12)
 * @param day Day (1-31)
 * @param hour Hour (0-23)
 * @param minute Minute (0-59)
 * @param second Second (0-59)
 */
void rtc_set_time(uint16_t year, uint8_t month, uint8_t day,
                  uint8_t hour, uint8_t minute, uint8_t second);

/**
 * Get Unix timestamp
 * @return Seconds since 1970
 */
uint32_t rtc_unix_time();

/**
 * Sync RTC with NTP (Internet Time)
 * Uses IST (UTC+5:30)
 * @return true if successful
 */
bool rtc_sync_ntp();

#endif // RTC_HAL_H
