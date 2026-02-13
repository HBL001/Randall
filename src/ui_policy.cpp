// ui_policy.cpp

#include "ui_policy.h"

#include "action_queue.h"

// ----------------------------------------------------------------------------
// Internal state (policy-level only)
// ----------------------------------------------------------------------------
static controller_state_t s_last_state = STATE_OFF;
static led_pattern_t      s_last_led   = LED_OFF;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
static inline void emit_action(uint32_t now_ms, action_id_t id, uint16_t arg0, uint16_t arg1)
{
    action_t a;
    a.t_enq_ms = now_ms;
    a.id       = id;
    a.arg0     = arg0;
    a.arg1     = arg1;
    (void)actionq_push(&a);
}

static inline void led(uint32_t now_ms, led_pattern_t p)
{
    // Don’t spam identical LED pattern commands.
    if (p == s_last_led)
        return;

    s_last_led = p;
    emit_action(now_ms, ACT_LED_PATTERN, (uint16_t)p, 0);
}

static inline void beep(uint32_t now_ms, beep_pattern_t p)
{
    emit_action(now_ms, ACT_BEEP, (uint16_t)p, 0);
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
void ui_policy_init(void)
{
    s_last_state = STATE_OFF;
    s_last_led   = LED_OFF;
}

void ui_policy_on_state_enter(uint32_t now_ms,
                              controller_state_t s,
                              error_code_t err,
                              battery_state_t bat)
{
    const bool state_changed = (s != s_last_state);
    s_last_state = s;

    switch (s)
    {
        case STATE_OFF:
            led(now_ms, LED_OFF);
            // intentionally quiet
            break;

        case STATE_BOOTING:
            led(now_ms, LED_FAST_BLINK);
            break;

        case STATE_IDLE:
            led(now_ms, LED_SOLID);
            break;

        case STATE_RECORDING:
            led(now_ms, LED_SLOW_BLINK);
            break;

        case STATE_LOW_BAT:
            // Use bat to scale severity if your FSM routes both LOW/CRITICAL here.
            led(now_ms, LED_SLOW_BLINK);
            if (state_changed)
            {
                if (bat == BAT_CRITICAL)
                    beep(now_ms, BEEP_ERROR_FAST); // or a dedicated CRITICAL pattern if you have one
                else
                    beep(now_ms, BEEP_LOW_BAT);
            }
            break;

        case STATE_ERROR:
            (void)err;
            led(now_ms, LED_ERROR_PATTERN);
            if (state_changed)
                beep(now_ms, BEEP_ERROR_FAST);
            break;

        case STATE_LOCKOUT:
            led(now_ms, LED_LOCKOUT_PATTERN);
            if (state_changed)
                beep(now_ms, BEEP_SINGLE); // one-time “nope” cue on entry; keep quiet afterwards
            break;

        default:
            led(now_ms, LED_ERROR_PATTERN);
            if (state_changed)
                beep(now_ms, BEEP_ERROR_FAST);
            break;
    }
}

void ui_policy_on_record_confirmed(uint32_t now_ms)
{
    beep(now_ms, BEEP_DOUBLE);
}

void ui_policy_on_stop_confirmed(uint32_t now_ms)
{
    beep(now_ms, BEEP_SINGLE);
}

void ui_policy_on_error(uint32_t now_ms, error_code_t err)
{
    (void)err;
    led(now_ms, LED_ERROR_PATTERN);
    beep(now_ms, BEEP_ERROR_FAST);
}
