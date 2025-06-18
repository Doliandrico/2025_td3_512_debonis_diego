#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "helper.h"

// Configuración puertos GPIO
#define SIGNAL_GEN_GPIO 16
#define SIGNAL_IN_GPIO  15

// Configuración de frecuencia de la señal generada
#define OUTPUT_FREQ_HZ 5000  // Modificar para probar 1KHz, 5KHz, 10KHZ, etc.

// Estructura para poder pasarle los dos parametros a la tarea que usa la func pwm_user_init
typedef struct {
    uint32_t gpio;
    uint32_t freq;
} PwmParams;

// Tarea que inicializa el PWM
void signal_gen_task(void *pvParameters) {
    PwmParams *params = (PwmParams *) pvParameters;

    pwm_user_init(params->gpio, params->freq);

    // Solo inicializa una vez, luego libera y elimina la tarea
    vPortFree(params);
    vTaskDelete(NULL);
}

// Tarea que mide frecuencia de entrada
void freq_count_task(void *pvParameters) {
    gpio_init(SIGNAL_IN_GPIO);
    gpio_set_dir(SIGNAL_IN_GPIO, GPIO_IN);

    bool last_state = false;
    bool current_state;
    uint32_t count = 0;

    //Probé el sample_delay con valor pdMS_TO_TICKS(1) y luego lo cambie a 0.1 mejorando la precision
    const TickType_t sample_delay_ticks = pdMS_TO_TICKS(0.1); 
    const TickType_t measure_time_ticks = pdMS_TO_TICKS(1000);

    while (1) {
        count = 0;
        TickType_t start_time = xTaskGetTickCount();

        while ((xTaskGetTickCount() - start_time) < measure_time_ticks) {
            current_state = gpio_get(SIGNAL_IN_GPIO);

            if (!last_state && current_state) {
                count++;
            }

            last_state = current_state;

            vTaskDelay(sample_delay_ticks);
        }

        printf("Frecuencia medida: %lu Hz\n", count);
    }
}

int main() {
    stdio_init_all();

    // Crear estructura de parámetros para PWM
    PwmParams *params = pvPortMalloc(sizeof(PwmParams));
    params->gpio = SIGNAL_GEN_GPIO;
    params->freq = OUTPUT_FREQ_HZ;

    // Crear tareas
    xTaskCreate(signal_gen_task, "SignalGen", configMINIMAL_STACK_SIZE, (void *)params, 1, NULL);
    xTaskCreate(freq_count_task, "FreqCounter", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Iniciar el planificador
    vTaskStartScheduler();

    while (1) {}
    return 0;
}
