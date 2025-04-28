#include "adc_processing.h"
#include "config.h"

int16_t process_adc_value(uint16_t raw, uint8_t axis) {
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

int16_t process_pot_value(uint16_t raw) {
    return raw * 255 / 4095;
}
