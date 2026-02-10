// controller_fsm.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "enums.h"
#include "event_queue.h"
#include "action_queue.h"

// =============================================================================
// controller_fsm
//
// Top-level controller state machine.
// - Consumes events (event_queue) produced by ISR/polling modules
// - Emits actions (action_queue) consumed by executor
//
// This module does NOT directly touch hardware pins.
// =============================================================================

void controller_fsm_init(void);
void controller_fsm_reset(void);

// Feed one event into the FSM (call from main loop after eventq_pop()).
void controller_fsm_on_event(const event_t *e);

// Poll time-based guards/timeouts (call frequently from main loop).
void controller_fsm_poll(uint32_t now_ms);

// Introspection / telemetry
controller_state_t controller_fsm_state(void);
error_code_t       controller_fsm_error(void);
battery_state_t    controller_fsm_battery_state(void);
dvr_led_pattern_t  controller_fsm_dvr_led_pattern(void);
bool               controller_fsm_lockout(void);

