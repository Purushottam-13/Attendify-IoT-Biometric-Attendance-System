/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  SD Card HAL - Implementation
 * ============================================================
 */

#include "sd_hal.h"
#include <SD.h>
#include <SPI.h>

// ============================================================
// STATE
// ============================================================
static bool sdReady = false;
static bool inInit = false;

// Helper to sanitize filenames (replace special chars)
String sd_sanitize_filename(String name) {
  String safe = "";
  for (unsigned int i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    if (isalnum(c) || c == '-' || c == '_') {
      safe += c;
    } else if (c == ' ') {
      safe += "_";
    }
  }
  return safe;
}

// ============================================================
// INITIALIZATION
// ============================================================

bool sd_init() {
  if (inInit) return false;
  inInit = true;

  // Free SD resources if previously active
  SD.end();

  // Explicitly set SPI pins for SD card (custom mapping)
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  // Use a stable SPI frequency (4MHz) to prevent communication errors
  // seen as "no token received" or "Card Failed" in logs.
  if (!SD.begin(SD_CS_PIN, SPI, 4000000)) {
    Serial.println("[SD] Card mount FAILED");
    sdReady = false;
    inInit = false;
    return false;
  }
  // Note: I2C for OLED/RTC is SDA=GPIO21, SCL=GPIO22 (shared bus)

  sdReady = true;
  Serial.println("[SD] Card mounted");
  Serial.print("[SD] Size: ");
  Serial.print(sd_get_size_mb());
  Serial.println(" MB");

  // Create known top-level directories used by setup/runtime flows.
  sd_mkdir("/attendance");
  sd_mkdir("/config");
  sd_mkdir("/fps");

  inInit = false;
  return true;
}

// ============================================================
// STATUS
// ============================================================
bool sd_is_ready() {
  if (inInit) {
    return sdReady;
  }
  if (!sdReady) {
    return sd_init();
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println("[SD] Card detached or unresponsive. Re-mounting...");
    sdReady = false;
    return sd_init();
  }
  return true;
}

std::vector<String> sd_list_csv_files(const char *dirPath) {
  std::vector<String> files;
  if (!sd_is_ready())
    return files;

  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    Serial.println("[SD] Failed to open dir for listing");
    return files;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.endsWith(".csv") || name.endsWith(".CSV")) {
        files.push_back(name);
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return files;
}

std::vector<String> sd_list_teacher_files(String teacherName) {
  std::vector<String> files;
  if (!sd_is_ready())
    return files;

  String safeName = sd_sanitize_filename(teacherName);
  String path = "/attendance/" + safeName;

  File dir = SD.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    Serial.print("[SD] Teacher dir not found: ");
    Serial.println(path);
    return files;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.endsWith(".csv") || name.endsWith(".CSV")) {
        files.push_back(name);
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return files;
}

std::vector<String> sd_list_files_recursive(const String &basePath) {
  std::vector<String> files;
  if (!sd_is_ready())
    return files;

  File root = SD.open(basePath);
  if (!root || !root.isDirectory()) {
    return files;
  }

  File entry = root.openNextFile();
  while (entry) {
    String name = String(entry.name());
    String fullPath = basePath;
    if (!fullPath.endsWith("/"))
      fullPath += "/";
    fullPath += name;

    if (entry.isDirectory()) {
      // Recursively add files from subdirs
      std::vector<String> subFiles = sd_list_files_recursive(fullPath);
      files.insert(files.end(), subFiles.begin(), subFiles.end());
    } else {
      if (name.endsWith(".csv") || name.endsWith(".CSV")) {
        files.push_back(fullPath);
      }
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();
  return files;
}

uint32_t sd_get_size_mb() {
  if (!sdReady)
    return 0;
  return SD.totalBytes() / (1024 * 1024);
}

uint32_t sd_get_used_mb() {
  if (!sdReady)
    return 0;
  return SD.usedBytes() / (1024 * 1024);
}

bool sd_delete_file(const char *path) {
  if (!sd_is_ready())
    return false;
  return SD.remove(path);
}

// ============================================================
// FILE OPERATIONS
// ============================================================
bool sd_mkdir(const char *path) {
  if (!sd_is_ready())
    return false;

  if (SD.exists(path)) {
    return true;
  }

  // Create folders recursively?
  // SD lib mkdir handles nested paths depending on implementation,
  // but standard Arduino SD library usually requires parent to exist.
  // For now, we assume simple 1-level depth or standard behavior.

  if (SD.mkdir(path)) {
    Serial.print("[SD] Created dir: ");
    Serial.println(path);
    return true;
  }

  Serial.print("[SD] Failed to create dir: ");
  Serial.println(path);
  return false;
}

// Ensure teacher directory exists
bool sd_ensure_teacher_dir(String teacherName) {
  String safeName = sd_sanitize_filename(teacherName);
  String path = "/attendance/" + safeName;
  return sd_mkdir(path.c_str());
}

bool sd_file_exists(const char *path) {
  if (!sd_is_ready())
    return false;
  return SD.exists(path);
}

String sd_read_file(const char *path) {
  if (!sd_is_ready())
    return "";

  if (!SD.exists(path)) {
    return "";
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    return "";
  }

  String content = "";
  size_t fileSize = file.size();
  if (fileSize > 0) {
    content.reserve(fileSize);
  }

  while (file.available()) {
    content += (char)file.read();
  }
  file.close();

  return content;
}

// ============================================================
// ATTENDANCE RECORDING
// ============================================================
bool sd_write_attendance(const String &filePath, const String &record) {
  if (!sd_is_ready()) {
    Serial.println("[SD] Not ready, cannot write");
    return false;
  }

  File file = SD.open(filePath, FILE_APPEND);
  if (!file) {
    Serial.print("[SD] Failed to open: ");
    Serial.println(filePath);
    return false;
  }

  file.println(record);
  file.close();

  Serial.print("[SD] Recorded to ");
  Serial.print(filePath);
  Serial.print(": ");
  Serial.println(record);

  return true;
}

bool sd_write_binary(const char *path, const uint8_t *data, size_t size) {
  if (!sd_is_ready()) {
    Serial.println("[SD] Not ready, cannot write binary");
    return false;
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.print("[SD] Failed to open for writing: ");
    Serial.println(path);
    return false;
  }

  size_t written = file.write(data, size);
  file.close();

  if (written != size) {
    Serial.print("[SD] Binary write mismatch: ");
    Serial.printf("%d of %d bytes written\n", written, size);
    return false;
  }

  return true;
}

bool sd_read_binary(const char *path, uint8_t *data, size_t size) {
  if (!sd_is_ready()) {
    Serial.println("[SD] Not ready, cannot read binary");
    return false;
  }

  if (!SD.exists(path)) {
    return false;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.print("[SD] Failed to open for reading: ");
    Serial.println(path);
    return false;
  }

  size_t bytesRead = file.read(data, size);
  file.close();

  if (bytesRead != size) {
    Serial.print("[SD] Binary read mismatch: ");
    Serial.printf("%d of %d bytes read\n", bytesRead, size);
    return false;
  }

  return true;
}
