/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Button HAL - Input System with Debounce
 * ============================================================
 */

#ifndef BUTTON_HAL_H
#define BUTTON_HAL_H

#include <Arduino.h>

// ============================================================
// BUTTON PIN CONFIGURATION
// ============================================================
#define BTN_UP_PIN     32
#define BTN_DOWN_PIN   33
#define BTN_SELECT_PIN 25
#define BTN_BACK_PIN   26

// Timing constants
#define DEBOUNCE_MS       200
#define LONG_PRESS_MS     1000

// ============================================================
// BUTTON EVENTS
// ============================================================
enum ButtonEvent {
    BTN_NONE,           // No event
    BTN_UP,             // Up pressed
    BTN_DOWN,           // Down pressed
    BTN_SELECT,         // Select pressed (short)
    BTN_BACK,           // Back pressed
    BTN_SELECT_LONG     // Select held (long press)
};

// ============================================================
// BUTTON FUNCTIONS
// ============================================================

/**
 * Initialize button pins
 */
void button_init();

/**
 * Poll buttons and return event
 * Call this in loop()
 * @return ButtonEvent or BTN_NONE
 */
ButtonEvent button_poll();

/**
 * Get human-readable event name
 * @param event The button event
 * @return Event name string
 */
const char* button_event_name(ButtonEvent event);

/**
 * Check if any button is currently pressed
 * @return true if pressed
 */
bool button_any_pressed();

/**
 * Reset internal button state
 * Call this after long blocking operations
 */
void button_reset();

#endif // BUTTON_HAL_H
