// button.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

// =============================================================================
// button (gesture decoder)
// ----------------------------------------------------------------------------
// Purpose
// - Provide a dedicated "button semantics" module that turns the LTC2954 INT#
//   behaviour into clean EV_BTN_* events.
// - Optionally also emits EV_LTC_INT_ASSERTED / EV_LTC_INT_DEASSERTED as raw
//   edge telemetry (useful for logging and debugging).
//
// This module reads PIN_LTC_INT_N directly (polling) and does NOT require ISR.
// If you already have an ISR-based INT module, you can either:
//   - stop using this module, OR
//   - keep this module and disable the ISR's EV_BTN_* generation.
//
// Emitted events:
//   - EV_LTC_INT_ASSERTED / EV_LTC_INT_DEASSERTED  (raw level transitions)
//     arg0 = raw digital level (HIGH/LOW)
//   - EV_BTN_SHORT_PRESS / EV_BTN_LONG_PRESS      (classified on release OR grace)
//     arg0 = press_ms (duration at classification time)
//
// Timing constants used (from timings.h; no aliases):
//   - T_BTN_DEBOUNCE_MS
//   - T_BTN_SHORT_MIN_MS
//   - T_BTN_GRACE_MS
//   - T_BTN_NUCLEAR_MS (commentary only; hardware-enforced)
// =============================================================================

void button_init(void);

// Call frequently from main loop.
void button_poll(uint32_t now_ms);

// Introspection
bool     button_is_pressed(void);
uint16_t button_last_press_ms(void);
