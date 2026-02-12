// drv_fuel_gauge.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

void drv_fuel_gauge_init(void);
void drv_fuel_gauge_poll(uint32_t now_ms);
uint16_t drv_fuel_gauge_last_adc(void);
battery_state_t drv_fuel_gauge_last_state(void);
bool drv_fuel_gauge_lockout_active(void);