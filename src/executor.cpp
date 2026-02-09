// executor.cpp

#include "executor.h"

// Internal executor state
typedef enum : uint8_t
{
    EX_IDLE = 0,

    // LED pattern engine
    EX_LED_RUNNING,

    // Beep pattern engine
    EX_BEEP_RUNNING
} exec_state_t;

static exec_state_t s_state = EX_IDLE;

// Current action being executed (latched)
static action_t s_cur;

// Deadlines / counters for pattern playback
static uint32_t s_next_ms = 0;

// LED pattern playback
static led_pattern_t s_led_pat = LED_NONE;
static bool s_led_level = false; // current output level
static uint8_t s_led_phase = 0;

// Beep playback
static beep_pattern_t s_beep_pat = BEEP_NONE;
static bool s_buzz_level = false;
static uint8_t s_beep_remaining = 0; // number of beeps left in sequence
static uint8_t s_beep_phase = 0;     // 0=beep on, 1=gap, 2=done

static inline void led_set(bool on)
{
    digitalWrite(PIN_STATUS_LED, on ? STATUS_LED_ON_LEVEL : STATUS_LED_OFF_LEVEL);
}

static inline void buzz_set(bool on)
{
    digitalWrite(PIN_BUZZER_OUT, on ? BUZZER_ON_LEVEL : BUZZER_OFF_LEVEL);
}

void executor_abort_feedback(void)
{
    led_set(false);
    buzz_set(false);
    s_state = EX_IDLE;
}

bool executor_busy(void)
{
    return (s_state != EX_IDLE);
}

void executor_init(void)
{
    // assumes pins_init() already called, but defensively set outputs
    led_set(false);
    buzz_set(false);
    s_state = EX_IDLE;
}

// Translate an LED pattern into timed phases.
// Keep this simple and deterministic.
// You can later map these to timings.h values if you want.
static void start_led_pattern(uint32_t now_ms, led_pattern_t pat)
{
    s_led_pat = pat;
    s_led_phase = 0;
    s_led_level = false;
    s_next_ms = now_ms; // run immediately
    s_state = EX_LED_RUNNING;
}

// Translate a beep pattern into "N beeps" with fixed durations.
static void start_beep(uint32_t now_ms, beep_pattern_t pat)
{
    s_beep_pat = pat;
    s_beep_phase = 0;
    s_buzz_level = false;

    switch (pat)
    {
        case BEEP_SINGLE:     s_beep_remaining = 1; break;
        case BEEP_DOUBLE:     s_beep_remaining = 2; break;
        case BEEP_TRIPLE:     s_beep_remaining = 3; break;
        case BEEP_ERROR_FAST: s_beep_remaining = 4; break;
        case BEEP_LOW_BAT:    s_beep_remaining = 2; break;
        default:              s_beep_remaining = 0; break;
    }

    s_next_ms = now_ms; // run immediately
    s_state = EX_BEEP_RUNNING;
}

// One step of the LED pattern engine
static void led_step(uint32_t now_ms)
{
    if (now_ms < s_next_ms) return;

    switch (s_led_pat)
    {
        case LED_OFF:
            led_set(false);
            s_state = EX_IDLE;
            return;

        case LED_SOLID:
            led_set(true);
            s_state = EX_IDLE;
            return;

        case LED_SLOW_BLINK:
        case LED_FAST_BLINK:
        case LED_LOCKOUT_PATTERN:
        case LED_ERROR_PATTERN:
        {
            // Use simple on/off with distinct periods.
            // (You can tie these to timings.h later if desired.)
            const uint16_t on_ms  = (s_led_pat == LED_FAST_BLINK) ? 100 : 300;
            const uint16_t off_ms = (s_led_pat == LED_FAST_BLINK) ? 150 : 700;

            if (!s_led_level)
            {
                s_led_level = true;
                led_set(true);
                s_next_ms = now_ms + on_ms;
            }
            else
            {
                s_led_level = false;
                led_set(false);
                s_next_ms = now_ms + off_ms;
            }
            return;
        }

        default:
            led_set(false);
            s_state = EX_IDLE;
            return;
    }
}

// One step of the beep engine (non-blocking)
static void beep_step(uint32_t now_ms)
{
    if (now_ms < s_next_ms) return;

    if (s_beep_remaining == 0)
    {
        buzz_set(false);
        s_state = EX_IDLE;
        return;
    }

    // Fixed, deterministic timing constants (you can route to timings.h later)
    const uint16_t beep_on_ms  = (s_beep_pat == BEEP_ERROR_FAST) ? 50 : 80;
    const uint16_t beep_gap_ms = 80;
    const uint16_t seq_gap_ms  = 180;

    if (s_beep_phase == 0)
    {
        // turn buzzer on
        buzz_set(true);
        s_next_ms = now_ms + beep_on_ms;
        s_beep_phase = 1;
        return;
    }

    if (s_beep_phase == 1)
    {
        // turn buzzer off
        buzz_set(false);
        s_beep_remaining--;

        if (s_beep_remaining == 0)
        {
            s_next_ms = now_ms + seq_gap_ms;
            s_beep_phase = 2;
        }
        else
        {
            s_next_ms = now_ms + beep_gap_ms;
            s_beep_phase = 0;
        }
        return;
    }

    // phase 2: done (post gap)
    s_state = EX_IDLE;
}

// Main poll
void executor_poll(uint32_t now_ms)
{
    // If idle, pull next action
    if (s_state == EX_IDLE)
    {
        action_t a;
        if (!actionq_pop(&a))
            return;

        s_cur = a;

        switch (s_cur.id)
        {
            case ACT_LED_PATTERN:
                start_led_pattern(now_ms, (led_pattern_t)(s_cur.arg0 & 0xFF));
                break;

            case ACT_BEEP:
                start_beep(now_ms, (beep_pattern_t)(s_cur.arg0 & 0xFF));
                break;

            default:
                // Unsupported for now: ignore deterministically
                s_state = EX_IDLE;
                break;
        }
        return;
    }

    // If running, advance the active engine
    if (s_state == EX_LED_RUNNING)
    {
        led_step(now_ms);
        return;
    }

    if (s_state == EX_BEEP_RUNNING)
    {
        beep_step(now_ms);
        return;
    }

    // Failsafe
    s_state = EX_IDLE;
}
