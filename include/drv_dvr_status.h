// drv_dvr_status.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

// =============================================================================
// drv_dvr_status (LED → semantic DVR state bridge)
//
// Responsibility:
//   - Consumes EV_DVR_LED_PATTERN_CHANGED from event_queue
//   - Derives higher-level DVR semantic events
//   - Implements SD card error discriminator (persistent FAST_BLINK)
//
// This module does NOT:
//   - Touch GPIO
//   - Read hardware directly
//   - Re-run the LED classifier
//
// Call order in main loop (important):
//
//   drv_dvr_led_poll(now_ms);     // classifier bridge
//   drv_dvr_status_poll(now_ms);  // semantic interpreter
//
// =============================================================================
//
// Events expected from enums.h:
//
//   EV_DVR_LED_PATTERN_CHANGED  (arg0 = dvr_led_pattern_t)
//
// Events emitted (must exist in enums.h):
//
//   EV_DVR_RECORD_STARTED
//   EV_DVR_RECORD_STOPPED
//   EV_DVR_POWERED_OFF
//   EV_DVR_POWERED_ON_IDLE
//   EV_DVR_ERROR                (arg0 = error_code_t, arg1 = last LED pattern)
//
// Error codes expected:
//
//   ERR_DVR_CARD_ERROR
//
// =============================================================================

void drv_dvr_status_init(void);
void drv_dvr_status_poll(uint32_t now_ms);

// ----------------------------------------------------------------------------
// Observability (debug / FSM inspection)
// ----------------------------------------------------------------------------

// Last stable LED pattern observed (from bridge)
dvr_led_pattern_t drv_dvr_status_last_led_pattern(void);

// True if SLOW_BLINK is currently assumed (recording active)
bool drv_dvr_status_recording_assumed(void);
