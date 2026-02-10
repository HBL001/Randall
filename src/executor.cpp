// executor.cpp
// concurrent
//      LED + BEEP engines
//      DVR button press engine
//  handle
//  ACT_DVR_PRESS_SHORT/LONG
//
// - DVR press engine is non-blocking and runs concurrently with LED + BEEP.
// - Policy: ignore new DVR press requests while one is active.

#include <executor.h>
#include <timings.h>
#include <power_mgr.h>


// ----------------------------------------------------------------------------
// Internal state (independent engines)
// ----------------------------------------------------------------------------

static led_pattern_t  s_led_pat = LED_OFF;
static bool           s_led_level = false;
static uint32_t       s_led_next_ms = 0;

static bool           s_beep_active = false;
static beep_pattern_t s_beep_pat = BEEP_NONE;
static uint8_t        s_beep_remaining = 0;
static uint8_t        s_beep_phase = 0;      // 0=on, 1=gap, 2=done-gap
static uint32_t       s_beep_next_ms = 0;

// DVR press engine (one-shot waveform)
static bool           s_dvr_active = false;
static bool           s_dvr_pressed = false;
static uint32_t       s_dvr_next_ms = 0;
static uint16_t       s_dvr_press_ms = 0;

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
    if (now_ms < s_led_next_ms) return;

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
    if (now_ms < s_beep_next_ms) return;

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

    s_beep_active = false; // phase 2 done
}


// ----------------------------------------------------------------------------
// DVR press engine (one-shot press + enforced gap)
// ----------------------------------------------------------------------------

static void start_dvr_press(uint32_t now_ms, uint16_t press_ms)
{
    if (s_dvr_active) return; // deterministic: ignore while active

    s_dvr_active   = true;
    s_dvr_pressed  = true;
    s_dvr_press_ms = press_ms;

    dvr_btn_set(true);
    s_dvr_next_ms = now_ms + press_ms;
}

static void dvr_step(uint32_t now_ms)
{
    if (!s_dvr_active) return;
    if (now_ms < s_dvr_next_ms) return;

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
// Executor poll: apply queued actions, then step all engines
// ----------------------------------------------------------------------------

void executor_poll(uint32_t now_ms)
{
    action_t a;
    while (actionq_pop(&a))
    {
        switch (a.id)
        {
            case ACT_LED_PATTERN:
                s_led_pat = (led_pattern_t)(a.arg0 & 0xFF);
                s_led_next_ms = now_ms;
                break;

            case ACT_BEEP:
                start_beep(now_ms, (beep_pattern_t)(a.arg0 & 0xFF));
                break;

            case ACT_DVR_PRESS_SHORT:
                start_dvr_press(now_ms, T_DVR_PRESS_SHORT_MS);
                break;

            case ACT_DVR_PRESS_LONG:
                start_dvr_press(now_ms, T_DVR_PRESS_LONG_MS);
                break;
				
			case ACT_LTC_KILL_ASSERT:
				power_mgr_kill_assert();
				break;

			case ACT_LTC_KILL_DEASSERT:
				power_mgr_kill_deassert();
				break;

            default:
                break; 
        }
    }

    led_step(now_ms);
    beep_step(now_ms);
    dvr_step(now_ms);
}
