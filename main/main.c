#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "common.h"
#include "pot.h"
#include "button.h"
#include "fsr.h"
#include "bluetooth.h"

QueueHandle_t xQueueADC;
QueueHandle_t xQueueBTN;
SemaphoreHandle_t xFSRSem;

button_config_t buttons[NUM_BUTTONS] = {
    {9, 0x01}, {6, 0x02}, {7, 0x03}, {8, 0x04},
    {10, 0x05}, {11, 0x09}, {12, 0x0A}, {13, 0x0B},
    {21, 0x0C}, {20, 0x0D}, {19, 0x0E}, {18, 0x0F}
};

int main() {
    stdio_init_all();
    adc_init();

    sleep_ms(2000); // Allow USB to initialize

    // Initialize CYW43 driver
    if (cyw43_arch_init()) {
        printf("cyw43_arch_init() failed.\n");
        return -1;
    }

    // Initialize GPIO
    init_gpio();

    xQueueADC = xQueueCreate(10, sizeof(adc_data_t));
    xQueueBTN = xQueueCreate(10, sizeof(uint8_t));
    xFSRSem = xSemaphoreCreateBinary();

    xTaskCreate(pot_task, "POT", 1024, NULL, 1, NULL);
    xTaskCreate(hid_report_task, "HID_REPORT", 1024, NULL, 2, NULL);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        xTaskCreate(button_task, "BTN", 512, &buttons[i], 1, NULL);
    }

    xTaskCreate(fsr_task, "FSR_Read", 1024, NULL, 1, NULL);
    xTaskCreate(hc06C_task, "UART", 1024, NULL, 1, NULL);

    // Start BTStack in a separate task
    xTaskCreate((TaskFunction_t)btstack_main, "BTSTACK", 2048, NULL, 3, NULL);
    
    vTaskStartScheduler();

    while (1){
        tight_loop_contents();
    }
}
