// power_mgr.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

// =============================================================================
// power_mgr (LTC2954 interface)
//
// Responsibilities:
// 1) Treat LTC2954 INT# (PIN_LTC_INT_N / PD2 / INT0) as an event source:
//      - EV_LTC_INT_ASSERTED / EV_LTC_INT_DEASSERTED
// 2) Derive deterministic button semantics from INT# timing:
//      - EV_BTN_SHORT_PRESS / EV_BTN_LONG_PRESS
//    using timings in timings.h (no aliases):
//      - T_BTN_DEBOUNCE_MS
//      - T_BTN_SHORT_MIN_MS
//      - T_BTN_GRACE_MS
//      - T_BTN_NUCLEAR_MS (documented; hardware will win)
// 3) Provide deterministic control of KILL# output (PIN_KILL_N_O / PB1):
//      - power_mgr_kill_assert()
//      - power_mgr_kill_deassert()
//
// This module does NOT implement the full shutdown policy (FSM does).
// It provides "power truth" primitives and input events.
//
// Event payload conventions:
//  - EV_LTC_INT_*: arg0 = raw level (HIGH/LOW), arg1 = 0
//  - EV_BTN_SHORT_PRESS / EV_BTN_LONG_PRESS: arg0 = press_ms, arg1 = 0
// =============================================================================

void power_mgr_init(void);

// Call frequently from the main loop to emit time-based events (e.g. grace-hold).
void power_mgr_poll(uint32_t now_ms);

// KILL# control (deterministic, immediate)
void power_mgr_kill_assert(void);
void power_mgr_kill_deassert(void);
bool power_mgr_kill_is_asserted(void);

// INT# observation
bool power_mgr_int_is_asserted(void);
uint32_t power_mgr_last_press_ms(void);     // duration of last completed press (on release)
