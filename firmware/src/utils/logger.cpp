/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Logger - Implementation
 * ============================================================
 */

#include "logger.h"

// ============================================================
// INITIALIZATION
// ============================================================
void logger_init(unsigned long baudRate) {
    Serial.begin(baudRate);
    while (!Serial) {
        delay(10);
    }
    Serial.println();
    Serial.println("========================================");
    Serial.println("  ATTENDIFY HARDWARE - PHASE 1");
    Serial.println("  Device Foundation");
    Serial.println("========================================");
}

// ============================================================
// LOGGING FUNCTIONS
// ============================================================
static void log_print(LogLevel level, const char* module, const char* message) {
    const char* levelStr;
    switch (level) {
        case LOG_DEBUG: levelStr = "DBG"; break;
        case LOG_INFO:  levelStr = "INF"; break;
        case LOG_WARN:  levelStr = "WRN"; break;
        case LOG_ERROR: levelStr = "ERR"; break;
        default:        levelStr = "???"; break;
    }
    
    Serial.print("[");
    Serial.print(levelStr);
    Serial.print("][");
    Serial.print(module);
    Serial.print("] ");
    Serial.println(message);
}

void log_debug(const char* module, const char* message) {
    log_print(LOG_DEBUG, module, message);
}

void log_info(const char* module, const char* message) {
    log_print(LOG_INFO, module, message);
}

void log_warn(const char* module, const char* message) {
    log_print(LOG_WARN, module, message);
}

void log_error(const char* module, const char* message) {
    log_print(LOG_ERROR, module, message);
}

void log_value(const char* module, const char* label, int value) {
    Serial.print("[INF][");
    Serial.print(module);
    Serial.print("] ");
    Serial.print(label);
    Serial.print(": ");
    Serial.println(value);
}

void log_value(const char* module, const char* label, const char* value) {
    Serial.print("[INF][");
    Serial.print(module);
    Serial.print("] ");
    Serial.print(label);
    Serial.print(": ");
    Serial.println(value);
}
