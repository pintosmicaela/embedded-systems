#ifndef FAN_H
#define FAN_H

void setup_fan();
void set_fan_active(bool active);
bool is_fan_active();
void update_fan_indicator(bool system_active);

#endif // FAN_H
