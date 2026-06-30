/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 10
 *  GFM Mode UI - Header
 * ============================================================
 */

#ifndef GFM_UI_H
#define GFM_UI_H

#include "../hal/button_hal.h"
#include <Arduino.h>


// ============================================================
// GFM UI FUNCTIONS
// ============================================================

/**
 * Initialize GFM UI
 */
void gfm_ui_init();

/**
 * Handle GFM mode events
 * @param event Button event from main loop
 */
void gfm_ui_handle(ButtonEvent event);

/**
 * Show GFM main menu
 */
void gfm_ui_show_menu();

/**
 * Set student data for enrollment (from background polling)
 */
void gfm_ui_set_student(int roll, const String &name);

#endif // GFM_UI_H
