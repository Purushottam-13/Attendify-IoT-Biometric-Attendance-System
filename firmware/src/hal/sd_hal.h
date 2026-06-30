/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  SD Card HAL - Local Storage
 * ============================================================
 */

#ifndef SD_HAL_H
#define SD_HAL_H

#include <Arduino.h>
#include <vector>

// ============================================================
// SD CONFIGURATION
// ============================================================
#define SD_CS_PIN    5
#define SD_SCK_PIN   18
#define SD_MOSI_PIN  23  // MOSI is GPIO23
#define SD_MISO_PIN  19  // MISO is GPIO19

// I2C pins for OLED/RTC
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN  21  // SDA is GPIO21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN  22  // SCL is GPIO22
#endif

// ============================================================
// SD FUNCTIONS
// ============================================================

/**
 * Initialize SD card
 * @return true if card mounted
 */
bool sd_init();

/**
 * Check if SD card is available
 * @return true if mounted
 */
bool sd_is_ready();

/**
 * Write attendance record to specific file
 * @param filePath Full path to CSV file
 * @param record Record string to write
 * @return true if written
 */
bool sd_write_attendance(const String &filePath, const String &record);

/**
 * Sanitize strings for filename usage
 */
String sd_sanitize_filename(String name);

/**
 * Ensure teacher directory exists
 */
bool sd_ensure_teacher_dir(String teacherName);

/**
 * Create directory if not exists
 * @param path Directory path
 * @return true if created or exists
 */
bool sd_mkdir(const char *path);

/**
 * Read file contents
 * @param path File path
 * @return File contents or empty string
 */
String sd_read_file(const char *path);

/**
 * Check if file exists
 * @param path File path
 * @return true if exists
 */
bool sd_file_exists(const char *path);

/**
 * Get list of CSV files in any directory
 */
std::vector<String> sd_list_csv_files(const char *dirPath);

/**
 * Get list of CSV files for a specific teacher
 */
std::vector<String> sd_list_teacher_files(String teacherName);

/**
 * Get list of CSV files recursively from a base path
 */
std::vector<String> sd_list_files_recursive(const String &basePath);

/**
 * Get SD card size in MB
 */
uint32_t sd_get_size_mb();
uint32_t sd_get_used_mb();

/**
 * Delete a file
 */
bool sd_delete_file(const char *path);

/**
 * Write binary data to a file on the SD card
 * @param path File path
 * @param data Data buffer
 * @param size Data size in bytes
 * @return true if written successfully
 */
bool sd_write_binary(const char *path, const uint8_t *data, size_t size);

/**
 * Read binary data from a file on the SD card
 * @param path File path
 * @param data Output data buffer
 * @param size Data size to read
 * @return true if read successfully
 */
bool sd_read_binary(const char *path, uint8_t *data, size_t size);

#endif // SD_HAL_H
