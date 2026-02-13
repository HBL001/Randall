// drv_dvr_led.h
#pragma once

#include <stdint.h>
#include "enums.h"

// =============================================================================
// drv_dvr_led (driver / bridge)
// -----------------------------------------------------------------------------
// Purpose:
//   Bridge the low-level DVR LED classifier (dvr_led.*) into the system event_queue.
//
// Behaviour:
//   - Must be POLLED from the main loop.
//   - Internally calls dvr_led_poll(now_ms) to keep the classifier alive.
//   - Emits EV_DVR_LED_PATTERN_CHANGED when the *stable* classified pattern changes.
//
// Event contract:
//   EV_DVR_LED_PATTERN_CHANGED
//     arg0 = (uint8_t)dvr_led_pattern_t  (packed into uint16_t)
//     arg1 = 0
//     src  = SRC_DVR_LED
//     reason = EVR_CLASSIFIER_STABLE
//
// Notes:
//   - This module does NOT own ISR/attachInterrupt directly (that’s in dvr_led_init()).
//   - No buffering: emits only on accepted changes (after stability filtering).
// =============================================================================

void drv_dvr_led_init(void);

// Call frequently from main loop.
// Typical usage: drv_dvr_led_poll(millis());
void drv_dvr_led_poll(uint32_t now_ms);

// Optional observability for debug/tests
dvr_led_pattern_t drv_dvr_led_last_pattern(void);
uint32_t drv_dvr_led_last_change_ms(void);
