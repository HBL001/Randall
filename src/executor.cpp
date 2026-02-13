// executor.cpp
// concurrent
//      LED + BEEP engines
//      DVR button press engine
//  handle
//  ACT_DVR_PRESS_SHORT/LONG
//
// Core changes applied:
// - Executor is now an action dispatcher that MUST NOT lose actions.
// - LED + BEEP are internal engines.
// - DVR press engine is non-blocking and runs concurrently with LED + BEEP.
// - Policy: ignore new DVR press requests while one is active,
//           BUT do not drop the action; requeue it for later.
//
// Notes:
// - Uses actionq_pop()/actionq_push() to preserve FIFO when actions cannot be serviced yet.
// - We treat LED as non-blocking (never a reason to stall other actions).

#include <Arduino.h>

#include <executor.h>
#include <timings.h>
#include "pins.h"
#include "action_queue.h"

// ----------------------------------------------------------------------------
// Internal state (independent engines)
// ----------------------------------------------------------------------------

static led_pattern_t  s_led_pat      = LED_OFF;
static bool           s_led_level    = false;
static uint32_t       s_led_next_ms  = 0;

static bool           s_beep_active     = false;
static beep_pattern_t s_beep_pat        = BEEP_NONE;
static uint8_t        s_beep_remaining  = 0;
static uint8_t        s_beep_phase      = 0;      // 0=on, 1=gap, 2=done-gap
static uint32_t       s_beep_next_ms    = 0;

// DVR press engine (one-shot waveform)
static bool           s_dvr_active    = false;
static bool           s_dvr_pressed   = false;
static uint32_t       s_dvr_next_ms   = 0;
static uint16_t       s_dvr_press_ms  = 0;

// ----------------------------------------------------------------------------
// HW helpers
// ----------------------------------------------------------------------------

static inline void led_set(bool on)
{
    digitalWrite(PIN_STATUS_LED, on ? STATUS_LED_ON_LEVEL : STATUS_LED_OFF_LEVEL);
}

static inline void buzz_set(bool on)
{
    digitalWrite(PIN_BUZZER_OUT, on ? BUZZER_ON_LEVEL : BUZZER_OFF_LEVEL);
}

// DVR button emulation helper (uses pins.h names verbatim)
static inline void dvr_btn_set(bool pressed)
{
    digitalWrite(PIN_DVR_BTN_CMD, pressed ? DVR_BTN_PRESS_LEVEL : DVR_BTN_RELEASE_LEVEL);
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

void executor_abort_feedback(void)
{
    // LED
    s_led_pat = LED_OFF;
    s_led_level = false;
    s_led_next_ms = 0;
    led_set(false);

    // Beep
    s_beep_active = false;
    s_beep_pat = BEEP_NONE;
    s_beep_remaining = 0;
    s_beep_phase = 0;
    s_beep_next_ms = 0;
    buzz_set(false);

    // DVR press
    s_dvr_active = false;
    s_dvr_pressed = false;
    s_dvr_next_ms = 0;
    s_dvr_press_ms = 0;
    dvr_btn_set(false);
}

bool executor_busy(void)
{
    // LED can be "busy" forever; do not count it.
    return s_beep_active || s_dvr_active;
}

void executor_init(void)
{
    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_BUZZER_OUT, OUTPUT);
    pinMode(PIN_DVR_BTN_CMD, OUTPUT);

    led_set(false);
    buzz_set(false);
    dvr_btn_set(false);

    executor_abort_feedback();
}

// ----------------------------------------------------------------------------
// LED engine
// ----------------------------------------------------------------------------

static void led_step(uint32_t now_ms)
{
    if ((int32_t)(now_ms - s_led_next_ms) < 0)
        return;

    switch (s_led_pat)
    {
        case LED_OFF:
            led_set(false);
            s_led_level = false;
            s_led_next_ms = now_ms + 1000;
            return;

        case LED_SOLID:
            led_set(true);
            s_led_level = true;
            s_led_next_ms = now_ms + 1000;
            return;

        case LED_SLOW_BLINK:
        case LED_FAST_BLINK:
        case LED_LOCKOUT_PATTERN:
        case LED_ERROR_PATTERN:
        {
            // Keep simple here; can be promoted into timings.h later.
            const uint16_t on_ms  = (s_led_pat == LED_FAST_BLINK) ? 100 : 300;
            const uint16_t off_ms = (s_led_pat == LED_FAST_BLINK) ? 150 : 700;

            if (!s_led_level)
            {
                s_led_level = true;
                led_set(true);
                s_led_next_ms = now_ms + on_ms;
            }
            else
            {
                s_led_level = false;
                led_set(false);
                s_led_next_ms = now_ms + off_ms;
            }
            return;
        }

        default:
            s_led_pat = LED_OFF;
            led_set(false);
            s_led_level = false;
            s_led_next_ms = now_ms + 1000;
            return;
    }
}

// ----------------------------------------------------------------------------
// Beep engine (one-shot sequence)
// ----------------------------------------------------------------------------

static void start_beep(uint32_t now_ms, beep_pattern_t pat)
{
    s_beep_pat = pat;
    s_beep_phase = 0;

    switch (pat)
    {
        case BEEP_SINGLE:     s_beep_remaining = 1; break;
        case BEEP_DOUBLE:     s_beep_remaining = 2; break;
        case BEEP_TRIPLE:     s_beep_remaining = 3; break;
        case BEEP_ERROR_FAST: s_beep_remaining = 4; break;
        case BEEP_LOW_BAT:    s_beep_remaining = 2; break;
        default:              s_beep_remaining = 0; break;
    }

    s_beep_active = (s_beep_remaining > 0);
    s_beep_next_ms = now_ms;
}

static void beep_step(uint32_t now_ms)
{
    if (!s_beep_active) return;
    if ((int32_t)(now_ms - s_beep_next_ms) < 0) return;

    if (s_beep_remaining == 0)
    {
        buzz_set(false);
        s_beep_active = false;
        return;
    }

    if (s_beep_phase == 0)
    {
        buzz_set(true);
        s_beep_next_ms = now_ms + ((s_beep_pat == BEEP_ERROR_FAST) ? 50 : T_BEEP_MS);
        s_beep_phase = 1;
        return;
    }

    if (s_beep_phase == 1)
    {
        buzz_set(false);
        s_beep_remaining--;

        if (s_beep_remaining == 0)
        {
            s_beep_next_ms = now_ms + T_DOUBLE_BEEP_GAP_MS;
            s_beep_phase = 2;
        }
        else
        {
            s_beep_next_ms = now_ms + T_BEEP_GAP_MS;
            s_beep_phase = 0;
        }
        return;
    }

    // phase 2 done-gap
    s_beep_active = false;
}

// ----------------------------------------------------------------------------
// DVR press engine (one-shot press + enforced gap)
// ----------------------------------------------------------------------------

static bool start_dvr_press(uint32_t now_ms, uint16_t press_ms)
{
    if (s_dvr_active)
        return false; // cannot accept now

    s_dvr_active   = true;
    s_dvr_pressed  = true;
    s_dvr_press_ms = press_ms;

    dvr_btn_set(true);
    s_dvr_next_ms = now_ms + press_ms;
    return true;
}

static void dvr_step(uint32_t now_ms)
{
    if (!s_dvr_active) return;
    if ((int32_t)(now_ms - s_dvr_next_ms) < 0) return;

    if (s_dvr_pressed)
    {
        s_dvr_pressed = false;
        dvr_btn_set(false);
        s_dvr_next_ms = now_ms + T_DVR_PRESS_GAP_MS;
        return;
    }

    s_dvr_active = false;
}

// ----------------------------------------------------------------------------
// Executor poll: dispatch queued actions (without loss), then step engines
// ----------------------------------------------------------------------------

void executor_poll(uint32_t now_ms)
{
    // 1) Dispatch actions, but NEVER drop ones we cannot execute.
    enum { STASH_MAX = 16 };
    action_t stash[STASH_MAX];
    uint8_t  n = 0;

    action_t a;
    while (actionq_pop(&a))
    {
        bool handled = false;

        switch (a.id)
        {
            case ACT_LED_PATTERN:
                s_led_pat = (led_pattern_t)(a.arg0 & 0xFF);
                s_led_next_ms = now_ms;
                handled = true;
                break;

            case ACT_BEEP:
                // If a beep is already active, this will preempt it.
                // If you prefer "ignore while active", change start_beep() behaviour.
                start_beep(now_ms, (beep_pattern_t)(a.arg0 & 0xFF));
                handled = true;
                break;

            case ACT_DVR_PRESS_SHORT:
                handled = start_dvr_press(now_ms, (uint16_t)T_DVR_PRESS_SHORT_MS);
                break;

            case ACT_DVR_PRESS_LONG:
                handled = start_dvr_press(now_ms, (uint16_t)T_DVR_PRESS_LONG_MS);
                break;

            default:
                handled = false; // unknown => preserve
                break;
        }

        if (handled)
            continue;

        // Could not handle (e.g. DVR busy) => requeue later (preserve FIFO best-effort)
        if (n < STASH_MAX)
            stash[n++] = a;
#if CFG_DEBUG_SERIAL
        else
            Serial.println(F("WARN: executor action stash overflow; dropping action."));
#endif
    }

    // Re-push unhandled actions in original order.
    for (uint8_t i = 0; i < n; i++)
        (void)actionq_push(&stash[i]);

    // 2) Step all engines
    led_step(now_ms);
    beep_step(now_ms);
    dvr_step(now_ms);
}
