#include "pot.h"
#include "common.h"
#include "hardware/adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern QueueHandle_t xQueueADC;

static int16_t process_pot_value(uint16_t raw) {
    return raw * 255 / 4095;
}

void pot_task(void *p) {
    #define WINDOW_SIZE 8

    adc_gpio_init(POT_GPIO);

    uint16_t buffer[WINDOW_SIZE] = {0};
    uint32_t sum = 0;
    int idx = 0;
    int16_t last_sent = -1;

    while (1) {
        adc_select_input(POT_ADC);
        uint16_t raw = adc_read();

        sum -= buffer[idx];
        buffer[idx] = raw;
        sum += raw;
        idx = (idx + 1) % WINDOW_SIZE;

        uint16_t avg = sum / WINDOW_SIZE;
        int16_t converted = process_pot_value(avg);

        if (abs(converted - last_sent) > 2) {
            adc_data_t data = { .axis = AXIS_POT, .value = converted };
            xQueueSend(xQueueADC, &data, portMAX_DELAY);
            last_sent = converted;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
