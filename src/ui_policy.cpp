// ui_policy.cpp
//
// Updated to match new canonical behaviour:
// - On firmware start (STATE_BOOTING entry), perform the MEGA boot cue:
//     "road runner – beep beep" + LED flashes twice
// - Then stay in a quiet "booting" visual (FAST blink) until DVR self-test confirms idle
// - READY (STATE_IDLE): LED solid (DVR is ON + idle; system ready)
// - RECORDING: slow blink
//
// No new enums/macros are invented here; it only uses patterns that must already exist in enums.h.
// If any of the patterns below do not exist in your enums.h, replace them with the nearest existing
// ones (do not create new aliases).

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

// Boot cue: "beep beep" + LED flashes twice.
// We implement as a beep pattern + a short LED pattern that represents "double flash".
// This assumes enums.h provides these patterns. If your actual names differ, substitute.
// ----------------------------------------------------------------------------
static inline void boot_cue(uint32_t now_ms)
{
    // "road runner – beep beep"
    // Prefer a dedicated pattern if you have one; otherwise BEEP_DOUBLE is acceptable.
#ifdef BEEP_BOOT_BEEP_BEEP
    beep(now_ms, BEEP_BOOT_BEEP_BEEP);
#else
    beep(now_ms, BEEP_DOUBLE);
#endif

    // LED flashes twice
#ifdef LED_DOUBLE_FLASH
    led(now_ms, LED_DOUBLE_FLASH);
#else
    // Fallback: if you don't have a dedicated "double flash" pattern,
    // use a short "OK" / "pulse" pattern if available, else leave LED alone.
    // (Do NOT invent new enums here.)
#ifdef LED_OK_PULSE
    led(now_ms, LED_OK_PULSE);
#else
    // No safe fallback: leave the LED pattern unchanged.
#endif
#endif
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
            // On entry, play MEGA boot cue once.
            if (state_changed)
                boot_cue(now_ms);

            // While booting/self-test, show activity.
            // Use FAST blink as "busy" until DVR confirms IDLE.
            led(now_ms, LED_FAST_BLINK);
            break;

        case STATE_IDLE:
            // READY: system ready; DVR is ON and idle (solid red).
            led(now_ms, LED_SOLID);
            // no beep here; "ready" beep/flash should come from the FSM when DVR idle is confirmed
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
                    beep(now_ms, BEEP_ERROR_FAST);
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
                beep(now_ms, BEEP_SINGLE);
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
    // Recording started confirmation
    beep(now_ms, BEEP_DOUBLE);
}

void ui_policy_on_stop_confirmed(uint32_t now_ms)
{
    // Recording stopped confirmation
    beep(now_ms, BEEP_SINGLE);
}

void ui_policy_on_error(uint32_t now_ms, error_code_t err)
{
    (void)err;
    led(now_ms, LED_ERROR_PATTERN);
    beep(now_ms, BEEP_ERROR_FAST);
}
