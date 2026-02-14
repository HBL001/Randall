// dvr_button.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "enums.h"

void     button_init(void);
void     button_poll(uint32_t now_ms);

// Observability
bool     button_is_pressed(void);
uint16_t button_last_press_ms(void);
