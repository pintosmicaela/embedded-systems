#include <Arduino.h>
#include <eagle_soc.h>
#include "fan.h"

#define FAN_PIN D0
#define ACTIVE_LED_PIN D1

namespace {
bool g_fan_on = false;
uint32_t g_indicator_blink_last_ms = 0;
bool g_indicator_blink_state = false;

void set_indicator_output(bool active) {
  if (active) {
    GPOS = (1 << ACTIVE_LED_PIN);
  } else {
    GPOC = (1 << ACTIVE_LED_PIN);
  }
}

void set_fan_output(bool active) {
  if (active) {
    GP16O |= 1;
  } else {
    GP16O &= ~1;
  }
}
}

void setup_fan() {
  GPF(ACTIVE_LED_PIN) = GPFFS(GPFFS_GPIO(ACTIVE_LED_PIN));
  GPC(ACTIVE_LED_PIN) = GPC(ACTIVE_LED_PIN) & (0xF << GPCI);
  GPES = (1 << ACTIVE_LED_PIN);

  GPF16 = GP16FFS(GPFFS_GPIO(FAN_PIN));
  GPC16 = 0;
  GP16E |= 1;

  set_indicator_output(false);
  set_fan_output(false);
}

void set_fan_active(bool active) {
  if (g_fan_on == active) {
    return;
  }

  g_fan_on = active;
  set_fan_output(g_fan_on);
}

bool is_fan_active() {
  return g_fan_on;
}

void update_fan_indicator(bool system_active) {
  if (!system_active) {
    set_indicator_output(false);
    g_indicator_blink_last_ms = millis();
    g_indicator_blink_state = false;
    return;
  }

  if (!g_fan_on) {
    set_indicator_output(true);
    g_indicator_blink_last_ms = millis();
    g_indicator_blink_state = false;
    return;
  }

  uint32_t now = millis();
  if (now - g_indicator_blink_last_ms >= 200UL) {
    g_indicator_blink_last_ms = now;
    g_indicator_blink_state = !g_indicator_blink_state;
    set_indicator_output(g_indicator_blink_state);
  }
}
