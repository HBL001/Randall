// ui_policy.cpp

#include "ui_policy.h"

#include <Arduino.h>

#include "action_queue.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline void emit_action(action_id_t id, uint16_t arg0, uint16_t arg1)
{
    action_t a;
    a.t_enq_ms = millis();
    a.id       = id;
    a.arg0     = arg0;
    a.arg1     = arg1;
    (void)actionq_push(&a);
}

static inline void led(led_pattern_t p)
{
    emit_action(ACT_LED_PATTERN, (uint16_t)p, 0);
}

static inline void beep(beep_pattern_t p)
{
    emit_action(ACT_BEEP, (uint16_t)p, 0);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void ui_policy_init(void)
{
    // No persistent state required right now.
}

void ui_policy_on_state_enter(controller_state_t s,
                              error_code_t err,
                              battery_state_t bat)
{
    (void)err;
    (void)bat;

    switch (s)
    {
        case STATE_OFF:
            led(LED_OFF);
            // intentionally quiet
            break;

        case STATE_BOOTING:
            // visual “working” indicator
            led(LED_FAST_BLINK);
            break;

        case STATE_IDLE:
            led(LED_SOLID);
            break;

        case STATE_RECORDING:
            // Recording should be quiet; minimal UI.
            led(LED_SLOW_BLINK);
            break;

        case STATE_LOW_BAT:
            led(LED_SLOW_BLINK);
            beep(BEEP_LOW_BAT);
            break;

        case STATE_ERROR:
            led(LED_ERROR_PATTERN);
            beep(BEEP_ERROR_FAST);
            break;

        case STATE_LOCKOUT:
            led(LED_LOCKOUT_PATTERN);
            // quiet by default; you can add a periodic chirp later if required
            break;

        default:
            led(LED_ERROR_PATTERN);
            beep(BEEP_ERROR_FAST);
            break;
    }
}

void ui_policy_on_record_confirmed(void)
{
    // Your user story wants a clear “recording confirmed” cue.
    beep(BEEP_DOUBLE);
}

void ui_policy_on_stop_confirmed(void)
{
    // Optional “stop confirmed” cue (keep subtle).
    beep(BEEP_SINGLE);
}

void ui_policy_on_error(error_code_t err)
{
    (void)err;
    led(LED_ERROR_PATTERN);
    beep(BEEP_ERROR_FAST);
}
