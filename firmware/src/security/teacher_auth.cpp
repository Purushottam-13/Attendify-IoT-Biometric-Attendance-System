#include "teacher_auth.h"
#include "../config/config_manager.h"
#include "../hal/fingerprint_hal.h"
#include <Preferences.h>

// ============================================================
// STATE
// ============================================================
static std::vector<Teacher> teachers;
static int currentTeacherIndex = -1;
static Preferences prefs;

// ============================================================
// INITIALIZATION
// ============================================================
void teacher_init() {
    teachers.clear();
    currentTeacherIndex = -1;
    Serial.println("[TEACHER] Module initialized");
}

#include "../config/remote_config_loader.h"

void teacher_load_from_config() {
    teachers.clear();
    
    // Load from Remote Config (Source of Truth)
    if (!remote_config_is_loaded()) {
        Serial.println("[TEACHER] No config loaded");
        return;
    }
    
    DeviceConfig* cfg = remote_config_get();
    
    // Check enrollment status from local prefs
    prefs.begin("attendify", true);
    
    for (int i = 0; i < cfg->teacherCount; i++) {
        Teacher t;
        t.name = cfg->teachers[i].name;
        t.subjects = cfg->teachers[i].subjects;
        t.isGfm = cfg->teachers[i].isGfm;  // Copy GFM flag from config
        t.fingerprintId = TEACHER_FP_ID_START + i;
        
        // Enrollment is stored locally in "attendify" namespace
        // Key: t_enr_{index} (max 15 chars)
        String key = "t_enr_" + String(i);
        t.enrolled = prefs.getBool(key.c_str(), false);
        
        teachers.push_back(t);
        
        // Debug logging
        Serial.print("[TEACHER] Loaded: ");
        Serial.print(t.name);
        Serial.print(" | Subjects: ");
        Serial.print(t.subjects);
        Serial.print(" | GFM: ");
        Serial.print(t.isGfm ? "YES" : "NO");
        Serial.print(" | Enrolled: ");
        Serial.println(t.enrolled);
    }
    
    prefs.end();
    
    Serial.print("[TEACHER] Loaded ");
    Serial.print(teachers.size());
    Serial.println(" teachers from config");
}

// ============================================================
// GETTERS
// ============================================================
std::vector<Teacher>& teacher_get_list() {
    return teachers;
}

int teacher_get_count() {
    return teachers.size();
}

String teacher_get_current() {
    if (currentTeacherIndex >= 0 && currentTeacherIndex < (int)teachers.size()) {
        return teachers[currentTeacherIndex].name;
    }
    return "";
}

Teacher* teacher_get_current_obj() {
    if (currentTeacherIndex >= 0 && currentTeacherIndex < (int)teachers.size()) {
        return &teachers[currentTeacherIndex];
    }
    return NULL;
}

bool teacher_is_logged_in() {
    return currentTeacherIndex >= 0;
}

// ============================================================
// ENROLLMENT
// ============================================================
bool teacher_enroll(int index) {
    if (index < 0 || index >= (int)teachers.size()) {
        return false;
    }
    
    Teacher& t = teachers[index];
    Serial.print("[TEACHER] Enrolling: ");
    Serial.print(t.name);
    Serial.print(" (ID: ");
    Serial.print(t.fingerprintId);
    Serial.println(")");
    
    if (fingerprint_enroll(t.fingerprintId)) {
        t.enrolled = true;
        
        // Save to NVS (Enrollment status only)
        prefs.begin("attendify", false);
        String key = "t_enr_" + String(index);
        prefs.putBool(key.c_str(), true);
        prefs.end();
        
        Serial.println("[TEACHER] Enrollment successful");
        return true;
    }
    
    Serial.println("[TEACHER] Enrollment failed");
    return false;
}

// ============================================================
// VERIFICATION
// ============================================================
bool teacher_verify(int* matchedIndex) {
    *matchedIndex = -1;
    
    uint16_t matchedId;
    if (!fingerprint_verify(&matchedId)) {
        return false;
    }
    
    // Find teacher by fingerprint ID
    for (int i = 0; i < (int)teachers.size(); i++) {
        if (teachers[i].fingerprintId == matchedId && teachers[i].enrolled) {
            *matchedIndex = i;
            Serial.print("[TEACHER] Verified: ");
            Serial.println(teachers[i].name);
            return true;
        }
    }
    
    // Check if it's admin (ID 1)
    if (matchedId == 1) {
        Serial.println("[TEACHER] Admin fingerprint detected");
        return false;  // Admin is not a teacher
    }
    
    return false;
}

// ============================================================
// SESSION
// ============================================================
void teacher_set_current(int index) {
    if (index >= 0 && index < (int)teachers.size()) {
        currentTeacherIndex = index;
        Serial.print("[TEACHER] Logged in: ");
        Serial.println(teachers[index].name);
    }
}

void teacher_set_current_by_name(const String &name) {
    for (int i = 0; i < (int)teachers.size(); i++) {
        if (teachers[i].name == name) {
            currentTeacherIndex = i;
            Serial.print("[TEACHER] Recovered login for: ");
            Serial.println(teachers[i].name);
            return;
        }
    }
    Serial.println("[TEACHER] Warning: Recovered teacher name not found in current config list");
}

void teacher_logout() {
    if (currentTeacherIndex >= 0) {
        Serial.print("[TEACHER] Logged out: ");
        Serial.println(teachers[currentTeacherIndex].name);
    }
    currentTeacherIndex = -1;
}
