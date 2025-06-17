#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "btstack.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

// HID Configuration
#define REPORT_ID 0x01
#define TYPING_KEYDOWN_MS  20
#define TYPING_DELAY_MS    20

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

// HID Keyboard Scan Codes
#define HID_KEY_A           0x04
#define HID_KEY_D           0x07
#define HID_KEY_F           0x09
#define HID_KEY_I           0x0C
#define HID_KEY_J           0x0D
#define HID_KEY_K           0x0E
#define HID_KEY_L           0x0F
#define HID_KEY_O           0x12
#define HID_KEY_S           0x16
#define HID_KEY_U           0x18
#define HID_KEY_W           0x1A
#define HID_KEY_ESC         0x29

// Estrutura para mapear botões para teclas
typedef struct {
    uint8_t button_code;
    uint8_t key_count;
    uint8_t keys[2];  // Máximo 2 teclas por botão
} button_key_map_t;

// Mapeamento conforme sua especificação
static const button_key_map_t key_mapping[] = {
    {0x01, 1, {HID_KEY_W, 0}},
    {0x02, 1, {HID_KEY_S, 0}},
    {0x03, 1, {HID_KEY_D, 0}},
    {0x04, 1, {HID_KEY_A, 0}},
    {0x05, 1, {HID_KEY_U, 0}},
    {0x06, 2, {HID_KEY_J, HID_KEY_U}},
    {0x07, 2, {HID_KEY_K, HID_KEY_I}},
    {0x08, 2, {HID_KEY_O, HID_KEY_L}},
    {0x09, 1, {HID_KEY_I, 0}},
    {0x0A, 1, {HID_KEY_J, 0}},
    {0x0B, 1, {HID_KEY_K, 0}},
    {0x0C, 1, {HID_KEY_O, 0}},
    {0x0D, 1, {HID_KEY_L, 0}},
    {0x0E, 1, {HID_KEY_ESC, 0}},
    {0x0F, 1, {HID_KEY_F, 0}}
};

// HID Descriptor for Gamepad/Keyboard combo
const uint8_t hid_descriptor_keyboard[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    
    // Modifier keys
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    
    // Reserved byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    
    // Key array (6 keys)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    
    0xC0,              // End Collection
};

// Estrutura para controlar estado das teclas
static uint8_t pressed_keys[6] = {0}; // Array de teclas pressionadas
static uint8_t key_count = 0;

// Função para encontrar teclas baseado no código do botão
static const button_key_map_t* find_key_mapping(uint8_t button_code) {
    for (int i = 0; i < sizeof(key_mapping) / sizeof(key_mapping[0]); i++) {
        if (key_mapping[i].button_code == (button_code & 0x7F)) {
            return &key_mapping[i];
        }
    }
    return NULL;
}

// Função para adicionar tecla ao array
static void add_key(uint8_t keycode) {
    if (key_count < 6) {
        // Verifica se a tecla já está pressionada
        for (int i = 0; i < key_count; i++) {
            if (pressed_keys[i] == keycode) return;
        }
        pressed_keys[key_count++] = keycode;
    }
}

// Função para remover tecla do array
static void remove_key(uint8_t keycode) {
    for (int i = 0; i < key_count; i++) {
        if (pressed_keys[i] == keycode) {
            // Move todas as teclas seguintes uma posição para trás
            for (int j = i; j < key_count - 1; j++) {
                pressed_keys[j] = pressed_keys[j + 1];
            }
            key_count--;
            pressed_keys[key_count] = 0;
            break;
        }
    }
}

// Função para enviar relatório de teclado
static void send_keyboard_report(void) {
    if (app_state != APP_CONNECTED) return;
    
    // HID Keyboard Report: Report ID + modifier + reserved + 6 keys
    uint8_t report[9] = {
        0xa1,           // Input Report
        0x01,           // Report ID
        0x00,           // Modifier keys (none)
        0x00,           // Reserved
        pressed_keys[0], pressed_keys[1], pressed_keys[2],
        pressed_keys[3], pressed_keys[4], pressed_keys[5]
    };
    
    hid_device_send_interrupt_message(hid_cid, report, sizeof(report));
}

// Substitua a função hid_report_task por esta versão
void hid_report_task(void *p) {
    static TickType_t last_report_time = 0;
    uint8_t btn_code;
    bool state_changed = false;

    while (1) {
        // Process button events
        if (xQueueReceive(xQueueBTN, &btn_code, 0)) {
            bool is_release = (btn_code & 0x80) != 0;
            uint8_t button_code = btn_code & 0x7F;
            
            const button_key_map_t* mapping = find_key_mapping(button_code);
            if (mapping) {
                if (is_release) {
                    // Soltar teclas
                    for (int i = 0; i < mapping->key_count; i++) {
                        remove_key(mapping->keys[i]);
                    }
                } else {
                    // Pressionar teclas
                    for (int i = 0; i < mapping->key_count; i++) {
                        add_key(mapping->keys[i]);
                    }
                }
                state_changed = true;
                printf("Button 0x%02X %s\n", button_code, is_release ? "released" : "pressed");
            }
        }

        // Send report if state changed or periodic update
        TickType_t current_time = xTaskGetTickCount();
        if (state_changed || (current_time - last_report_time) > pdMS_TO_TICKS(100)) {
            send_keyboard_report();
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

    printf("Arcade Stick Bluetooth HID Keyboard Starting...\n");

    // Allow discoverable
    gap_discoverable_control(1);
    gap_set_class_of_device(0x2540); // MUDOU: Keyboard em vez de Gamepad
    gap_set_local_name("Arcade Stick Keyboard 00:00:00:00:00:00"); // MUDOU: Nome
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
        0x2540, 33, // MUDOU: Keyboard, US country code
        hid_virtual_cable, hid_remote_wake, 
        hid_reconnect_initiate, hid_normally_connectable,
        hid_boot_device,
        1600, 3200,
        3200,
        hid_descriptor_keyboard,              // MUDOU: keyboard
        sizeof(hid_descriptor_keyboard),      // MUDOU: keyboard
        hid_device_name
    };
    
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    sdp_register_service(hid_service_buffer);

    // Device ID
    device_id_create_sdp_record(device_id_sdp_service_buffer, sdp_create_service_record_handle(), 
                               DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    sdp_register_service(device_id_sdp_service_buffer);

    hid_device_init(hid_boot_device, sizeof(hid_descriptor_keyboard), hid_descriptor_keyboard); // MUDOU

    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    hid_device_register_packet_handler(&packet_handler);

    // Initialize ring buffer
    btstack_ring_buffer_init(&send_buffer, send_buffer_storage, sizeof(send_buffer_storage));

    // Turn on Bluetooth
    hci_power_control(HCI_POWER_ON);
    
    return 0;
}