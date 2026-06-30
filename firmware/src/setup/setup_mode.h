#pragma once
#include <Arduino.h>

// Setup states for sub-menu navigation
enum SetupStep {
    SETUP_MENU,           // Main setup menu
    SETUP_WIFI,           // WiFi configuration
    SETUP_WIFI_SCANNING,  // Scanning for networks
    SETUP_WIFI_CONNECT,   // Connecting to network
    SETUP_CONFIG,         // Load config from SD
    SETUP_CONFIG_LOADING, // Loading config
    SETUP_ADMIN,          // Admin authorization
    SETUP_ADMIN_ENROLL,   // Enrolling admin fingerprint
    SETUP_COMPLETE        // Setup finished
};

void setup_mode_init();
void setup_mode_loop();
bool setup_is_completed();

// Setup sub-state management
SetupStep setup_get_step();
void setup_set_step(SetupStep step);
int setup_get_selection();
void setup_handle_input(int event);  // 0=none, 1=up, 2=down, 3=select, 4=back

// WiFi state getters
int setup_get_wifi_count();
int setup_get_wifi_selection();
String setup_get_wifi_ssid(int index);
bool setup_get_wifi_is_open(int index);

// Complete setup and lock
void setup_finish();

// Reset setup (to force re-entry)
void setup_reset();
