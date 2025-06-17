#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"

#define NUM_BUTTONS 12

// GPIO Configuration
#define GPIO_STATUS_LED 15
#define POT_GPIO 26
#define POT_ADC 0
#define FSR_GPIO 28
#define FSR_ADC 2

// Axis definitions
#define AXIS_FSR 6
#define AXIS_POT 0


#define HC06_UART_ID uart1
#define HC06_BAUD_RATE 9600
#define HC06_TX_PIN 4
#define HC06_RX_PIN 5

typedef struct {
    uint8_t axis;
    int16_t value;
} adc_data_t;

typedef struct {
    uint gpio;
    uint8_t code;
} button_config_t;

typedef struct {
    uint8_t code;
    bool pressed;
} button_data_t;

#endif
