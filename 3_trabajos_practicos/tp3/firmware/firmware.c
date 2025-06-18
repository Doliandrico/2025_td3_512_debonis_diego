#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "helper.h"
#include "lcd.h"

// Pines y configuración
#define SIGNAL_GEN_GPIO 16
#define SIGNAL_IN_GPIO 15

#define I2C_PORT i2c0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define LCD_I2C_ADDR 0x3F

#define OUTPUT_FREQ_HZ 5000

// Variables globales
volatile uint32_t pulse_count = 0;
SemaphoreHandle_t count_mutex;

// Interrupción GPIO ISR para flanco ascendente
void gpio_irq_handler(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if ((events & GPIO_IRQ_EDGE_RISE) && gpio == SIGNAL_IN_GPIO) {
        // Incrementar contador protegido desde ISR
        pulse_count++;
    }
    gpio_acknowledge_irq(gpio, events);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Tarea para inicializar PWM (se ejecuta una vez y se elimina)
void signal_gen_task(void *pvParameters) {
    pwm_user_init(SIGNAL_GEN_GPIO, OUTPUT_FREQ_HZ);
    vTaskDelete(NULL);
}

// Tarea para mostrar frecuencia en LCD cada 1 segundo
void freq_display_task(void *pvParameters) {
    // Inicializar I2C para LCD
    i2c_init(I2C_PORT, 100000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // Inicializar LCD
    lcd_init(I2C_PORT, LCD_I2C_ADDR);

    while (1) {
        // Tomar mutex para leer y resetear pulse_count
        if (xSemaphoreTake(count_mutex, portMAX_DELAY) == pdTRUE) {
            uint32_t count = pulse_count;
            pulse_count = 0;
            xSemaphoreGive(count_mutex);

            // Formatear y mostrar en LCD
            char buf[17];
            snprintf(buf, sizeof(buf), "Freq: %lu Hz", count);

            lcd_clear();
            lcd_set_cursor(0, 0);
            lcd_string(buf);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main() {
    stdio_init_all();

    // Crear mutex
    count_mutex = xSemaphoreCreateMutex();
    if (count_mutex == NULL) {
        printf("Error creando mutex\n");
        while(1);
    }

    // Configurar GPIO entrada para señal a medir
    gpio_init(SIGNAL_IN_GPIO);
    gpio_set_dir(SIGNAL_IN_GPIO, GPIO_IN);
    gpio_pull_down(SIGNAL_IN_GPIO);

    // Configurar interrupción en flanco ascendente con callback
    gpio_set_irq_enabled_with_callback(SIGNAL_IN_GPIO, GPIO_IRQ_EDGE_RISE, true, &gpio_irq_handler);

    // Crear tarea PWM para señal de salida
    xTaskCreate(signal_gen_task, "PWMGen", configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    // Crear tarea para mostrar frecuencia
    xTaskCreate(freq_display_task, "FreqDisplay", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Iniciar scheduler FreeRTOS
    vTaskStartScheduler();

    while (1); // No debería llegar aquí
    return 0;
}
