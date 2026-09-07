#include <Arduino.h>
#include "led_rgb.h"

#define RED_GPIO   14  // pin D5
#define GREEN_GPIO 12  // pin D6
#define BLUE_GPIO  13  // pin D7

void setup_led() {
  const uint32_t rgb_mask = (1 << RED_GPIO) | (1 << GREEN_GPIO) | (1 << BLUE_GPIO);

  // Equivalente a pinMode(pin, OUTPUT) en ESP8266 
  GPF(RED_GPIO) = GPFFS(GPFFS_GPIO(RED_GPIO));
  GPF(GREEN_GPIO) = GPFFS(GPFFS_GPIO(GREEN_GPIO));
  GPF(BLUE_GPIO) = GPFFS(GPFFS_GPIO(BLUE_GPIO));

  GPC(RED_GPIO) = (GPC(RED_GPIO) & (0xF << GPCI));
  GPC(GREEN_GPIO) = (GPC(GREEN_GPIO) & (0xF << GPCI));
  GPC(BLUE_GPIO) = (GPC(BLUE_GPIO) & (0xF << GPCI));

  GPES = rgb_mask;

  GPOC = rgb_mask; // Apago los 3 LEDs al inicio
}

void turn_off(){
    GPOC = (1 << RED_GPIO) | (1 << GREEN_GPIO) | (1 << BLUE_GPIO);
}

void set_color(int r, int g, int b) {
  uint32_t to_set = 0;
  uint32_t to_clear = 0;

  (r > 0) ? to_set |= (1 << RED_GPIO) : to_clear |= (1 << RED_GPIO);
  (g > 0) ? to_set |= (1 << GREEN_GPIO) : to_clear |= (1 << GREEN_GPIO);
  (b > 0) ? to_set |= (1 << BLUE_GPIO) : to_clear |= (1 << BLUE_GPIO);

  GPOS = to_set;
  GPOC = to_clear;
}
