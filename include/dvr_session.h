// dvr_session.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

// =============================================================================
// dvr_session (E + F)
// ----------------------------------------------------------------------------
// E) DVR wake/orchestration
// F) Start/stop confirmation logic
//
// This module sits between:
// - FSM/user intent (power on / start record / stop record / power off)
// - Actuator layer (executor via action_queue) for press waveforms
// - LED classifier (dvr_led module via EV_DVR_LED_PATTERN_CHANGED)
//
// It does NOT read pins directly.
// It consumes LED pattern updates and uses timing constants from timings.h.
//
// High-level behaviour:
// - Boot: long press + wait for stable LED -> IDLE (SOLID) or RECORDING (SLOW)
// - Start record: short press + wait for SLOW_BLINK
// - Stop record: short press + wait for SOLID
// - Power off: long press (no LED confirmation assumed)
//
// API returns result_t so the FSM can act deterministically without inventing
// new event IDs.
// =============================================================================

void dvr_session_init(void);

// Feed LED pattern update (call from main loop when you pop EV_DVR_LED_PATTERN_CHANGED)
void dvr_session_on_led(uint32_t now_ms, dvr_led_pattern_t p);

// Poll for timeouts and to advance internal waits (call frequently)
void dvr_session_poll(uint32_t now_ms);

// Requests from FSM / top-level logic
result_t dvr_session_request_power_on(uint32_t now_ms, bool request_auto_record);
result_t dvr_session_request_start_record(uint32_t now_ms);
result_t dvr_session_request_stop_record(uint32_t now_ms);
result_t dvr_session_request_power_off(uint32_t now_ms);

// Introspection
dvr_led_pattern_t dvr_session_last_led(void);
bool              dvr_session_is_recording(void);
bool              dvr_session_is_idle(void);
bool              dvr_session_is_off(void);
bool              dvr_session_is_busy(void);
error_code_t       dvr_session_last_error(void);
