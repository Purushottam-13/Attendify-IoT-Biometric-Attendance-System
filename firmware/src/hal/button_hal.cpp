/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Button HAL - Input System Implementation
 * ============================================================
 */

#include "button_hal.h"

// ============================================================
// INTERNAL STATE
// ============================================================
static unsigned long lastDebounceTime = 0;
static unsigned long selectPressStart = 0;
static bool selectWasPressed = false;
static bool upWasPressed = false;
static bool downWasPressed = false;
static bool backWasPressed = false;
static ButtonEvent lastEvent = BTN_NONE;

// ============================================================
// INITIALIZATION
// ============================================================
void button_init() {
    pinMode(BTN_UP_PIN, INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_SELECT_PIN, INPUT_PULLUP);
    pinMode(BTN_BACK_PIN, INPUT_PULLUP);

    delay(5);

    bool upLow = (digitalRead(BTN_UP_PIN) == LOW);
    bool downLow = (digitalRead(BTN_DOWN_PIN) == LOW);
    bool selectLow = (digitalRead(BTN_SELECT_PIN) == LOW);
    bool backLow = (digitalRead(BTN_BACK_PIN) == LOW);

    if (upLow || downLow || selectLow || backLow) {
        Serial.println("[BUTTON] WARNING: One or more button pins are LOW at boot");
        Serial.print("[BUTTON] Raw state U/D/S/B: ");
        Serial.print(upLow ? "0" : "1");
        Serial.print("/");
        Serial.print(downLow ? "0" : "1");
        Serial.print("/");
        Serial.print(selectLow ? "0" : "1");
        Serial.print("/");
        Serial.println(backLow ? "0" : "1");
    }

    upWasPressed = upLow;
    downWasPressed = downLow;
    backWasPressed = backLow;
    selectWasPressed = selectLow;
    
    Serial.println("[BUTTON] Initialized on pins 32,33,25,26");
}

// ============================================================
// BUTTON POLLING
// ============================================================
ButtonEvent button_poll() {
    unsigned long now = millis();
    
    // Debounce check
    if (now - lastDebounceTime < DEBOUNCE_MS) {
        return BTN_NONE;
    }
    
    // Read button states (active LOW due to pullup)
    bool upPressed = (digitalRead(BTN_UP_PIN) == LOW);
    bool downPressed = (digitalRead(BTN_DOWN_PIN) == LOW);
    bool selectPressed = (digitalRead(BTN_SELECT_PIN) == LOW);
    bool backPressed = (digitalRead(BTN_BACK_PIN) == LOW);
    
    ButtonEvent event = BTN_NONE;
    
    // Handle SELECT with long press detection
    if (selectPressed) {
        if (!selectWasPressed) {
            // Button just pressed
            selectWasPressed = true;
            selectPressStart = now;
        } else {
            // Check for long press
            if (now - selectPressStart >= LONG_PRESS_MS) {
                event = BTN_SELECT_LONG;
                selectWasPressed = false;  // Reset to avoid repeat
                lastDebounceTime = now;
                return event;
            }
        }
    } else if (selectWasPressed) {
        // Button released - was it a short press?
        if (now - selectPressStart < LONG_PRESS_MS) {
            event = BTN_SELECT;
        }
        selectWasPressed = false;
    }
    
    // Handle other buttons (edge-triggered press detection)
    if (event == BTN_NONE) {
        if (upPressed && !upWasPressed) {
            event = BTN_UP;
        } else if (downPressed && !downWasPressed) {
            event = BTN_DOWN;
        } else if (backPressed && !backWasPressed) {
            event = BTN_BACK;
        }
    }

    upWasPressed = upPressed;
    downWasPressed = downPressed;
    backWasPressed = backPressed;
    
    // Update debounce timer if event occurred
    if (event != BTN_NONE) {
        lastDebounceTime = now;
        
        // Log event
        Serial.print("[BUTTON] ");
        Serial.println(button_event_name(event));
    }
    
    return event;
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================
const char* button_event_name(ButtonEvent event) {
    switch (event) {
        case BTN_NONE:        return "NONE";
        case BTN_UP:          return "UP";
        case BTN_DOWN:        return "DOWN";
        case BTN_SELECT:      return "SELECT";
        case BTN_BACK:        return "BACK";
        case BTN_SELECT_LONG: return "SELECT_LONG";
        default:              return "UNKNOWN";
    }
}

bool button_any_pressed() {
    return (digitalRead(BTN_UP_PIN) == LOW) ||
           (digitalRead(BTN_DOWN_PIN) == LOW) ||
           (digitalRead(BTN_SELECT_PIN) == LOW) ||
           (digitalRead(BTN_BACK_PIN) == LOW);
}

void button_reset() {
    lastDebounceTime = millis();
    upWasPressed = (digitalRead(BTN_UP_PIN) == LOW);
    downWasPressed = (digitalRead(BTN_DOWN_PIN) == LOW);
    selectWasPressed = (digitalRead(BTN_SELECT_PIN) == LOW);
    backWasPressed = (digitalRead(BTN_BACK_PIN) == LOW);
    selectPressStart = 0;
    lastEvent = BTN_NONE;
    Serial.println("[BUTTON] State reset");
}
