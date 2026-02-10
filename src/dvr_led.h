#pragma once

#include <stdint.h>
#include "enums.h"

// DVR LED sniffer/classifier (INT1 / D3 on ATmega328P)
// - PIN_DVR_STAT is the sniffer input (see pins.h)
// - LOW = DVR LED ON (per mirror test)
// - Classification lives entirely in dvr_led.cpp
//
// API contract:
// - dvr_led_init(): configure pin + attachInterrupt, reset internal state
// - dvr_led_poll(now_ms): run classifier / quiet-time checks; call frequently from loop()
// - dvr_led_get_pattern(): returns current classified pattern (dvr_led_pattern_t)

void dvr_led_init(void);
void dvr_led_poll(uint32_t now_ms);
dvr_led_pattern_t dvr_led_get_pattern(void);
