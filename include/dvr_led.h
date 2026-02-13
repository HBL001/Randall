// dvr_led.h
#pragma once

#include <stdint.h>
#include "enums.h"

// =============================================================================
// dvr_led (drv)
// ----------------------------------------------------------------------------
// DVR LED sniffer / classifier (INT1 / D3 on ATmega328P Nano)
//
// Electrical assumptions:
// - PIN_DVR_STAT is the sniffer input (see pins.h).
// - LOW = DVR LED ON (per your NPN mirror / inversion stage).
//
// Output semantics (classifier only):
// - dvr_led_get_pattern() returns one of:
//     DVR_LED_OFF
//     DVR_LED_SOLID
//     DVR_LED_SLOW_BLINK
//     DVR_LED_FAST_BLINK
//     DVR_LED_UNKNOWN
//
// IMPORTANT (RunCam behaviour):
// - FAST_BLINK is *not* uniquely "shutdown".
//   It can also indicate microSD/card error. Interpret meaning in controller_fsm,
//   not here.
//
// API contract:
// - dvr_led_init():
//     Configure GPIO + attachInterrupt(), reset internal classifier state.
// - dvr_led_poll(now_ms):
//     Drain ISR edge buffer, update classifier, apply quiet-time transitions.
//     Call frequently from loop().
// - dvr_led_get_pattern():
//     Current classified pattern (sticky blink until quiet-time).
// =============================================================================

void dvr_led_init(void);
void dvr_led_poll(uint32_t now_ms);
dvr_led_pattern_t dvr_led_get_pattern(void);
