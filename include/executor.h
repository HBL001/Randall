// executor.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

void executor_init(void);
void executor_poll(uint32_t now_ms);
void executor_abort_feedback(void);

// True if any one-shot feedback/gesture engine is active (LED is excluded).
bool executor_busy(void);
