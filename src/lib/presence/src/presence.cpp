#include <Arduino.h>
#include <eagle_soc.h>
#include "presence.h"
#include "wifi.h"

#define SENSOR_PIN D2  // Pin para botón o sensor de puerta (con pull-up)
int estadoSensor = HIGH;  // HIGH = no presionado (pull-up)
const char* detectMessage = "¡Persona entró a la habitación!";
const char* clearMessage = "Botón liberado.";  // Opcional, dependiendo del uso

unsigned long tiempoUltimoCambio = 10000;
const int delayConfirmacion = 500;

void setup_presence() {
    GPF(SENSOR_PIN) = GPFFS(GPFFS_GPIO(SENSOR_PIN));
    GPEC = (1 << SENSOR_PIN);
    GPC(SENSOR_PIN) = (GPC(SENSOR_PIN) & (0xF << GPCI)) | (1 << GPCD);
    GPF(SENSOR_PIN) |= (1 << GPFPU);
}

void loop_presence() {
    int sensorValue = GPIP(SENSOR_PIN) ? HIGH : LOW;

    int triggerCondition = (sensorValue == LOW && estadoSensor == HIGH);  // Botón presionado (LOW con pull-up)
    int clearCondition = (sensorValue == HIGH && estadoSensor == LOW);   // Botón liberado

    if ((triggerCondition || clearCondition) && (millis() - tiempoUltimoCambio) > delayConfirmacion) {
        estadoSensor = sensorValue;
        tiempoUltimoCambio = millis();

        if (triggerCondition) {
            if (time_is_synced()) {
                time_t epoch = get_epoch_time();
                String timestamp = get_time_string();
                Serial.println("------------------------------------");
                Serial.printf("%s | epoch: %lu | hora: %s\n", detectMessage, static_cast<unsigned long>(epoch), timestamp.c_str());
                Serial.println("------------------------------------");
            } else {
                Serial.printf("%s (hora no sincronizada)\n", detectMessage);
            }
        } else if (clearCondition) {
            Serial.println(clearMessage);
        }
    }
}
