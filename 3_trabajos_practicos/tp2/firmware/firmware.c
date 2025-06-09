#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"

#define ADC_PORT 4          //Puerto interno ADC_04 con sensor de temperatura
#define QUEUE_LENGTH 5
#define QUEUE_ITEM_SIZE sizeof(uint16_t)

//Handler de cola
QueueHandle_t T_queue;


//Tarea inicializacion ADC
void adc_init_task(void *pvParameters) {
    adc_init ();
    adc_set_temp_sensor_enabled(true);
    adc_select_input (ADC_PORT);
    vTaskDelete(NULL);
}

//Tarea de lectura
void adc_read_task(void *pvParameters) {
    while (1) {

        adc_select_input(ADC_PORT); 
        uint16_t result = adc_read();

        // Enviar el valor a la cola        
        xQueueSend(T_queue, &result, portMAX_DELAY);

        // Leer cada 1 segundo
        vTaskDelay(pdMS_TO_TICKS(1000));  
    }
} 

// Tarea mostrar dato de cola por consola
void print_task(void *pvParameters) {
    uint16_t value;
    while (1) {
        if (xQueueReceive(T_queue, &value, portMAX_DELAY) == pdPASS) {

            // Convertir a voltaje
            float voltage = value * 3.3f / (1 << 12);
            float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;

            // Imprimir
            printf("Valor ADC: %d, Temperatura: %.2f °C\n", value, temperature);
        }
    }
}


int main()
{
    stdio_init_all();

    // Creacion de la cola
    T_queue= xQueueCreate(5, sizeof(uint16_t));

    // Creacion las tareas
    xTaskCreate(adc_init_task, "Inicializacion ADC", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(adc_read_task, "Lectura ADC", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(print_task, "Imprime en consola", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while (1);
}
