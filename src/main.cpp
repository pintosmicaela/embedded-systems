#include <Arduino.h>
#include <eagle_soc.h>
#include <user_interface.h>
#include "wifi.h"
#include "temperature.h"
#include "led_rgb.h"
#include "fan.h"

#define TEMPERATURE_THRESHOLD 20.0f
#define LIGHT_FULL_ON_THRESHOLD 200

/* =========================================================================================
 * [Planificador]:
 * 1. Concepto de "Round Robin con Quantum":
 *    Round Robin con Quantum es un algoritmo de planificación preventiva (preemptive).
 *    Requiere un temporizador por hardware (tick) y cambio de contexto (guardado de registros
 *    y pilas independientes por tarea).
 *    Aquí no existe preemption ni FreeRTOS: cada función corre sincrónicamente hasta retornar
 *    (run-to-completion). Definir un "SCHED_QUANTUM_MS" de 100ms aquí no limita el tiempo de CPU
 *    de una tarea; actúa como un freno artificial que congela el despachador durante 100ms.
 *
 * 2. Inanición (starvation) y latencia:
 *    El planificador solo evalúa una tarea por cada quantum de 100ms. Al haber 5 tareas en el
 *    vector, una vuelta completa toma al menos: 5 tareas * 100ms = 500ms.
 *    La tarea "switch" está configurada con interval_ms = 50ms, pero es matemáticamente imposible
 *    que se ejecute a 50ms: solo es visitada cada 500ms en el mejor de los casos. Si un usuario
 *    presiona y suelta el botón en menos de medio segundo, la pulsación se pierde por completo.
 *
 * 3. Alternativa recomendada en Arduino sin RTOS:
 *    Implementar un "Cyclic Executive" (Superloop cooperativo sin demoras artificiales):
 *    evaluar en cada iteración de loop() todas las tareas contra millis() sin condicionar
 *    la inspección a un quantum global de 100ms.
 * ========================================================================================= */
#define SCHED_QUANTUM_MS 100UL

/* [Hardware / Pin de arranque (Boot strapping)]:
 * D3 en ESP8266 corresponde al GPIO 0.
 * GPIO 0 es un pin de arranque crítico (strapping pin): si está en nivel LOW al energizar
 * o reiniciar la placa, el ESP8266 entra en modo bootloader UART (flasheo) y no inicia el firmware.
 * Conectar un switch externo a masa en este pin sin aislamiento puede impedir el arranque normal. */
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

/* [Discrepancia en definición de tareas]:
 * Se define 'task_read_humidity' más abajo, pero no está en este array.
 * La lectura de humedad ocurre incidentalmente dentro de 'task_read_temperature'. */
Task tasks[] = {
  {"temp_alert", task_check_temperature_alert, 1000, 0, false},
  {"light_ctrl", task_control_light, 500, 0, false},
  {"read_temp", task_read_temperature, 1000, 0, false},
  {"read_light", task_read_light, 2000, 0, false},
  {"switch", task_read_switch, 50, 0, false}, // Inalcanzable a 50ms: se visita cada >= 500ms
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

/* [Despachador Round Robin]:
 * 1. Cada iteración solo avanza de tarea si han pasado SCHED_QUANTUM_MS (100ms).
 * 2. Si la tarea apuntada no está 'due', simplemente pasa el turno y se pierden otros 100ms.
 * 3. 'update_fan_indicator' se ejecuta aquí fuera de la tabla de tareas, acoplándolo
 *    arbitrariamente al ciclo del dispatcher en vez de tener su propia tarea periódica. */
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

/* [Código redundante]:
 * 'task_read_humidity' es un wrapper redundante de 'task_read_temperature'.
 * Además, nunca fue incorporada al array 'tasks[]', por lo que jamás se invoca. */
void task_read_humidity() {
  task_read_temperature();
}

void task_read_light() {
  g_light = system_adc_read();
}

/* [Lazo de control y conflicto con API HTTP]:
 * 1. Falta de histéresis: No hay margen de conmutación térmico (banda muerta).
 *    Si la temperatura oscila cerca de g_temp_threshold (ej. 19.9 y 20.0°C),
 *    el ventilador conmutará rápidamente dañando el actuador o generando ruido continuo.
 * 2. Conflicto con la API web:
 *    El endpoint HTTP '/set?fan=true' permite forzar el ventilador remotamente,
 *    pero en el siguiente ciclo de esta tarea, 'set_fan_active' sobreescribe
 *    inmediatamente el valor según la temperatura local, ignorando el comando remoto. */
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

/* [Inestabilidad por retroalimentación óptica]:
 * Si el sensor LDR comparte el mismo ambiente que la iluminación que comanda,
 * al encender la luz (light_on) el valor leído por el ADC superará 'g_light_threshold',
 * haciendo que en el siguiente ciclo se apague (light_off), generando un parpadeo (flicker)
 * oscilatorio indefinido por falta de histéresis y desacople espacial/temporal. */
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

/* [Ausencia de antirrebote (debounce)]:
 * 1. No existe filtrado de rebotes (debounce) ni por software (temporizado) ni por hardware.
 * 2. La lectura directa de 'GPIP(SWITCH_PIN)' se evalúa sobre una sola muestra cada >= 500ms.
 *    Esto hace al sistema sumamente lento ante pulsaciones del usuario y susceptible
 *    a lecturas inestables si coincide con el rebote del contacto mecánico. */
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

/* [Manipulación de registros vs. framework Arduino]:
 * Se configuran registros a bajo nivel (GPF, GPEC, GPC) para inicializar un simple pin de entrada,
 * bajo la premisa de "optimización de rendimiento". Sin embargo, esta optimización de microsegundos
 * en setup() pierde sentido cuando en el resto del proyecto se usan Strings dinámicos,
 * delays bloqueantes y lecturas lentas por polling. */
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

/* [Interacción síncrona en loop]:
 * 'loop_wifi_server' atiende peticiones HTTP de forma síncrona. Si arriba una petición
 * pesada o lenta, demora la llamada a 'run_round_robin'.
 * A su vez, si una tarea dentro de 'run_round_robin' tarda (como la lectura DHT),
 * la atención de los clientes web se posterga. */
void loop() {
  loop_wifi_server();
  run_round_robin();
}