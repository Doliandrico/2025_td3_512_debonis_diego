#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "bmp280.h"
#include "lcd.h"

// Configuración del I2C del LCD utilizado
#define I2C_PORT       i2c0
#define I2C_SDA_PIN    4
#define I2C_SCL_PIN    5
#define LCD_ADDR       0x27     // Dirección de 7 bits
#define I2C_FREQ       100000   // Frecuencia de comunicación de 100 khz

// Tipo de datos compartido entre tareas
typedef struct {
    float temperature;
    float pressure;
} sensor_data_t;

// Recursos RTOS
QueueHandle_t queue_sensor_data;
SemaphoreHandle_t i2c_mutex;

// --- Tarea: lectura del sensor BMP280 ---
void vTaskSensor(void *pvParameters) {
    sensor_data_t data;
    int32_t raw_temp = 0, raw_pres = 0;
    struct bmp280_calib_param calib;

    bmp280_get_calib_params(&calib);  // Cargar parámetros de fábrica del sensor

    while (1) {
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {  // Tomar Mutex con TimeOut_ticks de 100 ms
            bmp280_read_raw(&raw_temp, &raw_pres);  // Lectura directa del sensor
            data.temperature = bmp280_convert_temp(raw_temp, &calib);  // Compensación temperatura
            data.pressure = bmp280_convert_pressure(raw_pres, raw_temp, &calib) / 1000.0f;  // Pa → kPa
            xSemaphoreGive(i2c_mutex);  // Entregar semáforo

            xQueueSend(queue_sensor_data, &data, 0);  // Enviar a la cola para el LCD
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  // Esperar 1 segundo entre lecturas
    }
}

// --- Tarea: visualización en el LCD ---
void vTaskLCD(void *pvParameters) {
    sensor_data_t data;
    char line1[17], line2[17];

    while (1) {
        if (xQueueReceive(queue_sensor_data, &data, portMAX_DELAY) == pdTRUE) {
            snprintf(line1, sizeof(line1), "Temp: %.2f C", data.temperature);
            snprintf(line2, sizeof(line2), "Pres: %.2f kPa", data.pressure);

            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                //lcd_clear();      //Lo eliminé para eviar el parpadeo de la pantalla
                lcd_set_cursor(0, 0);
                lcd_string(line1);
                lcd_set_cursor(1, 0);
                lcd_string(line2);
                xSemaphoreGive(i2c_mutex);
            }
        }
    }
}

// --- Inicialización del hardware ---
void init_hardware() {
    stdio_init_all();

    // Configuración de I2C físico (las funciones internas ya usan este puerto)
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // Inicializar dispositivos (las funciones internas guardan el puntero I2C)
    bmp280_init(I2C_PORT);
    lcd_init(I2C_PORT, LCD_ADDR);
}

// --- Función principal ---
int main() {
    init_hardware();  // Setup inicial de periféricos

    // Crear recursos del sistema operativo
    i2c_mutex = xSemaphoreCreateMutex();
    queue_sensor_data = xQueueCreate(4, sizeof(sensor_data_t));

    // Crear tareas principales
    xTaskCreate(vTaskSensor, "Sensor", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(vTaskLCD, "LCD", configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    // Iniciar el planificador de FreeRTOS
    vTaskStartScheduler();

    // Nunca debería llegar aquí
    while (true) {}
}
