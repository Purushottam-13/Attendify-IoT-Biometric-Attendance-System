/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  UI Renderer - Screen Logic
 * ============================================================
 */

#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <Arduino.h>
#include "../core/system_state.h"
#include "../hal/button_hal.h"

// ============================================================
// MENU ITEMS - Admin Mode, Teacher Mode, GFM Mode
// ============================================================
#define MENU_ITEM_COUNT 3

// ============================================================
// UI FUNCTIONS
// ============================================================

/**
 * Initialize UI renderer
 */
void ui_init();

/**
 * Render current screen based on system state
 * Call this in loop after state changes
 */
void ui_render();

/**
 * Handle button events and update state
 * @param event Button event from button_poll()
 */
void ui_handle_input(ButtonEvent event);

/**
 * Get currently selected menu index
 * @return Menu index (0-based)
 */
int ui_get_menu_selection();

/**
 * Show a temporary status message
 * @param title Header title
 * @param message Status message
 * @param duration_ms Duration to show (0 = until button)
 */
void ui_show_status(const char* title, const char* message, unsigned long duration_ms);

#endif // UI_RENDERER_H
