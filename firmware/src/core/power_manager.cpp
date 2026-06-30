/**
 * ============================================================
 *  ATTENDIFY HARDWARE - PHASE 8
 *  Power Manager - Idle + Deep Sleep Control
 * ============================================================
 */

#include "power_manager.h"

#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "../hal/display_hal.h"
#include "../hal/fingerprint_hal.h"
#include "../hal/wifi_hal.h"

static unsigned long lastInteractionMs = 0;
static int busyCounter = 0;
static bool idleScreenShown = false;
static esp_sleep_wakeup_cause_t wakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;

static bool state_allows_auto_sleep(SystemState state) {
  return state == STATE_HOME;
}

static void show_idle_screen_once() {
  if (idleScreenShown) {
    return;
  }

  display_clear();
  display_header("IDLE MODE");
  display_message_multi("Low-power standby", "Press SELECT to wake",
                        "Auto-sleep soon");
  display_update();
  fingerprint_led_off();

  idleScreenShown = true;
  Serial.println("[POWER] Idle screen shown");
}

static void enter_deep_sleep_now() {
  Serial.println("[POWER] Entering deep sleep...");

  display_clear();
  display_header("SLEEP");
  display_message_multi("Device sleeping", "Press SELECT", "to wake");
  display_update();

  fingerprint_led_off();
  wifi_disconnect();

  delay(80);

  rtc_gpio_init((gpio_num_t)POWER_WAKE_PIN);
  rtc_gpio_set_direction((gpio_num_t)POWER_WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)POWER_WAKE_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)POWER_WAKE_PIN);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)POWER_WAKE_PIN, 0);

  delay(20);
  esp_deep_sleep_start();
}

void power_init() {
  wakeCause = esp_sleep_get_wakeup_cause();
  lastInteractionMs = millis();
  busyCounter = 0;
  idleScreenShown = false;

  pinMode(POWER_WAKE_PIN, INPUT_PULLUP);

  Serial.print("[POWER] Wake cause: ");
  Serial.println(power_get_wakeup_cause_text());
}

void power_tick(SystemState currentState) {
  if (busyCounter > 0) {
    return;
  }

  if (!state_allows_auto_sleep(currentState)) {
    idleScreenShown = false;
    return;
  }

  unsigned long now = millis();
  unsigned long idleMs = now - lastInteractionMs;

  if (idleMs >= POWER_DEEP_SLEEP_TIMEOUT_MS) {
    enter_deep_sleep_now();
    return;
  }

  if (idleMs >= POWER_IDLE_TIMEOUT_MS) {
    show_idle_screen_once();
  }
}

void power_notify_interaction() {
  lastInteractionMs = millis();
  idleScreenShown = false;
}

void power_set_busy(bool busy) {
  if (busy) {
    busyCounter++;
    power_notify_interaction();
    return;
  }

  if (busyCounter > 0) {
    busyCounter--;
  }

  power_notify_interaction();
}

bool power_is_busy() { return busyCounter > 0; }

bool power_woke_from_deep_sleep() {
  return wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED;
}

const char *power_get_wakeup_cause_text() {
  switch (wakeCause) {
  case ESP_SLEEP_WAKEUP_EXT0:
    return "EXT0 (button)";
  case ESP_SLEEP_WAKEUP_EXT1:
    return "EXT1";
  case ESP_SLEEP_WAKEUP_TIMER:
    return "TIMER";
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    return "TOUCHPAD";
  case ESP_SLEEP_WAKEUP_ULP:
    return "ULP";
  case ESP_SLEEP_WAKEUP_GPIO:
    return "GPIO";
  case ESP_SLEEP_WAKEUP_UART:
    return "UART";
  case ESP_SLEEP_WAKEUP_UNDEFINED:
  default:
    return "POWER_ON_RESET";
  }
}
