#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include "wifi.h"
#include "fan.h"

/* [Variables sin uso (Dead code)]:
 * Se definen estas direcciones IP estáticas pero nunca se pasan a 'WiFi.config(...)'.
 * Por lo tanto, el microcontrolador siempre solicita configuración por DHCP y estas
 * variables no tienen efecto en la ejecución. */
IPAddress ip(192, 168, 1, 100);    
IPAddress gateway(192, 168, 1, 1);  
IPAddress subnet(255, 255, 255, 0);

namespace {
const long gmtOffset_sec = -3 * 3600;
const int daylightOffset_sec = 0;
const char* ntpServer = "pool.ntp.org";
const time_t MIN_VALID_EPOCH = 8 * 3600 * 2;
ESP8266WebServer server(80);

/* [Gestión de memoria y fragmentación de heap]:
 * En microcontroladores con recursos limitados (~40KB de RAM libre en ESP8266),
 * el uso intensivo de la clase 'String' con continuas concatenaciones y reasignaciones
 * dinámicas (heap) en el procesamiento HTTP y JSON termina fragmentando la memoria.
 * Es una práctica desaconsejada para sistemas embebidos continuos 24/7; se recomienda
 * el uso de buffers estáticos (char[]) o librerías como ArduinoJson con buffer fijo. */
String urlDecode(const String& input) {
    String decoded;
    decoded.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '+') {
            decoded += ' ';
        } else if (input[i] == '%' && i + 2 < input.length()) {
            char hex[3] = { input[i + 1], input[i + 2], '\0' };
            char* end = nullptr;
            long value = strtol(hex, &end, 16);
            if (end != nullptr && *end == '\0') {
                decoded += static_cast<char>(value);
                i += 2;
            } else {
                decoded += input[i];
            }
        } else {
            decoded += input[i];
        }
    }
    return decoded;
}

void applyParameter(const String& key, const String& value) {
    const String normalizedKey = key;
    if (normalizedKey == "tempThreshold" || normalizedKey == "temperatureThreshold") {
        set_temperature_threshold(value.toFloat());
    } else if (normalizedKey == "lightThreshold") {
        set_light_threshold(value.toInt());
    } else if (normalizedKey == "fan") {
        set_fan_active(value.toInt() != 0 || value.equalsIgnoreCase("true"));
    }
}

void parseRequestParameters() {
    if (server.args() > 0) {
        for (int i = 0; i < server.args(); ++i) {
            applyParameter(server.argName(i), urlDecode(server.arg(i)));
        }
        return;
    }

    String body = server.arg("plain");
    if (body.length() == 0) {
        return;
    }

    int start = 0;
    while (start <= body.length()) {
        const int pos = body.indexOf('&', start);
        String item = (pos < 0) ? body.substring(start) : body.substring(start, pos);
        if (item.length() > 0) {
            const int equalPos = item.indexOf('=');
            String key = (equalPos >= 0) ? item.substring(0, equalPos) : item;
            String val = (equalPos >= 0) ? item.substring(equalPos + 1) : "";
            applyParameter(urlDecode(key), urlDecode(val));
        }
        if (pos < 0) {
            break;
        }
        start = pos + 1;
    }
}

String buildStatusJson() {
    String json = "{";
    json += "\"temperature\":" + String(get_current_temperature(), 1) + ",";
    json += "\"humidity\":" + String(get_current_humidity(), 1) + ",";
    json += "\"light\":" + String(get_current_light()) + ",";
    json += "\"tempThreshold\":" + String(get_temperature_threshold(), 1) + ",";
    json += "\"lightThreshold\":" + String(get_light_threshold()) + ",";
    json += "\"fan\":" + String(is_fan_active() ? "true" : "false");
    json += "}";
    return json;
}

void handleRoot() {
    server.send(200, "text/plain", "ESP8266 control ready. Use /status for readings and /set?lightThreshold=250");
}

void handleStatus() {
    server.send(200, "application/json", buildStatusJson());
}

void handleSet() {
    parseRequestParameters();
    server.send(200, "application/json", buildStatusJson());
}

void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}
}

/* [Bloqueo potencial del sistema]:
 * 1. Bucle bloqueante en setup():
 *    Si el router está apagado, la señal es débil o las credenciales fallan,
 *    el sistema queda en este 'while' dentro de setup().
 *    Consecuencia: no se inicia loop(), por lo que el control del ventilador,
 *    la lectura de temperatura, la luz y el interruptor físico quedan inoperativos.
 *    En un sistema embebido, la conectividad suele desacoplarse para no bloquear el control local.
 * 2. Credenciales en código fuente:
 *    Credenciales de demostración ("NOMBREDEWIFI", "CONTRASEÑA") en el código fuente
 *    sin provisión de WiFiManager, archivo de configuración ni almacenamiento en NVS/EEPROM. */
void connectWifi(const char* ssid, const char* password) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }

    Serial.println("Connected to WiFi");
    Serial.println(WiFi.localIP());
}

const char* wl_status_to_string(wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD: return "WL_NO_SHIELD";
        case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
        case WL_CONNECTED: return "WL_CONNECTED";
        case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED: return "WL_DISCONNECTED";
        default: return "UNKNOWN_STATUS";
    }
}

/* [Sincronización NTP no utilizada]:
 * Sincroniza hora vía NTP demorando el arranque hasta 10 segundos adicionales (20 x 500ms).
 * Sin embargo, ni 'get_epoch_time()' ni 'get_time_string()' son consumidos en ningún punto
 * de la aplicación (ni en el endpoint /status, ni en logs, ni en reglas de horario). */
void sync_hour() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.print("Synchronizing time with NTP server");

    uint8_t retries = 0;
    time_t now = time(nullptr);
    while (now < MIN_VALID_EPOCH && retries < 20) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        retries++;
    }

    if (now < MIN_VALID_EPOCH) {
        Serial.println("Error: Sync failed, time is not valid");
    }
}

bool time_is_synced() {
    return time(nullptr) >= MIN_VALID_EPOCH;
}

time_t get_epoch_time() {
    return time(nullptr);
}

String get_time_string() {
    time_t now = time(nullptr);
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);

    char buffer[24];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return String(buffer);
}

void setup_wifi() {
    Serial.begin(115200);
    connectWifi("NOMBREDEWIFI", "CONTRASEÑA");
    sync_hour();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/set", HTTP_GET, handleSet);
    server.on("/set", HTTP_POST, handleSet);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server listening on port 80");
}

void loop_wifi_server() {
    server.handleClient();
}

void status_wifi(){
    Serial.println(wl_status_to_string(WiFi.status()));
}
