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

float get_current_temperature();
float get_current_humidity();
int get_current_light();
float get_temperature_threshold();
int get_light_threshold();

void set_temperature_threshold(float value);
void set_light_threshold(int value);

#endif // WIFI_H
