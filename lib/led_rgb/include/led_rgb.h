#ifndef LED_H
#define LED_H

#include <stdint.h>

void setup_led();
void turn_off();
void set_color(int r, int g, int b);

#endif // LED_H