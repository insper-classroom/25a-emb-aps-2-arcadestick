#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/adc.h"

#define NUM_BUTTONS 4
#define AXIS_X   0
#define AXIS_Y   1
#define AXIS_FSR 6

typedef struct {
    uint8_t axis;    // 0 para X, 1 para Y
    int16_t value;   // valor já filtrado e convertido (-255 a 255)
} adc_data_t;

typedef struct {
    uint gpio;
    uint8_t code;  // Código para identificar o botão
} button_config_t;

button_config_t buttons[NUM_BUTTONS] = {
    {15, 0x02}, // W
    {14, 0x03}, // S
    {17, 0x04}, // D
    {16, 0x05}  // A
};

QueueHandle_t xQueueADC;
QueueHandle_t xQueueBTN;
SemaphoreHandle_t xFSRSem;
volatile int16_t fsr_max_value = 0;

int16_t process_adc_value(uint16_t raw, uint8_t axis) {
    if (axis == AXIS_FSR) {
        if (raw < 300) return 0; // Considera "não pressionado"
        // Escala simples: 0 a 4095 -> 0 a 255
        return (raw - 300) * 255 / (4095 - 300);
    }

    int16_t val = ((int32_t)raw - 2048) * 255 / 2048;
    if (val < 30 && val > -30) {
        val = 0;
    }
    return val;
}

void adc_task(void *p) {
    #define WINDOW_SIZE 8
    uint8_t axis = (uint32_t)p;
    uint gpio = (axis == AXIS_X) ? 26 : 27;

    adc_gpio_init(gpio);

    uint16_t buffer[WINDOW_SIZE] = {0};
    uint32_t sum = 0;
    int idx = 0;

    while (1) {
        adc_select_input(axis);
        
        uint16_t raw = adc_read();

        sum -= buffer[idx];
        buffer[idx] = raw;
        sum += raw;

        idx = (idx + 1) % WINDOW_SIZE;

        uint16_t avg = sum / WINDOW_SIZE;
        int16_t converted = process_adc_value(avg, axis);

        if (converted != 0) {
            adc_data_t data = { .axis = axis, .value = converted };
            xQueueSend(xQueueADC, &data, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void button_task(void *p) {
    button_config_t btn = *(button_config_t *)p;
    gpio_init(btn.gpio);
    gpio_set_dir(btn.gpio, GPIO_IN);
    gpio_pull_up(btn.gpio);

    bool last_state = true;

    while (1) {
        bool current_state = gpio_get(btn.gpio);

        if (last_state && !current_state) {
            // Pressionado
            uint8_t code = btn.code;
            xQueueSend(xQueueBTN, &code, portMAX_DELAY);
        } else if (!last_state && current_state) {
            // Soltou
            uint8_t code = btn.code | 0x80; // Bit 7 indica "soltou"
            xQueueSend(xQueueBTN, &code, portMAX_DELAY);
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(20)); // taxa de verificação rápida
    }
}


void fsr_task(void *p) {
    #define WINDOW_SIZE 8
    uint gpio = 28;

    adc_gpio_init(gpio);

    uint16_t buffer[WINDOW_SIZE] = {0};
    uint32_t sum = 0;
    int idx = 0;

    bool pressed = false;
    int16_t max_value = 0;

    while (1) {
        adc_select_input(2); // FSR (ADC2)
        uint16_t raw = adc_read();

        sum -= buffer[idx];
        buffer[idx] = raw;
        sum += raw;
        idx = (idx + 1) % WINDOW_SIZE;

        uint16_t avg = sum / WINDOW_SIZE;
        int16_t converted = process_adc_value(avg, AXIS_FSR);

        if (converted > 0) {
            if (!pressed) {
                pressed = true;
                max_value = converted;
            } else if (converted > max_value) {
                max_value = converted;
            }
        } else if (pressed) {
            // Soltou o FSR → envia o pico
            fsr_max_value = max_value;
            xSemaphoreGive(xFSRSem);
            pressed = false;
            max_value = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void fsr_sender_task(void *p) {
    while (1) {
        if (xSemaphoreTake(xFSRSem, portMAX_DELAY)) {
            // Envia evento de "pressionado"
            adc_data_t data = {
                .axis = AXIS_FSR,
                .value = fsr_max_value
            };
            xQueueSend(xQueueADC, &data, portMAX_DELAY);

            // Espera um pouco e envia evento de "soltou"
            vTaskDelay(pdMS_TO_TICKS(50)); // pequena espera para garantir recebimento
            adc_data_t release_event = {
                .axis = AXIS_FSR,
                .value = 0x8000  // valor especial para indicar "soltou"
            };
            xQueueSend(xQueueADC, &release_event, portMAX_DELAY);
        }
    }
}

void uart_task(void *p) {
    adc_data_t data;
    uint8_t code;

    while (1) {
        // Prioridade para botões
        if (xQueueReceive(xQueueBTN, &code, 0)) {
            putchar(code);
            putchar(0x00);
            putchar(0x64);
            putchar(0xFF);
        } else if (xQueueReceive(xQueueADC, &data, 0)) {
            putchar(data.axis);
            putchar((data.value >> 8) & 0xFF);
            putchar(data.value & 0xFF);
            putchar(0xFF); // End Of Packet
        }
    }
}

int main() {
    stdio_init_all();
    adc_init();

    xQueueADC = xQueueCreate(10, sizeof(adc_data_t));
    xQueueBTN = xQueueCreate(10, sizeof(uint8_t));
    xFSRSem = xSemaphoreCreateBinary();

    xTaskCreate(adc_task, "ADC_X", 1024, (void *)0, 1, NULL);
    xTaskCreate(adc_task, "ADC_Y", 1024, (void *)1, 1, NULL);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        xTaskCreate(button_task, "BTN", 512, &buttons[i], 1, NULL);
    }

    xTaskCreate(fsr_task, "FSR_Read", 1024, NULL, 1, NULL);
    xTaskCreate(fsr_sender_task, "FSR_Send", 512, NULL, 1, NULL);
    xTaskCreate(uart_task, "UART", 1024, NULL, 1, NULL);
    vTaskStartScheduler();

    while (1);
}
