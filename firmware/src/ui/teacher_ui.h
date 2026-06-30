#ifndef TEACHER_UI_H
#define TEACHER_UI_H

#include "../core/system_state.h"

/**
 * Initialize teacher UI
 */
void teacher_ui_init();

/**
 * Handle input in teacher mode
 */
void teacher_ui_handle_input(int event);

/**
 * Update loop for teacher mode
 */
void teacher_ui_loop();

#endif // TEACHER_UI_H
