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
// Current scope:
// - Button gestures: EV_BTN_SHORT_PRESS / EV_BTN_LONG_PRESS
// - Battery: EV_BAT_STATE_CHANGED, EV_BAT_LOCKOUT_ENTER/EXIT
// - Booting behaviour: discards taps while STATE_BOOTING (no buffering)
// - BOOTING -> IDLE is timeout-based for now (until DVR LED policy is integrated)
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
