#include <Arduino.h>
#include "temperature.h"

//Para el GPIO 2 (D4), el registro es PERIBS_IO_MUX_GPIO2_U
#define PIN_CONFIG_REG *((volatile uint32_t *)0x60000838)

// Registros rápidos para ESP8266 (GPIO 2 / D4)
#define DHT_BIT 2
#define SET_HIGH (GPOS = (1 << DHT_BIT))
#define SET_LOW  (GPOC = (1 << DHT_BIT))
#define READ_PIN (GPI & (1 << DHT_BIT))
#define SET_OUTPUT (GPE |= (1 << DHT_BIT))
#define SET_INPUT  (GPE &= ~(1 << DHT_BIT))


void setup_dht(){
    //Configuracion del multiplexor del pin
    //bit 0-2: funcion del pin (0 es GPIO)
    //bit 7: activa el pull-up interno
    PIN_CONFIG_REG = (1<<7) | (0<<0);

    SET_HIGH;
    SET_OUTPUT;

    delay(100);

    Serial.println("DHT pin state check...");
    Serial.printf("GPIO2 raw state: %d\n", (GPI & (1 << 2)) ? 1 : 0);
    delay(1000);
}


static inline uint32_t get_cycles() {
    uint32_t c;
    asm volatile ("rsr %0, ccount" : "=a" (c));
    return c;
}

DHT_Data read_dht_raw(){
	uint8_t data[5] = {0,0,0,0,0};
    DHT_Data result = {0, 0, false};
    uint32_t pulse_widths[40];

    //pinMode(D4, OUTPUT);
    SET_OUTPUT;
    SET_LOW;
    delay(18); //El sensor necesita el puslo de arranque

    //seccion critica
    noInterrupts();
    SET_HIGH;
    SET_INPUT;

    // Esperar respuesta del sensor
    // El sensor bajará la línea y luego la subirá (~160us en total)
    uint32_t timeout = get_cycles();
    while (READ_PIN){
        if (get_cycles() - timeout > 10000) {
            Serial.println("Timeout 1: line stayed HIGH");
            interrupts();
            return result;
        }
    }

    timeout = get_cycles();
    while (!READ_PIN){
        if (get_cycles() - timeout > 20000) {
            Serial.println("Timeout 2: line stayed LOW");
            interrupts();
            return result;
        }
    }

    timeout = get_cycles();
    while (READ_PIN) {
        if (get_cycles() - timeout > 30000) {
            Serial.println("Timeout 3: line stayed HIGH");
            interrupts();
            return result;
        }
    }

    //pulseIn(40)
    for (int i=0; i< 40; i++){
        //inicio del bit
        while(!READ_PIN);
        uint32_t start = get_cycles();

        //esperando el fin del bit
        while(READ_PIN);
        pulse_widths[i] = get_cycles() - start;
    }

    interrupts(); 
    //fin de la seccion critica

    //80MHz, 1 microsegundo = 80 ciclos
    //un 0 dura ~26us (2080 ciclos), un 1 dura ~70us (5600 ciclos)
    //utilizo un umbral intermedio 50us (4000 ciclos)
    uint32_t threshold = 50 * 80;

    for (int i = 0; i < 40; i++) {
        data[i/8] <<= 1;
        if(pulse_widths[i] > threshold) {
            data[i/8] |= 1;
        }
    }

    //checksum
    if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)){
        result.humidity = data[0];
        result.temperature = data[2];
        result.success = true;
    }

    return result;
}

// En lugar de dos funciones separadas, usa una que devuelva ambos
bool check_thresholds(float t_limit, float h_limit) {
    DHT_Data raw = read_dht_raw();
    if (!raw.success) return false;

    // Comparación directa de enteros (mucho más rápida que float)
    bool temp_over = raw.temperature > (int)t_limit;
    bool hum_over = raw.humidity > (int)h_limit;

    return temp_over || hum_over;
}