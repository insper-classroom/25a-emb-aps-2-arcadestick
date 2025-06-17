/*
 * Arcade Stick Bluetooth HID Integration
 * Combines arcade stick functionality with BTStack HID communication
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// HID Configuration
#define REPORT_ID 0x01
#define TYPING_KEYDOWN_MS  20
#define TYPING_DELAY_MS    20

// GPIO Configuration
#define GPIO_STATUS_LED 15
#define POT_GPIO 26
#define POT_ADC 0
#define FSR_GPIO 28
#define FSR_ADC 2

// Button Configuration
#define NUM_BUTTONS 12
static const uint8_t button_gpios[NUM_BUTTONS] = {9, 6, 7, 8, 10, 11, 12, 13, 21, 20, 19, 18};
static const uint8_t button_codes[NUM_BUTTONS] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// Axis definitions
#define AXIS_POT 0
#define AXIS_FSR 6
#define FSR_LVL1 0x06
#define FSR_LVL2 0x07
#define FSR_LVL3 0x08

// Data structures
typedef struct {
    uint8_t axis;
    int16_t value;
} adc_data_t;

typedef struct {
    uint8_t code;
    bool pressed;
} button_data_t;

// FreeRTOS Queues
QueueHandle_t xQueueADC;
QueueHandle_t xQueueBTN;

// Bluetooth HID variables
static uint8_t hid_service_buffer[300];
static uint8_t device_id_sdp_service_buffer[100];
static const char hid_device_name[] = "Arcade Stick HID";
static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint16_t hid_cid;
static uint8_t hid_boot_device = 0;

// HID Report sending
static uint8_t send_buffer_storage[32];
static btstack_ring_buffer_t send_buffer;
static btstack_timer_source_t send_timer;
static uint8_t send_modifier;
static uint8_t send_keycode;
static bool send_active;

// Connection state
static enum {
    APP_BOOTING,
    APP_NOT_CONNECTED,
    APP_CONNECTING,
    APP_CONNECTED
} app_state = APP_BOOTING;

// HID Descriptor for Gamepad/Keyboard combo
const uint8_t hid_descriptor_gamepad[] = {
    // Gamepad
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    
    // Buttons (12 buttons)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x0C,        //   Usage Maximum (0x0C)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x95, 0x0C,        //   Report Count (12)
    0x75, 0x01,        //   Report Size (1)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    
    // Padding (4 bits to complete byte)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x04,        //   Report Size (4)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    
    // Analog inputs (Pot and FSR)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //   Usage (X)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    
    0x09, 0x31,        //   Usage (Y)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    
    0xC0,              // End Collection
};

// ADC Processing Functions
static int16_t process_adc_value(uint16_t raw, uint8_t axis) {
    if (axis == AXIS_FSR) {
        if (raw < 300) return 0;
        return (raw - 300) * 255 / (4095 - 300);
    }
    
    int16_t val = ((int32_t)raw - 2048) * 255 / 2048;
    if (val < 30 && val > -30) {
        val = 0;
    }
    return val;
}

static int16_t process_pot_value(uint16_t raw) {
    return raw * 255 / 4095;
}

// LED Status Management
static void update_status_led(void) {
    if (app_state == APP_CONNECTED) {
        gpio_put(GPIO_STATUS_LED, 1);
        printf("Status LED: ON (Connected)\n");
    } else {
        gpio_put(GPIO_STATUS_LED, 0);
        printf("Status LED: OFF (Disconnected)\n");
    }
}

// HID Report Functions
static void send_gamepad_report(uint16_t buttons, uint8_t pot_value, uint8_t fsr_value) {
    if (app_state != APP_CONNECTED) return;
    
    // HID Gamepad Report: Report ID + 2 bytes buttons + pot + fsr
    uint8_t report[] = {
        0xa1,           // Input Report
        0x01,           // Report ID
        buttons & 0xFF, // Buttons 1-8
        (buttons >> 8) & 0x0F, // Buttons 9-12 + padding
        pot_value,      // Pot axis
        fsr_value       // FSR axis
    };
    
    hid_device_send_interrupt_message(hid_cid, report, sizeof(report));
}

// GPIO Initialization
static void init_gpio(void) {
    // Initialize status LED
    gpio_init(GPIO_STATUS_LED);
    gpio_set_dir(GPIO_STATUS_LED, GPIO_OUT);
    gpio_put(GPIO_STATUS_LED, 0);
    
    // Initialize buttons
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_init(button_gpios[i]);
        gpio_set_dir(button_gpios[i], GPIO_IN);
        gpio_pull_up(button_gpios[i]);
    }
    
    // Initialize ADC
    adc_init();
    adc_gpio_init(POT_GPIO);
    adc_gpio_init(FSR_GPIO);
    
    printf("GPIO initialized\n");
}

// FreeRTOS Tasks
void pot_task(void *p) {
    #define WINDOW_SIZE 8
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

void fsr_task(void *p) {
    #define WINDOW_SIZE 8
    uint16_t buffer[WINDOW_SIZE] = {0};
    uint32_t sum = 0;
    int idx = 0;
    bool pressed = false;
    uint8_t last_sent = 0;

    while (1) {
        adc_select_input(FSR_ADC);
        uint16_t raw = adc_read();

        sum -= buffer[idx];
        buffer[idx] = raw;
        sum += raw;
        idx = (idx + 1) % WINDOW_SIZE;

        uint16_t avg = sum / WINDOW_SIZE;
        int16_t converted = process_adc_value(avg, AXIS_FSR);

        if (converted > 0 && !pressed) {
            // Send FSR data as analog value
            adc_data_t data = { .axis = AXIS_FSR, .value = converted };
            xQueueSend(xQueueADC, &data, portMAX_DELAY);
            pressed = true;
        } else if (converted == 0 && pressed) {
            adc_data_t data = { .axis = AXIS_FSR, .value = 0 };
            xQueueSend(xQueueADC, &data, portMAX_DELAY);
            pressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void button_task(void *p) {
    int button_index = *(int*)p;
    uint8_t gpio = button_gpios[button_index];
    uint8_t code = button_codes[button_index];
    bool last_state = true;

    while (1) {
        bool current_state = gpio_get(gpio);

        if (last_state != current_state) {
            button_data_t btn_data;
            btn_data.code = code;
            btn_data.pressed = !current_state;
            xQueueSend(xQueueBTN, &btn_data, portMAX_DELAY);
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void hid_report_task(void *p) {
    static uint16_t button_state = 0;
    static uint8_t pot_value = 0;
    static uint8_t fsr_value = 0;
    static TickType_t last_report_time = 0;
    
    adc_data_t adc_data;
    button_data_t btn_data;
    bool state_changed = false;

    while (1) {
        // Process button events
        if (xQueueReceive(xQueueBTN, &btn_data, 0)) {
            uint16_t button_mask = 1 << (btn_data.code - 1);
            if (btn_data.pressed) {
                button_state |= button_mask;
            } else {
                button_state &= ~button_mask;
            }
            state_changed = true;
        }

        // Process ADC events
        if (xQueueReceive(xQueueADC, &adc_data, 0)) {
            if (adc_data.axis == AXIS_POT) {
                pot_value = (uint8_t)adc_data.value;
            } else if (adc_data.axis == AXIS_FSR) {
                fsr_value = (uint8_t)adc_data.value;
            }
            state_changed = true;
        }

        // Send report if state changed or periodic update
        TickType_t current_time = xTaskGetTickCount();
        if (state_changed || (current_time - last_report_time) > pdMS_TO_TICKS(100)) {
            send_gamepad_report(button_state, pot_value, fsr_value);
            last_report_time = current_time;
            state_changed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Bluetooth Event Handler
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t packet_size) {
    UNUSED(channel);
    UNUSED(packet_size);
    uint8_t status;
    
    switch (packet_type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
                    app_state = APP_NOT_CONNECTED;
                    update_status_led();
                    printf("BTstack ready. Waiting for HID connection...\n");
                    break;

                case HCI_EVENT_HID_META:
                    switch (hci_event_hid_meta_get_subevent_code(packet)) {
                        case HID_SUBEVENT_CONNECTION_OPENED:
                            status = hid_subevent_connection_opened_get_status(packet);
                            if (status != ERROR_CODE_SUCCESS) {
                                printf("Connection failed, status 0x%x\n", status);
                                app_state = APP_NOT_CONNECTED;
                                hid_cid = 0;
                                update_status_led();
                                return;
                            }
                            app_state = APP_CONNECTED;
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                            update_status_led();
                            printf("HID Connected! Arcade stick ready.\n");
                            break;
                            
                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            printf("HID Disconnected\n");
                            app_state = APP_NOT_CONNECTED;
                            hid_cid = 0;
                            update_status_led();
                            break;
                            
                        default:
                            break;
                    }
                    break;
                    
                default:
                    break;
            }
            break;
            
        default:
            break;
    }
}

// BTStack Main Function 
int btstack_main(int argc, const char * argv[]) {
    (void)argc;
    (void)argv;

    printf("Arcade Stick Bluetooth HID Starting...\n");

    // Allow discoverable
    gap_discoverable_control(1);
    gap_set_class_of_device(0x2508); // Gamepad
    gap_set_local_name("Arcade Stick HID 00:00:00:00:00:00");
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);

    // L2CAP
    l2cap_init();

    // SDP Server
    sdp_init();
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));

    // HID parameters
    uint8_t hid_virtual_cable = 0;
    uint8_t hid_remote_wake = 1;
    uint8_t hid_reconnect_initiate = 1;
    uint8_t hid_normally_connectable = 1;

    hid_sdp_record_t hid_params = {
        0x2508, 33, // Gamepad, US country code
        hid_virtual_cable, hid_remote_wake, 
        hid_reconnect_initiate, hid_normally_connectable,
        hid_boot_device,
        1600, 3200, // latency parameters
        3200,
        hid_descriptor_gamepad,
        sizeof(hid_descriptor_gamepad),
        hid_device_name
    };
    
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    sdp_register_service(hid_service_buffer);

    // Device ID
    device_id_create_sdp_record(device_id_sdp_service_buffer, sdp_create_service_record_handle(), 
                               DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    sdp_register_service(device_id_sdp_service_buffer);

    // HID Device
    hid_device_init(hid_boot_device, sizeof(hid_descriptor_gamepad), hid_descriptor_gamepad);
    
    // Register event handlers
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    hid_device_register_packet_handler(&packet_handler);

    // Initialize ring buffer
    btstack_ring_buffer_init(&send_buffer, send_buffer_storage, sizeof(send_buffer_storage));

    // Turn on Bluetooth
    hci_power_control(HCI_POWER_ON);
    
    return 0;
}

// Main Function
int main() {
    stdio_init_all();
    sleep_ms(2000); // Allow USB to initialize

    // Initialize CYW43 driver
    if (cyw43_arch_init()) {
        printf("cyw43_arch_init() failed.\n");
        return -1;
    }

    // Initialize GPIO
    init_gpio();

    // Create FreeRTOS queues
    xQueueADC = xQueueCreate(10, sizeof(adc_data_t));
    xQueueBTN = xQueueCreate(20, sizeof(button_data_t));

    // Create tasks
    xTaskCreate(pot_task, "POT", 1024, NULL, 1, NULL);
    xTaskCreate(fsr_task, "FSR", 1024, NULL, 1, NULL);
    xTaskCreate(hid_report_task, "HID_REPORT", 1024, NULL, 2, NULL);

    // Create button tasks
    static int button_indices[NUM_BUTTONS];
    for (int i = 0; i < NUM_BUTTONS; i++) {
        button_indices[i] = i;
        xTaskCreate(button_task, "BTN", 512, &button_indices[i], 1, NULL);
    }

    // Start BTStack in a separate task
    xTaskCreate((TaskFunction_t)btstack_main, "BTSTACK", 2048, NULL, 3, NULL);
    
    // Start FreeRTOS scheduler
    vTaskStartScheduler();

    // This should never be reached
    while (1) {
        tight_loop_contents();
    }

    return 0;
}