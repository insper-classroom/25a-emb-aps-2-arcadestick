#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"

#define NUM_BUTTONS 12

#define AXIS_POT 0
#define POT_GPIO 26
#define POT_ADC  0

#define AXIS_FSR 6

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

#endif
