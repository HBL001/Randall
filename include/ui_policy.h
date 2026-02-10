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
// It enqueues ACT_LED_PATTERN / ACT_BEEP actions into the action queue.
// It does not touch GPIO.
//
// Call sites:
// - FSM on state entry: ui_policy_on_state_enter(...)
// - FSM on key confirmations: ui_policy_on_record_confirmed(), etc.
//
// NOTE: This module uses enums.h patterns only (no new aliases).
// =============================================================================

void ui_policy_init(void);

// State-entry mapping
void ui_policy_on_state_enter(controller_state_t s,
                              error_code_t err,
                              battery_state_t bat);

// Momentary feedback hooks
void ui_policy_on_record_confirmed(void);
void ui_policy_on_stop_confirmed(void);
void ui_policy_on_error(error_code_t err);
