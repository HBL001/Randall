// ui_policy.h
#pragma once

#include <stdint.h>

#include "enums.h"

// =============================================================================
// ui_policy (G)
// ----------------------------------------------------------------------------
// Pure policy module: maps controller state + error + battery into actions.
// This keeps the FSM free of "presentation" logic.
//
// Updated behaviour:
// - On STATE_BOOTING entry, emit MEGA boot cue:
//     "road runner – beep beep" + LED flashes twice
// - While STATE_BOOTING, show activity (FAST blink) until DVR confirms IDLE.
// - READY (STATE_IDLE): LED solid
// - RECORDING: slow blink
//
// It enqueues ACT_LED_PATTERN / ACT_BEEP actions into the action queue.
// It does not touch GPIO.
//
// Call sites:
// - FSM on state entry: ui_policy_on_state_enter(now_ms, ...)
// - FSM on key confirmations: ui_policy_on_record_confirmed(now_ms), etc.
//
// NOTE: This module uses enums.h patterns only (no new aliases).
// =============================================================================

void ui_policy_init(void);

// State-entry mapping (pass time in from caller; do not call millis() inside policy)
void ui_policy_on_state_enter(uint32_t now_ms,
                              controller_state_t s,
                              error_code_t err,
                              battery_state_t bat);

// Momentary feedback hooks
void ui_policy_on_record_confirmed(uint32_t now_ms);
void ui_policy_on_stop_confirmed(uint32_t now_ms);
void ui_policy_on_error(uint32_t now_ms, error_code_t err);
