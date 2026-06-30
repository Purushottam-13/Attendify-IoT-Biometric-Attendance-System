/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 7
 *  Session Manager - Tracks active class sessions
 * ============================================================
 */

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "../security/teacher_auth.h"
#include <Arduino.h>

// ============================================================
// SESSION STRUCTURE
// ============================================================
struct ClassSession {
  bool active;
  String teacherName;
  String subject;
  unsigned long startTime;
  unsigned long endTime;
  int studentCount;
  String sessionId;       // Unique ID for the session (e.g. timestamp)
  String currentFilePath; // Path to CSV on SD
};

// ============================================================
// FUNCTIONS
// ============================================================

/**
 * Initialize session manager
 */
void session_init();

/**
 * Start a new session
 * @param teacherName Name of teacher
 * @param subject Subject name
 * @return true if started successfully
 */
bool session_start(const String &teacherName, const String &subject);

/**
 * End current session
 * @return true if ended successfully
 */
bool session_end();

/**
 * Check if session is active
 */
bool session_is_active();

/**
 * Get current session details
 */
ClassSession *session_get_current();

/**
 * Record attendance for a student
 * @param studentId Fingerprint ID of student
 * @return true if recorded (false if duplicate or error)
 */
bool session_record_attendance(uint16_t studentId);

/**
 * Get formatted duration string (HH:MM:SS)
 */
/**
 * Sync pending offline files for specific teacher
 * @param teacherName Teacher folder to check
 * @return Number of files synced
 */
/**
 * Get formatted duration string (HH:MM:SS)
 */
String session_get_duration_string();

int session_sync_pending(String teacherName);

/**
 * Sync ALL pending offline files from all teachers
 * Used by GFM for bulk upload
 * @return Total number of records synced
 */
int session_sync_all_pending();

/**
 * Check if there are any pending offline files
 * @return true if pending files exist
 */
/**
 * Check if there are any pending offline files (Blocking/Slow)
 * @return true if pending files exist
 */
bool session_check_pending();

/**
 * Update the cached pending status (Call periodically)
 */
void session_update_pending_status();

/**
 * Get the cached pending status (Fast, safe for UI)
 * @return true if pending files exist
 */
bool session_get_pending_state();

// Legacy wrapper: returns cached state
inline bool session_has_pending() { return session_get_pending_state(); }

/**
 * Scan SD card and upload file list to server (Admin Feature)
 */
void session_list_files_and_upload();

/**
 * Save active session info to SD card for boot recovery
 */
void session_save_recovery_state();

/**
 * Delete session recovery file from SD card
 */
void session_delete_recovery_state();

/**
 * Check if a recovered session file exists and resume it
 * @return true if session was successfully recovered
 */
bool session_check_and_resume_recovery();

#endif // SESSION_MANAGER_H
