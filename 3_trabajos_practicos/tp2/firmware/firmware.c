#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define ADC_PORT 4          // Puerto ADC interno con sensor de temperatura
#define QUEUE_LENGTH 5
#define QUEUE_ITEM_SIZE sizeof(uint16_t)

QueueHandle_t T_queue;

//Tarea inicializacion ADC
void adc_init_task(void *pvParameters) {
    adc_init ();
    adc_set_temp_sensor_enabled(true);
    adc_select_input (ADC_PORT);
    vTaskDelete(NULL);
}

// Variable para almacenar el resultado ADC (usada en ISR)
volatile uint16_t adc_result = 0;

// ISR del ADC
void adc_irq_handler() {
    // Leer el resultado ADC   
    adc_result = adc_fifo_get();
    uint16_t temp = adc_result;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

     // Enviar el resultado a la cola (desde ISR)
    xQueueSendFromISR(T_queue, &temp, &xHigherPriorityTaskWoken);
    // Limpiar interrupción FIFO (se hace con adc_fifo_get())
    
    // Solicitar cambio de contexto si es necesario
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

}

// Tarea que dispara la conversión ADC periódicamente
void adc_trigger_task(void *pvParameters) {
    while (1) {
        adc_select_input(ADC_PORT);
        
        // Vaciar FIFO antes de nueva lectura
        adc_fifo_drain();
        adc_fifo_setup(
            true,       // Enable FIFO
            true,       // Enable DMA data request (no usado aquí)
            1,          // Number of samples before interrupt
            false,      // No err bit
            false       // No byte shift
        );

        // Habilitar interrupciones del ADC
        adc_irq_set_enabled(true);
        
        // Habilitar la IRQ en NVIC
        irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_irq_handler);
        irq_set_enabled(ADC_IRQ_FIFO, true);

        // Iniciar la conversión de un solo canal (simple)
        adc_run(true);

        // Esperamos 1 segundo antes de nueva conversión
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Parar ADC para evitar múltiples conversiones simultáneas
        adc_run(false);

        // Deshabilitar interrupción hasta la siguiente conversión
        adc_irq_set_enabled(false);
        irq_set_enabled(ADC_IRQ_FIFO, false);
    }
}

// Tarea para imprimir valores recibidos por la cola
void print_task(void *pvParameters) {
    uint16_t value;
    while (1) {
        if (xQueueReceive(T_queue, &value, portMAX_DELAY) == pdPASS) {
            // Convertir a voltaje
            float voltage = value * 3.3f / (1 << 12);
            float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;

            printf("Valor ADC: %d, Temperatura: %.2f °C\n", value, temperature);
        }
    }
}

int main() {
    stdio_init_all();

    T_queue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);

    xTaskCreate(adc_init_task, "Inicializaicon ADC", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(adc_trigger_task, "Trigger ADC", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(print_task, "Imprime ADC", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    vTaskStartScheduler();

    while(1);
}
