#pragma once
#include <Arduino.h>
#include <vector>

// Admin fingerprint ID (enrolled during setup)
#define ADMIN_FP_ID 1

// Teacher fingerprint IDs start at 10 (1-9 reserved for admins)
#define TEACHER_FP_ID_START 10
#define MAX_TEACHERS 20

struct Teacher {
    String name;
    String subjects;
    uint16_t fingerprintId;
    bool enrolled;
    bool isGfm; // New Phase 10
};

// ============================================================
// TEACHER AUTH FUNCTIONS
// ============================================================

/**
 * Initialize teacher auth module
 */
void teacher_init();

/**
 * Load teachers from config
 */
void teacher_load_from_config();

/**
 * Get list of teachers
 */
std::vector<Teacher>& teacher_get_list();

/**
 * Get teacher count
 */
int teacher_get_count();

/**
 * Enroll teacher fingerprint
 * @param index Teacher index in list
 * @return true if enrollment successful
 */
bool teacher_enroll(int index);

/**
 * Verify teacher fingerprint
 * @param matchedIndex Output: index of matched teacher (-1 if not found)
 * @return true if teacher verified
 */
bool teacher_verify(int* matchedIndex);

/**
 * Get currently logged in teacher
 * @return Teacher name or empty string
 */
String teacher_get_current();

/**
 * Get current teacher object
 */
Teacher* teacher_get_current_obj();

/**
 * Set current teacher (after verification)
 */
void teacher_set_current(int index);

/**
 * Set current teacher by name (used during boot recovery)
 */
void teacher_set_current_by_name(const String &name);

/**
 * Log out current teacher
 */
void teacher_logout();

/**
 * Check if teacher is logged in
 */
bool teacher_is_logged_in();
