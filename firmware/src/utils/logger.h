/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Logger - Debug Utility
 * ============================================================
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// ============================================================
// LOG LEVELS
// ============================================================
enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

// ============================================================
// LOGGER FUNCTIONS
// ============================================================

/**
 * Initialize logger
 * @param baudRate Serial baud rate
 */
void logger_init(unsigned long baudRate = 115200);

/**
 * Log a debug message
 * @param module Module name (e.g., "FP", "WIFI")
 * @param message Message text
 */
void log_debug(const char* module, const char* message);

/**
 * Log an info message
 */
void log_info(const char* module, const char* message);

/**
 * Log a warning message
 */
void log_warn(const char* module, const char* message);

/**
 * Log an error message
 */
void log_error(const char* module, const char* message);

/**
 * Log with value (integer)
 */
void log_value(const char* module, const char* label, int value);

/**
 * Log with value (string)
 */
void log_value(const char* module, const char* label, const char* value);

#endif // LOGGER_H
