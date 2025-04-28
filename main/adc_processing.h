#ifndef ADC_PROCESSING_H
#define ADC_PROCESSING_H

#include <stdint.h>

int16_t process_adc_value(uint16_t raw, uint8_t axis);
int16_t process_pot_value(uint16_t raw);

#endif // ADC_PROCESSING_H
