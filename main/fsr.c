#include "fsr.h"
#include "common.h"
#include "hardware/adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern QueueHandle_t xQueueBTN;

static int16_t process_adc_value(uint16_t raw, uint8_t axis) {
    if (axis == AXIS_FSR) {
        if (raw < 300) return 0;
        return (raw - 300) * 255 / (4095 - 300);
    }
    int16_t val = ((int32_t)raw - 2048) * 255 / 2048;
    if (val < 30 && val > -30) val = 0;
    return val;
}

void fsr_task(void *p) {
    #define WINDOW_SIZE 8
    const uint gpio = 28;
    const uint8_t FSR_LVL1 = 0x06;
    const uint8_t FSR_LVL2 = 0x07;
    const uint8_t FSR_LVL3 = 0x08;

    adc_gpio_init(gpio);

    uint16_t buffer[WINDOW_SIZE] = {0};
    uint32_t sum = 0;
    int idx = 0;
    bool pressed = false;
    uint8_t last_sent = 0;

    while (1) {
        adc_select_input(2);
        uint16_t raw = adc_read();

        sum -= buffer[idx];
        buffer[idx] = raw;
        sum += raw;
        idx = (idx + 1) % WINDOW_SIZE;

        uint16_t avg = sum / WINDOW_SIZE;
        int16_t converted = process_adc_value(avg, AXIS_FSR);

        if (converted > 0 && !pressed) {
            vTaskDelay(pdMS_TO_TICKS(150));
            adc_select_input(2);
            raw = adc_read();

            sum -= buffer[idx];
            buffer[idx] = raw;
            sum += raw;
            idx = (idx + 1) % WINDOW_SIZE;

            avg = sum / WINDOW_SIZE;
            converted = process_adc_value(avg, AXIS_FSR);

            uint8_t current_code;
            if (converted < 0x1D)
                current_code = FSR_LVL1;
            else if (converted < 0x30)
                current_code = FSR_LVL2;
            else
                current_code = FSR_LVL3;

            xQueueSend(xQueueBTN, &current_code, portMAX_DELAY);
            pressed = true;
            last_sent = current_code;
        } else if (converted == 0 && pressed) {
            uint8_t release_code = last_sent | 0x80;
            xQueueSend(xQueueBTN, &release_code, portMAX_DELAY);
            pressed = false;
            last_sent = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
