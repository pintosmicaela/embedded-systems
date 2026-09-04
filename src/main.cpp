#include <Arduino.h>
#include <eagle_soc.h>
#include <user_interface.h>
#include "wifi.h"
#include "temperature.h"
#include "led_rgb.h"
#include "fan.h"

#define TEMPERATURE_THRESHOLD 20.0f
#define LIGHT_FULL_ON_THRESHOLD 200
#define SCHED_QUANTUM_MS 100UL

#define SWITCH_PIN D3

static float g_temperature = -1.0f;
static float g_humidity = -1.0f;
static int g_light = 0;
static float g_temp_threshold = TEMPERATURE_THRESHOLD;
static int g_light_threshold = LIGHT_FULL_ON_THRESHOLD;
static bool g_switch_active = false;

void task_read_temperature();
void task_read_humidity();
void task_read_light();
void task_check_temperature_alert();
void task_control_light();
void task_read_switch();

struct Task {
  const char* name;
  void (*run)();
  uint32_t interval_ms;
  uint32_t last_run_ms;
  bool has_run;
};

Task tasks[] = {
  {"temp_alert", task_check_temperature_alert, 1000, 0, false},
  {"light_ctrl", task_control_light, 500, 0, false},
  {"read_temp", task_read_temperature, 1000, 0, false},
  {"read_light", task_read_light, 2000, 0, false},
  {"switch", task_read_switch, 50, 0, false},
};

constexpr size_t TASK_COUNT = sizeof(tasks) / sizeof(tasks[0]);
static uint8_t next_task_index = 0;
static uint32_t g_last_quantum_ms = 0;

void light_on() {
  set_color(255, 255, 255);
}

void light_off() {
  turn_off();
}

bool task_due(const Task& task, uint32_t now_ms) {
  return !task.has_run || (now_ms - task.last_run_ms >= task.interval_ms);
}

void run_round_robin() {
  uint32_t now_ms = millis();

  if (now_ms - g_last_quantum_ms < SCHED_QUANTUM_MS) {
    return;
  }

  g_last_quantum_ms = now_ms;

  Task& task = tasks[next_task_index];
  if (task_due(task, now_ms)) {
    task.run();
    task.last_run_ms = now_ms;
    task.has_run = true;
  }

  update_fan_indicator(g_switch_active);
  next_task_index = (next_task_index + 1) % TASK_COUNT;
}

void task_read_temperature() {
  DHT_Data data = read_dht_raw();
  if (data.success) {
    g_temperature = static_cast<float>(data.temperature);
    g_humidity = static_cast<float>(data.humidity);
    Serial.print("Temperature: ");
    Serial.print(g_temperature);
    Serial.print("°C. Humidity: ");
    Serial.println(g_humidity);
  } else {
    Serial.println("DHT read failed");
  }
}

void task_read_humidity() {
  task_read_temperature();
}

void task_read_light() {
  g_light = system_adc_read();
}

void task_check_temperature_alert() {
  bool fan_should_run = false;

  if (g_switch_active && g_temperature >= 0.0f) {
    fan_should_run = g_temperature >= g_temp_threshold;
  }

  set_fan_active(fan_should_run);

  if (fan_should_run) {
    Serial.println("Fan on...");
  } else if (g_switch_active) {
    Serial.println("Fan off...");
  }
}

void task_control_light() {
  if (!g_switch_active) {
    light_off();
    return;
  }

  if (g_light > g_light_threshold) {
    light_off();
    return;
  }

  light_on();
}

void task_read_switch() {
  static bool last_state = HIGH;
  bool current_state = GPIP(SWITCH_PIN);

  if (last_state == HIGH && current_state == LOW) {
    g_switch_active = !g_switch_active;

    if (!g_switch_active) {
      light_off();
      set_fan_active(false);
      Serial.println("Switch inactivo");
      Serial.println("Fan off...");
    } else {
      Serial.println("Switch activo");
    }

    update_fan_indicator(g_switch_active);
  }

  last_state = current_state;
}

float get_current_temperature() {
  return g_temperature;
}

float get_current_humidity() {
  return g_humidity;
}

int get_current_light() {
  return g_light;
}

float get_temperature_threshold() {
  return g_temp_threshold;
}

int get_light_threshold() {
  return g_light_threshold;
}

void set_temperature_threshold(float value) {
  g_temp_threshold = value;
}

void set_light_threshold(int value) {
  g_light_threshold = constrain(value, 0, 1023);
}

void setup() {
  Serial.begin(115200);

  GPF(SWITCH_PIN) = GPFFS(GPFFS_GPIO(SWITCH_PIN));
  GPEC = (1 << SWITCH_PIN);
  GPC(SWITCH_PIN) = (GPC(SWITCH_PIN) & (0xF << GPCI)) | (1 << GPCD);
  GPF(SWITCH_PIN) |= (1 << GPFPU);

  setup_fan();
  update_fan_indicator(g_switch_active);

  setup_wifi();
  setup_dht();
  setup_led();
}

void loop() {
  loop_wifi_server();
  run_round_robin();
}