// fuel_gauge.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

// Fuel gauge driver
// - Polling (main loop) ADC read on PIN_FUELGAUGE_ADC
// - Classifies raw ADC counts into battery_state_t using thresholds.h
// - Emits events into event_queue:
//     EV_BAT_STATE_CHANGED (arg0=battery_state_t)
//     EV_BAT_LOCKOUT_ENTER / EV_BAT_LOCKOUT_EXIT
//
// This module does not own policy (what FSM does with BAT_CRITICAL etc).
// It only reports observations deterministically.

void fuel_gauge_init(void);

// Call frequently from the main loop.
void fuel_gauge_poll(uint32_t now_ms);

// Optional: read last sampled raw ADC (0..1023)
uint16_t fuel_gauge_last_adc(void);

// Optional: read last reported battery_state_t
battery_state_t fuel_gauge_last_state(void);

// Optional: lockout latch computed from ADC_LOCKOUT_ENTER/EXIT hysteresis
bool fuel_gauge_lockout_active(void);
