#pragma once

#include <stdint.h>
void dvr_led_init(void);                    // init INT1 + internal state
void dvr_led_poll(uint32_t now_ms);         // poll led and generate events into eventq
