/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 1
 *  Display HAL - SH1106 OLED Implementation
 * ============================================================
 */

#include "display_hal.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Wire.h>


// ============================================================
// DISPLAY OBJECT
// ============================================================
static Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================
// INITIALIZATION
// ============================================================
bool display_init() {
  // Set I2C pins explicitly (SDA=21, SCL=22)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!display.begin(OLED_I2C_ADDR, true)) {
    Serial.println("[DISPLAY] SH1106 init FAILED");
    return false;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.display();

  Serial.println("[DISPLAY] SH1106 initialized");
  return true;
}

// ============================================================
// BASIC FUNCTIONS
// ============================================================
void display_clear() { display.clearDisplay(); }

void display_update() { display.display(); }

// ============================================================
// HEADER & FOOTER
// ============================================================
void display_header(const char *title) {
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(1);
  display.setCursor(4, 2);
  display.print(title);
  display.setTextColor(SH110X_WHITE);
}

void display_footer(const char *hint) {
  display.drawLine(0, 54, SCREEN_WIDTH, 54, SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 56);
  display.print(hint);
}

// ============================================================
// MESSAGE DISPLAY
// ============================================================
void display_message(const char *msg) {
  display.setTextSize(1);
  display.setCursor(4, 28);
  display.print(msg);
}

void display_message_multi(const char *line1, const char *line2,
                           const char *line3) {
  display.setTextSize(1);

  if (line1) {
    display.setCursor(4, 18);
    display.print(line1);
  }
  if (line2) {
    display.setCursor(4, 30);
    display.print(line2);
  }
  if (line3) {
    display.setCursor(4, 42);
    display.print(line3);
  }
}

void display_message_at(int line, const char *msg) {
  display.setTextSize(1);
  // Each line is ~8 pixels, start after header at line 14
  int y = 14 + (line * 8);
  if (y > 56)
    y = 56; // Clamp to screen
  display.setCursor(4, y);
  display.print(msg);
}

// ============================================================
// MENU DISPLAY
// ============================================================
void display_menu(const char *items[], int count, int selected, int y,
                  int maxItems) {
  int startY = y;
  int itemHeight = 10;

  // Calculate visible window
  int startIndex = 0;

  if (selected >= maxItems) {
    startIndex = selected - maxItems + 1;
  }

  for (int i = 0; i < maxItems && (startIndex + i) < count; i++) {
    int itemIndex = startIndex + i;
    int currentY = startY + (i * itemHeight);

    if (itemIndex == selected) {
      // Highlight selected item
      display.fillRect(0, currentY - 1, SCREEN_WIDTH, itemHeight, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    display.setCursor(4, currentY);
    display.print(items[itemIndex]);
  }

  display.setTextColor(SH110X_WHITE);

  // Scroll indicators
  if (startIndex > 0) {
    display.setCursor(120, startY);
    display.print("^");
  }
  if (startIndex + maxItems < count) {
    int endY = startY + ((maxItems - 1) * itemHeight);
    display.setCursor(120, endY);
    display.print("v");
  }
}

// ============================================================
// PROGRESS BAR
// ============================================================
void display_progress(const char *label, int progress) {
  display.setTextSize(1);
  display.setCursor(4, 24);
  display.print(label);

  // Draw progress bar outline
  display.drawRect(4, 36, 120, 10, SH110X_WHITE);

  // Fill progress
  int fillWidth = (progress * 116) / 100;
  display.fillRect(6, 38, fillWidth, 6, SH110X_WHITE);

  // Show percentage
  display.setCursor(54, 48);
  display.print(progress);
  display.print("%");
}

// ============================================================
// BOOT SCREEN
// ============================================================
bool display_boot_screen() {
  // Setup SELECT button pin just in case it isn't initialized yet
  pinMode(25, INPUT_PULLUP); // BTN_SELECT_PIN is 25

  // Phase 1: Draw Border (animate it tracing the outline)
  for (int step = 0; step <= 20; step++) {
    if (digitalRead(25) == LOW) return true; // Interrupt check
    
    display_clear();
    
    // Draw bounding box matching the step progress
    int borderW = (step * SCREEN_WIDTH) / 20;
    int borderH = (step * SCREEN_HEIGHT) / 20;
    
    display.drawRect(0, 0, borderW, borderH, SH110X_WHITE);
    display_update();
    delay(20);
  }
  
  // Phase 2: Logo Flicker & Reveal
  // Flicker 1
  if (digitalRead(25) == LOW) return true;
  display_clear();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  display_update();
  delay(60);
  
  // Flicker 2
  if (digitalRead(25) == LOW) return true;
  display_clear();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("ATTENDIFY");
  display_update();
  delay(80);
  
  // Solid Logo
  if (digitalRead(25) == LOW) return true;
  display_clear();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("ATTENDIFY");
  display_update();
  delay(150);

  // Phase 3: The Expanding Divider Line
  int lineMaxHalfWidth = 54; // 108 pixels total (from X=10 to X=118)
  for (int w = 0; w <= lineMaxHalfWidth; w += 4) {
    if (digitalRead(25) == LOW) return true; // Interrupt check
    
    display_clear();
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.print("ATTENDIFY");
    
    // Draw expanding line from center
    display.drawFastHLine(64 - w, 28, w * 2, SH110X_WHITE);
    display_update();
    delay(20);
  }

  // Phase 4: Tagline Cascade
  const char* line1 = "Smart Portable";
  const char* line2 = "Attendance System";
  const char* line3 = "Paperless Attendance";
  
  // Slide in/Show each tagline with 250ms delay, keeping checks
  for (int phase = 1; phase <= 3; phase++) {
    if (digitalRead(25) == LOW) return true; // Interrupt check
    
    display_clear();
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.print("ATTENDIFY");
    display.drawFastHLine(10, 28, 108, SH110X_WHITE);
    
    display.setTextSize(1);
    if (phase >= 1) {
      display.setCursor(22, 34);
      display.print(line1);
    }
    if (phase >= 2) {
      display.setCursor(13, 44);
      display.print(line2);
    }
    if (phase >= 3) {
      display.setCursor(4, 54);
      display.print(line3);
    }
    
    display_update();
    delay(250);
  }

  // Phase 5: Hold static screen for 500ms
  for (int i = 0; i < 10; i++) {
    if (digitalRead(25) == LOW) return true; // Interrupt check
    delay(50);
  }

  return false; // Completed fully without interruption
}

void display_invert(bool invert) {
  display.invertDisplay(invert);
}
