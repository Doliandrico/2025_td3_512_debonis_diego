#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "bmp280.h"  // Librería del sensor BMP280 (I2C)
#include "lcd.h"     // Librería para LCD I2C

// --- Pines usados ---
#define I2C_PORT       i2c0        // Puerto I2C principal
#define I2C_SDA_PIN    4           // Pin SDA I2C
#define I2C_SCL_PIN    5           // Pin SCL I2C
#define LCD_ADDR       0x27        // Dirección I2C del LCD
#define I2C_FREQ       100000      // Frecuencia I2C (100 kHz)

#define LED_PWM_PIN    15          // Pin para LED controlado por PWM
#define BUTTON_PIN     16          // Pin de entrada con botón

// --- Parámetros del sistema ---
#define SETPOINT       24.0f       // Temperatura deseada (°C)
#define MAX_ERROR      10.0f       // Máximo error considerado para el mapeo PWM
#define PWM_WRAP       1000        // Resolución del PWM (0–1000)

// --- Estructura compartida entre tareas ---
typedef struct {
    float temperature;
    float pressure;
} sensor_data_t;

// --- Recursos de FreeRTOS ---
QueueHandle_t queue_sensor_data;  // Cola para pasar datos del sensor a la pantalla
SemaphoreHandle_t i2c_mutex;      // Mutex para uso exclusivo del bus I2C
SemaphoreHandle_t sem_button;     // Semáforo que activa el cambio de pantalla

// --- Variable global para cambiar el modo de pantalla LCD (0 o 1) ---
volatile int screen_mode = 0;

// --- Variables globales para debounce ---
TickType_t last_button_time = 0;
const TickType_t debounce_delay = pdMS_TO_TICKS(200);

// --- ISR: interrupción del botón ---
// Solo se activa al flanco de bajada. Despierta a la tarea LCD para cambiar de modo.
void button_isr(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sem_button, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// --- Inicialización del canal PWM para el LED ---
void init_pwm() {
    gpio_set_function(LED_PWM_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(LED_PWM_PIN);
    pwm_set_wrap(slice, PWM_WRAP);                // Establece el máximo valor de PWM
    pwm_set_chan_level(slice, PWM_CHAN_B, 0);     // Duty cycle inicial: apagado
    pwm_set_enabled(slice, true);                 // Habilita el canal PWM
}

// --- Inicialización del botón con interrupción y pull-up interna ---
void init_button() {
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);  // Activa resistencia pull-up interna
    gpio_set_irq_enabled_with_callback(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &button_isr);
}

// --- Tarea: lectura del sensor BMP280 ---
void vTaskSensor(void *pvParameters) {
    sensor_data_t data;
    int32_t raw_temp = 0, raw_pres = 0;
    struct bmp280_calib_param calib;

    bmp280_get_calib_params(&calib);  // Obtiene los parámetros de fábrica del sensor

    while (1) {
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bmp280_read_raw(&raw_temp, &raw_pres);  // Lecturas crudas
            data.temperature = bmp280_convert_temp(raw_temp, &calib);  // Conversión con compensación
            data.pressure = bmp280_convert_pressure(raw_pres, raw_temp, &calib) / 1000.0f;  // Pa a kPa
            xSemaphoreGive(i2c_mutex);

            xQueueSend(queue_sensor_data, &data, 0);  // Envía datos a la tarea de pantalla
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  // Espera 1 segundo antes de nueva lectura
    }
}

// --- Tarea: actualización del LCD y control de PWM del LED ---
void vTaskLCD(void *pvParameters) {
    sensor_data_t data;
    char line1[17], line2[17];
    float error, error_abs, factor = 1.0f - powf(error_abs / MAX_ERROR, 0.5f);
    uint slice = pwm_gpio_to_slice_num(LED_PWM_PIN);

    while (1) {
        // Espera indefinida a recibir datos del sensor
        if (xQueueReceive(queue_sensor_data, &data, portMAX_DELAY) == pdTRUE) {
            // Calcula el error respecto al setpoint
            error = SETPOINT - data.temperature;
            error_abs = fabsf(error);       // Valor absoluto

            // Mostrar datos en LCD según el modo actual
            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (screen_mode == 0) {
                    snprintf(line1, sizeof(line1), "Temp: %.1f %cC    ", data.temperature, '\xDF');
                    snprintf(line2, sizeof(line2), "Pres: %.1f kPa    ", data.pressure);
                } else {
                    snprintf(line1, sizeof(line1), "Set: %.1f %cC     ", SETPOINT, '\xDF');
                    snprintf(line2, sizeof(line2), "Err: %.1f %cC     ", error, '\xDF');
                }
                lcd_set_cursor(0, 0);
                lcd_string(line1);
                lcd_set_cursor(1, 0);
                lcd_string(line2);
                xSemaphoreGive(i2c_mutex);
            }

            // PWM: brillo inverso proporcional al error
            uint16_t duty = 0;
            if (error_abs < 0.1f) {
                duty = 0;   // LED apagado si el error es despreciable
            } else if (error_abs < MAX_ERROR) {
                duty = (uint16_t)(factor * PWM_WRAP);
            } else {
                duty = 0;   // Si el error es muy grande, apagar LED
            }
            pwm_set_chan_level(slice, PWM_CHAN_B, duty);
        }

        // Manejo del pulsador con debounce
        if (xSemaphoreTake(sem_button, 0) == pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_button_time) > debounce_delay) {
                screen_mode = !screen_mode;
                last_button_time = now;
            }
        }
    }
}

// --- Inicialización completa de hardware ---
void init_hardware() {
    stdio_init_all();  // Inicializa UART (debug por consola si se usa)

    // I2C: se comparte entre BMP280 y LCD
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    bmp280_init(I2C_PORT);            // Inicializa el sensor
    lcd_init(I2C_PORT, LCD_ADDR);     // Inicializa el LCD

    init_pwm();       // Configura PWM para LED
    init_button();    // Configura botón con interrupción
}

// --- Punto de entrada principal ---
int main() {
    init_hardware();  // Inicializa todos los periféricos

    // Crear recursos de FreeRTOS
    i2c_mutex = xSemaphoreCreateMutex();
    sem_button = xSemaphoreCreateBinary();
    queue_sensor_data = xQueueCreate(4, sizeof(sensor_data_t));

    // Crear las tareas principales del sistema
    xTaskCreate(vTaskSensor, "Sensor", configMINIMAL_STACK_SIZE + 100, NULL, 2, NULL);
    xTaskCreate(vTaskLCD, "LCD", configMINIMAL_STACK_SIZE + 200, NULL, 2, NULL);

    vTaskStartScheduler();  // Inicia el sistema operativo

    while (1);  // Nunca debería ejecutarse (por si falla el scheduler)
}
