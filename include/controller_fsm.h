// controller_fsm.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

// =============================================================================
// controller_fsm (C)
// -----------------------------------------------------------------------------
// Minimal controller FSM:
// - consumes events from event_queue (main loop context)
// - issues actions into action_queue (main loop context)
// - drives presentation via ui_policy on state transitions
//
// Updated scope (new canonical behaviour):
// - MEGA power-up is handled by LTC hardware before firmware runs.
// - On firmware start, controller enters STATE_BOOTING and immediately boots DVR for self-test.
// - After EV_DVR_POWERED_ON_IDLE, DVR remains ON and idle (STATE_IDLE).
// - Tap toggles recording start/stop (LED-confirmed).
// - Long press requests graceful DVR shutdown (stop first if recording), then waits for EV_DVR_POWERED_OFF.
//
// Call from main loop:
//   controller_fsm_init();
//   controller_fsm_poll(millis());
//
// Optional: readbacks for debug/tests.
// =============================================================================

void controller_fsm_init(void);
void controller_fsm_poll(uint32_t now_ms);

// Readbacks (debug/tests)
controller_state_t controller_fsm_state(void);
battery_state_t    controller_fsm_battery_state(void);
bool               controller_fsm_lockout_active(void);
error_code_t       controller_fsm_error(void);
