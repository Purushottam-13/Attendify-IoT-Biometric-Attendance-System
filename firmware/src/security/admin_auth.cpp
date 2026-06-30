#include "admin_auth.h"
#include <Preferences.h>

// ============================================================
// STATE
// ============================================================
static bool authorized = false;
static String authorizedAdmin = "";
static Preferences prefs;

// ============================================================
// CHECK AUTHORIZATION
// ============================================================
bool admin_is_authorized() {
    prefs.begin("attendify", true);
    authorized = prefs.getBool("admin_auth", false);
    authorizedAdmin = prefs.getString("admin_name", "");
    prefs.end();
    return authorized;
}

// ============================================================
// AUTHORIZE ADMIN
// ============================================================
bool admin_authorize(const String& adminName) {
    prefs.begin("attendify", false);
    prefs.putBool("admin_auth", true);
    prefs.putString("admin_name", adminName);
    prefs.end();
    
    authorized = true;
    authorizedAdmin = adminName;
    
    Serial.print("[ADMIN] Authorized: ");
    Serial.println(adminName);
    return true;
}

// ============================================================
// VERIFY ADMIN (for re-entry)
// ============================================================
bool admin_verify() {
    // This will be called to verify admin fingerprint
    // Fingerprint ID 1 is reserved for admin
    return authorized;
}
