/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 10
 *  Lecture History UI - Header
 * ============================================================
 */

#ifndef LECTURE_HISTORY_UI_H
#define LECTURE_HISTORY_UI_H

#include <Arduino.h>

// ============================================================
// LECTURE HISTORY UI FUNCTIONS
// ============================================================

/**
 * Initialize Lecture History UI
 */
void lecture_history_init();

/**
 * Handle Lecture History events
 */
void lecture_history_handle();

/**
 * Load lectures for current teacher
 */
void lecture_history_load(const String& teacherName);

#endif // LECTURE_HISTORY_UI_H
