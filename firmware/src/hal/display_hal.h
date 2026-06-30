/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Display HAL - SH1106 OLED Driver
 * ============================================================
 */

#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <Arduino.h>

// ============================================================
// DISPLAY CONFIGURATION
// ============================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

// ============================================================
// DISPLAY FUNCTIONS
// ============================================================

/**
 * Initialize the OLED display
 * @return true if successful
 */
bool display_init();

/**
 * Clear the entire screen
 */
void display_clear();

/**
 * Draw a header bar at top of screen
 * @param title Text to show in header
 */
void display_header(const char *title);

/**
 * Display a centered message
 * @param msg Text message
 */
void display_message(const char *msg);

/**
 * Display a multi-line message
 * @param line1 First line
 * @param line2 Second line
 * @param line3 Third line
 */
void display_message_multi(const char *line1, const char *line2,
                           const char *line3);

/**
 * Display a message at a specific line (0-7)
 * @param line Line number (0=top, 7=bottom)
 * @param msg Text message
 */
void display_message_at(int line, const char *msg);

/**
 * Display a menu with selectable items
 * @param items Array of menu item strings
 * @param count Number of items
 * @param selected Currently selected index
 * @param y Optional Y start position (default 16)
 * @param maxItems Optional max visible items (default 4)
 */
void display_menu(const char *items[], int count, int selected, int y = 16,
                  int maxItems = 4);

/**
 * Draw a hint/footer at bottom of screen
 * @param hint Hint text
 */
void display_footer(const char *hint);

/**
 * Display a progress indicator
 * @param label Label text
 * @param progress Progress 0-100
 */
void display_progress(const char *label, int progress);

/**
 * Show boot screen with animated logo. Returns true if interrupted by SELECT button.
 */
bool display_boot_screen();

/**
 * Refresh display (call after drawing)
 */
void display_update();

/**
 * Invert the display colors (true = inverted, false = normal)
 */
void display_invert(bool invert);

#endif // DISPLAY_HAL_H
