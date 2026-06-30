/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  RTC HAL - DS3231 Implementation
 * ============================================================
 */

#include "rtc_hal.h"
#include <Wire.h>
#include <Wire.h>
#include <RTClib.h>
#include <time.h>
#include "wifi_hal.h"

// ============================================================
// RTC OBJECT
// ============================================================
static RTC_DS3231 rtc;
static bool rtcInitialized = false;

// ============================================================
// INITIALIZATION
// ============================================================
bool rtc_init() {
    // Set I2C pins explicitly (SDA=21, SCL=22)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (!rtc.begin()) {
        Serial.println("[RTC] DS3231 NOT found");
        return false;
    }
    
    if (rtc.lostPower()) {
        Serial.println("[RTC] Lost power, time might be invalid");
        // Don't auto-reset to compile time, let user sync via NTP
        // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    
    rtcInitialized = true;
    Serial.println("[RTC] DS3231 initialized");
    Serial.print("[RTC] Current time: ");
    Serial.println(rtc_now_string());
    
    return true;
}

// ============================================================
// STATUS
// ============================================================
bool rtc_is_running() {
    return rtcInitialized && !rtc.lostPower();
}

// ============================================================
// TIME GETTERS
// ============================================================
String rtc_now_string() {
    if (!rtcInitialized) return "RTC ERROR";
    
    DateTime now = rtc.now();
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buffer);
}

String rtc_get_date_string() {
    if (!rtcInitialized) return "RTC ERROR";
    
    DateTime now = rtc.now();
    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             now.year(), now.month(), now.day());
    return String(buffer);
}

String rtc_time_string() {
    if (!rtcInitialized) return "RTC ERROR";
    
    DateTime now = rtc.now();
    uint8_t h = now.hour();
    const char* ap = (h >= 12) ? "PM" : "AM";
    h = h % 12;
    if (h == 0) h = 12;
    
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d %s",
             h, now.minute(), now.second(), ap);
    return String(buffer);
}

// ============================================================
// TIME SETTER
// ============================================================
void rtc_set_time(uint16_t year, uint8_t month, uint8_t day,
                  uint8_t hour, uint8_t minute, uint8_t second) {
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
    Serial.print("[RTC] Time set to: ");
    Serial.println(rtc_now_string());
}

// ============================================================
// UNIX TIMESTAMP
// ============================================================
uint32_t rtc_unix_time() {
    if (!rtcInitialized) return 0;
    return rtc.now().unixtime();
}

// ============================================================
// NTP SYNC
// ============================================================
bool rtc_sync_ntp() {
    Serial.println("[RTC] Starting NTP sync for IST (UTC+5:30)...");
    
    // Configure time (19800 sec = 5.5 hours)
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
    
    // Wait for time to be set
    time_t now = time(nullptr);
    int retries = 0;
    while (now < 100000 && retries < 40) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        retries++;
    }
    Serial.println();
    
    if (now < 100000) {
        Serial.println("[RTC] NTP Sync Failed");
        return false;
    }
    
    struct tm* timeinfo = localtime(&now);
    
    // Adjust RTC
    rtc.adjust(DateTime(
        timeinfo->tm_year + 1900,
        timeinfo->tm_mon + 1,
        timeinfo->tm_mday,
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec
    ));
    
    Serial.println("[RTC] Sync Success!");
    Serial.print("[RTC] New time: ");
    Serial.println(rtc_now_string());
    
    return true;
}
