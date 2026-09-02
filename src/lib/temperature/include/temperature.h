#ifndef TEMPERATURE_H
#define TEMPERATURE_H

struct DHT_Data {
    uint8_t humidity;
    uint8_t temperature;
    bool success;
};

void setup_dht();
bool check_thresholds(float t_limit, float h_limit);
DHT_Data read_dht_raw();

#endif 

