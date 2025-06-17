#include "hc06_task.h"
#include "common.h"
#include "hc06.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

extern QueueHandle_t xQueueADC;
extern QueueHandle_t xQueueBTN;

void hc06C_task(void *p) {
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);
    gpio_set_function(HC06_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(HC06_RX_PIN, GPIO_FUNC_UART);
    hc06_init("ARCADESTICK", "1234");

    adc_data_t data;
    uint8_t code;
    uint8_t buffer[4];

    while (1) {
        if (xQueueReceive(xQueueBTN, &code, 0)) {
            buffer[0] = code;
            buffer[1] = 0x00;
            buffer[2] = 0x64;
            buffer[3] = 0xFF;
            uart_write_blocking(HC06_UART_ID, buffer, 4);
        } else if (xQueueReceive(xQueueADC, &data, 0)) {
            buffer[0] = data.axis;
            buffer[1] = (data.value >> 8) & 0xFF;
            buffer[2] = data.value & 0xFF;
            buffer[3] = 0xFF;
            uart_write_blocking(HC06_UART_ID, buffer, 4);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
