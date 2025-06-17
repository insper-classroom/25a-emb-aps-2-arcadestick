#include "button.h"
#include "common.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern QueueHandle_t xQueueBTN;

void button_task(void *p) {
    button_config_t btn = *(button_config_t *)p;
    gpio_init(btn.gpio);
    gpio_set_dir(btn.gpio, GPIO_IN);
    gpio_pull_up(btn.gpio);

    bool last_state = true;

    while (1) {
        bool current_state = gpio_get(btn.gpio);

        if (last_state && !current_state) {
            // Botão pressionado
            uint8_t code = btn.code;
            xQueueSend(xQueueBTN, &code, portMAX_DELAY);
        } else if (!last_state && current_state) {
            // Botão solto
            uint8_t code = btn.code | 0x80;
            xQueueSend(xQueueBTN, &code, portMAX_DELAY);
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
