// drv_fuel_gauge.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

// =============================================================================
// drv_fuel_gauge
// -----------------------------------------------------------------------------
// Driver-level fuel gauge:
// - Samples ADC (PIN_FUELGAUGE_ADC)
// - Classifies into battery_state_t buckets using thresholds.h
// - Applies stability filtering before emitting changes
// - Applies lockout hysteresis (enter/exit thresholds)
//
// Emits events into event_queue:
//   EV_BAT_STATE_CHANGED   (arg0=battery_state_t, arg1=adc)
//   EV_BAT_LOCKOUT_ENTER   (arg0=battery_state_t, arg1=adc)
//   EV_BAT_LOCKOUT_EXIT    (arg0=battery_state_t, arg1=adc)
// =============================================================================

void          drv_fuel_gauge_init(void);
void          drv_fuel_gauge_poll(uint32_t now_ms);

// Observability (debug / FSM inspection)
uint16_t      drv_fuel_gauge_last_adc(void);
battery_state_t drv_fuel_gauge_last_state(void);
bool          drv_fuel_gauge_lockout_active(void);
