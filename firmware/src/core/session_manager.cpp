/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 7
 *  Session Manager - Implementation
 * ============================================================
 */

#include "session_manager.h"
#include "../config/remote_config_loader.h"
#include "../core/device_info.h"
#include "../hal/rtc_hal.h"
#include "../hal/sd_hal.h"
#include "../hal/wifi_hal.h"
#include <HTTPClient.h>
#include <SD.h>
#include <vector>
#include <ArduinoJson.h>

// ============================================================
// STATE
// ============================================================
static ClassSession currentSession;
static std::vector<uint32_t>
    attendedStudents; // Map to actual Roll Numbers (32-bit)

// Forward declarations
static bool session_upload_data(const String &jsonData);

static String csv_escape_field(const String &value) {
  String escaped = value;
  escaped.replace("\"", "\"\"");

  if (escaped.indexOf(',') >= 0 || escaped.indexOf('"') >= 0 ||
      escaped.indexOf('\n') >= 0 || escaped.indexOf('\r') >= 0) {
    escaped = "\"" + escaped + "\"";
  }

  return escaped;
}

static std::vector<String> parse_csv_line(const String &line) {
  std::vector<String> fields;
  String field = "";
  bool inQuotes = false;

  for (int i = 0; i < (int)line.length(); i++) {
    char c = line.charAt(i);
    if (c == '"') {
      if (inQuotes && i + 1 < (int)line.length() && line.charAt(i + 1) == '"') {
        field += '"';
        i++;
      } else {
        inQuotes = !inQuotes;
      }
    } else if (c == ',' && !inQuotes) {
      field.trim();
      fields.push_back(field);
      field = "";
    } else {
      field += c;
    }
  }

  field.trim();
  fields.push_back(field);
  return fields;
}

static int csv_column_index(const std::vector<String> &headers,
                            const char *columnName, int fallbackIndex) {
  for (int i = 0; i < (int)headers.size(); i++) {
    if (headers[i] == columnName) {
      return i;
    }
  }
  return fallbackIndex;
}

static String csv_field(const std::vector<String> &fields, int index) {
  if (index < 0 || index >= (int)fields.size()) {
    return "";
  }
  return fields[index];
}

// ============================================================
// INITIALIZATION
// ============================================================
void session_init() {
  currentSession.active = false;
  currentSession.studentCount = 0;
  attendedStudents.clear();
  Serial.println("[SESSION] Manager initialized");
}

// ============================================================
// SESSION CONTROL
// ============================================================
bool session_start(const String &teacherName, const String &subject) {
  if (currentSession.active) {
    Serial.println("[SESSION] Error: Session already active");
    return false;
  }

  currentSession.active = true;
  currentSession.teacherName = teacherName;
  currentSession.subject = subject;
  currentSession.startTime = millis(); // Use RTC for actual timestamp recording
  currentSession.studentCount = 0;

  // Generate Session ID (YYYYMMDD_HHMMSS)
  currentSession.sessionId = rtc_now_string();
  currentSession.sessionId.replace(" ", "_");
  currentSession.sessionId.replace(":", "");
  currentSession.sessionId.replace("-", "");

  // Generate File Path: /attendance/[Teacher]/[Subject]_[Date].csv
  // 1. Ensure Teacher Directory
  if (!sd_ensure_teacher_dir(teacherName)) {
    Serial.println("[SESSION] Error: Could not create teacher dir");
    // Proceed anyway? No, offline storage is critical.
    // But maybe SD is just missing. We continue in RAM.
  }

  String safeTeacher = sd_sanitize_filename(teacherName);
  String safeSubject = sd_sanitize_filename(subject);
  String dateStr = rtc_get_date_string();
  // Assuming rtc_get_date_string() exists or we extract from now string
  // Let's use sessionId substring for date (YYYYMMDD)
  String datePart = currentSession.sessionId.substring(0, 8);

  currentSession.currentFilePath = "/attendance/" + safeTeacher + "/" +
                                   safeSubject + "_" + datePart + ".csv";

  // Check if file exists to add headers
  if (!sd_file_exists(currentSession.currentFilePath.c_str())) {
    // Add Excel-friendly BOM (Optional) or just Header
    // Header: Timestamp,Date,Time,SessionID,Subject,Teacher,StudentID,StudentName,DeviceID
    String header = "Timestamp,Date,Time,SessionID,Subject,Teacher,StudentID,"
                    "StudentName,DeviceID";
    sd_write_attendance(currentSession.currentFilePath, header);
  }

  attendedStudents.clear();

  Serial.print("[SESSION] Started: ");
  Serial.print(subject);
  Serial.print(" by ");
  Serial.println(teacherName);
  Serial.print("[SESSION] File: ");
  Serial.println(currentSession.currentFilePath);

  session_save_recovery_state();

  return true;
}

bool session_end() {
  if (!currentSession.active) {
    return false;
  }

  currentSession.active = false;
  currentSession.endTime = millis();

  Serial.print("[SESSION] Ended. Total students: ");
  Serial.println(currentSession.studentCount);

  Serial.println("[SESSION] Local recording complete.");

  session_delete_recovery_state();

  return true;
}

bool session_is_active() { return currentSession.active; }

ClassSession *session_get_current() { return &currentSession; }

// Phase 14: ID Mapping
#include "../hal/fp_mapping_hal.h"

// ============================================================
// ATTENDANCE RECORDING
// ============================================================
bool session_record_attendance(uint16_t internalId) {
  if (!currentSession.active) {
    return false;
  }

  // Resolve roll number from mapping
  uint32_t roll = fp_map_get_roll(internalId);
  if (roll == 0) {
    // If not in mapping, maybe it's a small roll number used directly
    // (Legacy/Special)
    roll = internalId;
  }

  // Check for duplicate (using resolved roll)
  for (uint16_t id : attendedStudents) {
    if (id == roll) {
      Serial.println("[SESSION] Duplicate attendance");
      return false;
    }
  }

  // Record
  attendedStudents.push_back(roll);
  currentSession.studentCount++;

  // Log to SD
  // Format: Timestamp,Date,Time,SessionID,Subject,Teacher,StudentID,StudentName,DeviceID
  String nowStr = rtc_now_string();
  String dateStr = nowStr.substring(0, 10);
  String timeStr = nowStr.substring(11);
  String studentName = fp_map_get_name(internalId);

  String record = csv_escape_field(nowStr) + "," + csv_escape_field(dateStr) +
                  "," + csv_escape_field(timeStr) + "," +
                  csv_escape_field(currentSession.sessionId) + "," +
                  csv_escape_field(currentSession.subject) + "," +
                  csv_escape_field(currentSession.teacherName) + "," +
                  csv_escape_field(String(roll)) + "," +
                  csv_escape_field(studentName) + "," +
                  csv_escape_field(device_get_id());

  sd_write_attendance(currentSession.currentFilePath, record);

  session_save_recovery_state();

  Serial.printf("[SESSION] Recorded Roll: %u (Slot %d)\n", roll, internalId);

  return true;
}

// ============================================================
// UTILS
// ============================================================
String session_get_duration_string() {
  if (!currentSession.active) {
    return "00:00";
  }

  unsigned long duration = (millis() - currentSession.startTime) / 1000;
  unsigned long hours = duration / 3600;
  unsigned long minutes = (duration % 3600) / 60;
  unsigned long seconds = duration % 60;

  char buf[10];
  if (hours > 0) {
    snprintf(buf, 10, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  } else {
    snprintf(buf, 10, "%02lu:%02lu", minutes, seconds);
  }

  return String(buf);
}

// ============================================================
// UPLOAD
// ============================================================
// Forward declare helper to keep main logic clean
static bool session_upload_data(const String &jsonData);

static bool session_upload_data(const String &jsonData) {
  if (!wifi_is_connected()) {
    Serial.println("[SESSION] Cancel upload: No WiFi");
    return false;
  }

  String payload;

  if (jsonData.length() > 0) {
    // Use provided payload (Offline Sync)
    payload = jsonData;
  } else {
    // Build payload from current session (Live End)
    payload = "{";
    payload += "\"sessionId\":\"" + currentSession.sessionId + "\",";
    payload += "\"teacher\":\"" + currentSession.teacherName + "\",";
    payload += "\"subject\":\"" + currentSession.subject + "\",";
    payload += "\"students\":[";

    for (size_t i = 0; i < attendedStudents.size(); i++) {
      payload += String(attendedStudents[i]);
      if (i < attendedStudents.size() - 1)
        payload += ",";
    }

    payload += "]}";
  }

  // Send POST
  HTTPClient http;
  String url = remote_config_build_url("/api/upload-attendance");

  Serial.println("[SESSION] Uploading to: " + url);
  wifi_http_begin(http, url);
  http.setConnectTimeout(1500);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-id", DEVICE_SERIAL);
  http.addHeader("x-device-secret", DEVICE_SECRET);

  int httpCode = http.POST(payload);
  bool success = false;

  if (httpCode == 200) {
    Serial.println("[SESSION] Upload success");
    success = true;
  } else {
    Serial.print("[SESSION] Upload failed: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }

  http.end();
  return success;
}

// ============================================================
// SYNC ENGINE
// ============================================================
#include <map>

int session_sync_pending(String teacherName) {
  if (!wifi_is_connected())
    return 0;

  String safeTeacher = sd_sanitize_filename(teacherName);
  String typeDir = "/attendance/" + safeTeacher;

  std::vector<String> files = sd_list_teacher_files(teacherName);
  int syncedFilesCount = 0; // Count FILES synced, not just sessions
  int totalSessionsUploaded = 0;

  Serial.printf("[SYNC] Found %d files for teacher %s\n", files.size(),
                teacherName.c_str());

  for (String fileName : files) {
    String fullPath = typeDir + "/" + fileName;
    Serial.print("[SYNC] Processing: ");
    Serial.println(fullPath);

    File file = SD.open(fullPath.c_str(), FILE_READ);
    if (!file) {
      Serial.println("[SYNC] Failed to open file, skipping");
      continue;
    }
    if (file.size() == 0) {
      Serial.println("[SYNC] File is empty, skipping");
      file.close();
      continue;
    }
    Serial.printf("[SYNC] File size: %d bytes\n", file.size());

    // Use a Map to Group by SessionID
    // Key: SessionID, Value: Vector of Students
    struct SessionData {
      String subject;
      String teacher;
      std::vector<uint32_t> students;
    };
    std::map<String, SessionData> sessionMap;

    int r = 0;
    int dataRowsFound = 0;
    int sessionIdCol = 3;
    int subjectCol = 4;
    int teacherCol = 5;
    int studentIdCol = 6;

    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) {
        continue;
      }

      if (r == 0) {
        std::vector<String> headers = parse_csv_line(line);
        sessionIdCol = csv_column_index(headers, "SessionID", sessionIdCol);
        subjectCol = csv_column_index(headers, "Subject", subjectCol);
        teacherCol = csv_column_index(headers, "Teacher", teacherCol);
        studentIdCol = csv_column_index(headers, "StudentID", studentIdCol);
      } else if (line.length() > 5) {
        dataRowsFound++;
        String rowSessionId, rowSubject, rowTeacher;
        uint32_t rowStudentId = 0;

        std::vector<String> fields = parse_csv_line(line);
        rowSessionId = csv_field(fields, sessionIdCol);
        rowSubject = csv_field(fields, subjectCol);
        rowTeacher = csv_field(fields, teacherCol);
        rowStudentId = (uint32_t)csv_field(fields, studentIdCol).toInt();

        // Debug output
        if (rowStudentId > 0) {
          Serial.printf("[SYNC] Parsed: SessionID=%s, Subject=%s, Teacher=%s, "
                        "StudentID=%u (cols=%d)\n",
                        rowSessionId.c_str(), rowSubject.c_str(),
                        rowTeacher.c_str(), rowStudentId, (int)fields.size());
        }

        // Safety: Only add if we have some identifier
        if (rowSessionId.length() > 0 && rowStudentId > 0) {
          if (sessionMap.find(rowSessionId) == sessionMap.end()) {
            SessionData newSession;
            newSession.subject = rowSubject;
            newSession.teacher =
                rowTeacher.length() > 0 ? rowTeacher : teacherName;
            sessionMap[rowSessionId] = newSession;
          }
          if (rowStudentId > 0) {
            sessionMap[rowSessionId].students.push_back(rowStudentId);
          }
        }
      }
      r++;
    }
    file.close();

    // Now Upload Each Session found in this file
    bool allUploaded = true;
    Serial.printf("[SYNC] File contains %d sessions\n", (int)sessionMap.size());
    if (sessionMap.size() > 0) {
      for (auto const &pair : sessionMap) {
        String sessId = pair.first;
        SessionData data = pair.second;

        if (data.students.size() == 0) {
          Serial.println("[SYNC] Warning: Session " + sessId +
                         " has no students. Skipping upload.");
          continue;
        }

        String payload = "{";
        payload += "\"sessionId\":\"" + sessId + "\",";
        payload += "\"teacher\":\"" + data.teacher + "\",";
        payload += "\"subject\":\"" + data.subject + "\",";
        payload += "\"students\":[";
        for (size_t i = 0; i < data.students.size(); i++) {
          payload += String(data.students[i]);
          if (i < data.students.size() - 1)
            payload += ",";
        }
        payload += "]}";

        if (session_upload_data(payload)) {
          Serial.println("[SYNC] Uploaded Session: " + sessId);
          totalSessionsUploaded++;
        } else {
          Serial.println("[SYNC] Failed Session: " + sessId);
          allUploaded = false;
        }
      }
    } else {
      // CRITICAL FIX: If we found data rows but map is empty, it's a parse
      // error. Do NOT set allUploaded = true.
      if (dataRowsFound > 0) {
        Serial.println("[SYNC] ERROR: Data rows found but failed to group into "
                       "sessions. Keeping file.");
        allUploaded = false;
      } else {
        // Truly empty file (header only) - we can sync it (move it out)
        allUploaded = true;
      }
    }

    // Only move file if ALL sessions in it were uploaded
    if (allUploaded) {
      syncedFilesCount++;
      // Move to synced folder
      if (!SD.exists("/synced")) {
        SD.mkdir("/synced");
      }

      String syncedDir = "/synced/" + safeTeacher;
      if (!SD.exists(syncedDir)) {
        SD.mkdir(syncedDir);
      }

      String newPath = syncedDir + "/" + fileName;
      if (SD.exists(newPath)) {
        SD.remove(newPath);
      }

      if (SD.rename(fullPath, newPath)) {
        Serial.println("[SYNC] Moved to: " + newPath);
      } else {
        // Manual Copy Fallback
        File src = SD.open(fullPath, FILE_READ);
        if (src) {
          File dst = SD.open(newPath, FILE_WRITE);
          if (dst) {
            while (src.available())
              dst.write(src.read());
            dst.close();
            src.close();
            SD.remove(fullPath);
            Serial.println("[SYNC] Manual move success");
          } else {
            src.close();
            Serial.println("[SYNC] Copy failed");
          }
        }
      }
    } else {
      Serial.println("[SYNC] Keeping file (partial/failed upload)");
    }
  }
  Serial.printf("[SYNC] session_sync_pending completed for %s. Total sessions "
                "uploaded: %d\n",
                teacherName.c_str(), totalSessionsUploaded);
  return totalSessionsUploaded;
}

// ============================================================
// SYNC ALL PENDING (GFM Bulk Upload)
// ============================================================
int session_sync_all_pending() {
  if (!wifi_is_connected()) {
    Serial.println("[SYNC] No WiFi for bulk sync");
    return 0;
  }

  int totalSynced = 0;
  std::vector<String> teachers;

  File attendanceDir = SD.open("/attendance");
  if (!attendanceDir) {
    Serial.println("[SYNC] ERROR: Attendance folder missing!");
    return 0;
  }

  while (File entry = attendanceDir.openNextFile()) {
    if (entry.isDirectory()) {
      String name = entry.name();

      // Extract basename
      int lastSlash = name.lastIndexOf('/');
      if (lastSlash != -1) {
        name = name.substring(lastSlash + 1);
      }

      if (name.length() > 0 && !name.startsWith(".")) {
        teachers.push_back(name);
      }
    }
    entry.close();
  }
  attendanceDir.close();

  Serial.printf("[SYNC] Found %d teacher folders to process\n",
                (int)teachers.size());

  for (const String &teacher : teachers) {
    Serial.printf("[SYNC] Checking teacher: %s\n", teacher.c_str());
    int count = session_sync_pending(teacher);
    totalSynced += count;
    Serial.printf("[SYNC]   -> Completed %s with %d uploads\n", teacher.c_str(),
                  count);
  }

  Serial.printf("[SYNC] Bulk sync finished. Total uploaded: %d\n", totalSynced);
  return totalSynced;
}

// ============================================================
// CHECK PENDING STATUS
// ============================================================
// ============================================================
// CHECK PENDING STATUS
// ============================================================
static bool cachedPendingState = false;

// Actual blocking check
bool session_check_pending() {
  // Safety check: Don't access SD if not initialized
  // Assuming sd_init was called, but robust check would occur in SD HAL
  // For now, valid SD open check is enough

  File attendanceDir = SD.open("/attendance");
  if (!attendanceDir) {
    return false;
  }

  // Check all teacher folders
  while (File teacherDir = attendanceDir.openNextFile()) {
    if (teacherDir.isDirectory()) {
      String teacherName = teacherDir.name();
      if (!teacherName.startsWith(".")) {
        // Check inside teacher folder for any CSV
        File subdir = SD.open("/attendance/" + teacherName);
        if (subdir) {
          while (File entry = subdir.openNextFile()) {
            String fname = entry.name();
            if (fname.endsWith(".csv")) {
              // FAST RETURN: Found at least one file
              entry.close();
              subdir.close();
              teacherDir.close();
              attendanceDir.close();
              return true;
            }
            entry.close();
          }
          subdir.close();
        }
      }
    }
    teacherDir.close();
  }
  attendanceDir.close();
  return false;
}

// Update the cache (Call periodically)
void session_update_pending_status() {
  cachedPendingState = session_check_pending();
}

// Get cached state (Fast)
bool session_get_pending_state() { return cachedPendingState; }
// ============================================================
// ADMIN: LIST FILES & UPLOAD
// ============================================================
void session_list_files_and_upload() {
  if (!wifi_is_connected()) {
    Serial.println("[FILE] No WiFi");
    return;
  }

  Serial.println("[FILE] Scanning SD Card...");
  String json = "[";
  bool first = true;

  // 1. Scan /attendance (Pending)
  File root = SD.open("/attendance");
  if (root) {
    while (File teacherDir = root.openNextFile()) {
      if (teacherDir.isDirectory()) {
        String tName = teacherDir.name();
        if (!tName.startsWith(".")) {
          File subdir = SD.open("/attendance/" + tName);
          if (subdir) {
            while (File file = subdir.openNextFile()) {
              if (!file.isDirectory()) {
                if (!first)
                  json += ",";
                json += "{\"name\":\"" + String(file.name()) + "\",";
                json += "\"dir\":\"Pending/" + tName + "\",";
                json += "\"size\":" + String(file.size()) + "}";
                first = false;
              }
              file.close();
            }
            subdir.close();
          }
        }
      }
      teacherDir.close();
    }
    root.close();
  }

  // 2. Scan /synced (Uploaded)
  root = SD.open("/synced");
  if (root) {
    while (File teacherDir = root.openNextFile()) {
      if (teacherDir.isDirectory()) {
        String tName = teacherDir.name();
        if (!tName.startsWith(".")) {
          File subdir = SD.open("/synced/" + tName);
          if (subdir) {
            while (File file = subdir.openNextFile()) {
              if (!file.isDirectory()) {
                if (!first)
                  json += ",";
                json += "{\"name\":\"" + String(file.name()) + "\",";
                json += "\"dir\":\"Synced/" + tName + "\",";
                json += "\"size\":" + String(file.size()) + "}";
                first = false;
              }
              file.close();
            }
            subdir.close();
          }
        }
      }
      teacherDir.close();
    }
    root.close();
  }

  json += "]";
  Serial.print("[FILE] Found files JSON length: ");
  Serial.println(json.length());

  // Upload
  HTTPClient http;
  String url = remote_config_build_url("/api/devices/" + device_get_id() +
                                       "/upload-file-list");

  wifi_http_begin(http, url);
  http.setConnectTimeout(1500);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-id", DEVICE_SERIAL);
  http.addHeader("x-device-secret", DEVICE_SECRET);

  // Wrap in JSON object
  String payload = "{\"files\":" + json + "}";

  int code = http.POST(payload);
  if (code == 200) {
    Serial.println("[FILE] List uploaded successfully");
  } else {
    Serial.print("[FILE] Upload failed: ");
    Serial.println(code);
  }
  http.end();
}

void session_save_recovery_state() {
  if (!currentSession.active) return;
  
  File file = SD.open("/attendance/active_session.json", FILE_WRITE);
  if (!file) {
    Serial.println("[SESSION] Error: Failed to open recovery file for writing");
    return;
  }

  JsonDocument doc;
  doc["active"] = true;
  doc["teacherName"] = currentSession.teacherName;
  doc["subject"] = currentSession.subject;
  doc["sessionId"] = currentSession.sessionId;
  doc["currentFilePath"] = currentSession.currentFilePath;
  doc["startTime"] = (uint32_t)currentSession.startTime;
  doc["studentCount"] = currentSession.studentCount;

  for (uint32_t roll : attendedStudents) {
    doc["attendedStudents"].add(roll);
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("[SESSION] Error: Failed to write JSON to recovery file");
  } else {
    Serial.println("[SESSION] Recovery state saved to SD");
  }
  file.close();
}

void session_delete_recovery_state() {
  if (sd_file_exists("/attendance/active_session.json")) {
    if (sd_delete_file("/attendance/active_session.json")) {
      Serial.println("[SESSION] Recovery state file removed");
    } else {
      Serial.println("[SESSION] Error: Failed to remove recovery file");
    }
  }
}

bool session_check_and_resume_recovery() {
  if (!sd_file_exists("/attendance/active_session.json")) {
    return false;
  }

  File file = SD.open("/attendance/active_session.json", FILE_READ);
  if (!file) {
    Serial.println("[SESSION] Error: Failed to open recovery file for reading");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("[SESSION] Error: Failed to parse recovery JSON: %s\n", err.c_str());
    sd_delete_file("/attendance/active_session.json");
    return false;
  }

  bool active = doc["active"] | false;
  if (!active) {
    sd_delete_file("/attendance/active_session.json");
    return false;
  }

  currentSession.active = true;
  currentSession.teacherName = doc["teacherName"] | "";
  currentSession.subject = doc["subject"] | "";
  currentSession.sessionId = doc["sessionId"] | "";
  currentSession.currentFilePath = doc["currentFilePath"] | "";
  currentSession.startTime = millis(); // Reset start time to current boot duration
  currentSession.studentCount = doc["studentCount"] | 0;

  attendedStudents.clear();
  if (doc["attendedStudents"].is<JsonArray>()) {
    JsonArray arr = doc["attendedStudents"].as<JsonArray>();
    for (JsonVariant val : arr) {
      attendedStudents.push_back(val.as<uint32_t>());
    }
  }

  // Restore current teacher login by matching the saved teacherName
  teacher_set_current_by_name(currentSession.teacherName);

  Serial.printf("[SESSION] Auto-recovered active session: %s by %s (Students: %d)\n",
                currentSession.subject.c_str(), currentSession.teacherName.c_str(), currentSession.studentCount);
  return true;
}
