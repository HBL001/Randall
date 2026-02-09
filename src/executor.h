// executor.h
// Non-blocking action executor for:
//   - ACT_BEEP (buzzer patterns)
//   - ACT_LED_PATTERN (status LED patterns)
//
// Model:
// - Executor is a tiny state machine driven by executor_poll(now_ms).
// - It processes one action at a time (deterministic).
// - No delay(). No blocking. Uses deadlines.

#pragma once

#include "config.h"
#include "action_queue.h"

void executor_init(void);

// Call frequently from main loop (tick-driven)
void executor_poll(uint32_t now_ms);

// Optional: force stop all ongoing feedback outputs
void executor_abort_feedback(void);

// Optional: query whether executor is busy with an action
bool executor_busy(void);
