#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * executor.h
 *
 * Feedback executor
 *
 * Responsibilities:
 * - LED pattern execution
 * - Beep pattern execution
 *
 * NON-responsibilities (by design):
 * - DVR button presses
 * - Power control
 * - State authority
 *
 * DVR button contact closure is owned exclusively by dvr_ctrl.
 */

typedef enum {
    LED_OFF = 0,
    LED_SOLID,
    LED_SLOW_BLINK,
    LED_FAST_BLINK,
    LED_LOCKOUT_PATTERN,
    LED_ERROR_PATTERN
} led_pattern_t;

typedef enum {
    BEEP_NONE = 0,
    BEEP_SINGLE,
    BEEP_DOUBLE,
    BEEP_TRIPLE,
    BEEP_ERROR_FAST,
    BEEP_LOW_BAT
} beep_pattern_t;

/**
 * Initialise executor hardware outputs.
 * Ensures LED and buzzer are inactive.
 */
void executor_init(void);

/**
 * Abort all active feedback immediately.
 * Used on fault / shutdown / reset.
 */
void executor_abort_feedback(void);

/**
 * Returns true if executor is currently busy
 * (only beeps count as busy; LED may run indefinitely).
 */
bool executor_busy(void);

/**
 * Poll function.
 * Must be called frequently from main loop.
 */
void executor_poll(uint32_t now_ms);
