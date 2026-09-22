#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>
#include <time.h>

void setup_wifi();
void loop_wifi_server();
void status_wifi();
void sync_hour();
bool time_is_synced();
time_t get_epoch_time();
String get_time_string();

/* [Acoplamiento e inversión de dependencias]:
 * ¿Por qué las declaraciones de estado global del sistema (sensores y umbrales) están en 'wifi.h'?
 * Esto compromete la modularidad del código. El módulo 'wifi' debería ocuparse
 * únicamente de conectividad y transporte HTTP. Los getters/setters del estado del sistema
 * deberían residir en un módulo propio (ej. 'system_state.h' o 'app_config.h') hacia el cual
 * dependan 'main' y 'wifi', evitando el acoplamiento cruzado. */
float get_current_temperature();
float get_current_humidity();
int get_current_light();
float get_temperature_threshold();
int get_light_threshold();

void set_temperature_threshold(float value);
void set_light_threshold(int value);

#endif // WIFI_H
