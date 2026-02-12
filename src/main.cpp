/*
    SMOKE TEST: Button-driven DVR lifecycle with VERIFIED dvr_led module
    + STOP settle-time measurement (so we can set sane timeouts, not 17s guesses)

    Sequence (user button = LTC INT# on D2):
      1) SHORT press  -> DVR POWER ON (LONG shutter press) -> wait SOLID
      2) SHORT press  -> DVR TOGGLE (SHORT press) -> wait SLOW_BLINK (recording)
      3) SHORT press  -> DVR TOGGLE (SHORT press) -> wait SOLID (idle)  [MEASURE settle time]
      4) GRACE hold   -> DVR POWER OFF (LONG press) -> wait OFF (FAST_BLINK allowed)

    Notes:
      - INT0 (D2) = LTC INT# used as the user button source.
      - DVR LED monitoring is owned by dvr_led.cpp (INT1). DO NOT define INT1 ISR here.
      - Uses ONLY existing identifiers from pins.h/timings.h/enums.h/... (no new global macros).
*/

#include <Arduino.h>

#include "config.h"
#include "pins.h"
#include "timings.h"
#include "enums.h"
#include "event_queue.h"
#include "action_queue.h"
#include "executor.h"
#include "dvr_led.h"

// ----------------------------------------------------------------------------
// Time helper
// ----------------------------------------------------------------------------
static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

// ----------------------------------------------------------------------------
// Action enqueue helpers
// ----------------------------------------------------------------------------
static inline void enqueue_beep(uint32_t now, beep_pattern_t pat)
{
    action_t a;
    a.t_enq_ms = now;
    a.id       = ACT_BEEP;
    a.arg0     = (uint16_t)pat;
    a.arg1     = 0;
    actionq_push(&a);
}

static inline void enqueue_dvr_short(uint32_t now)
{
    action_t a;
    a.t_enq_ms = now;
    a.id       = ACT_DVR_PRESS_SHORT;
    a.arg0     = 0;
    a.arg1     = 0;
    actionq_push(&a);
}

static inline void enqueue_dvr_long(uint32_t now)
{
    action_t a;
    a.t_enq_ms = now;
    a.id       = ACT_DVR_PRESS_LONG;
    a.arg0     = 0;
    a.arg1     = 0;
    actionq_push(&a);
}

// ============================================================================
// INT0 (LTC INT#) -> press measurement (user button semantics)
// ============================================================================
static volatile uint8_t s_last_int0_level = 1;

static bool     g_btn_down = false;
static uint32_t g_btn_down_ms = 0;

static void isr_ltc_int_change()
{
    const uint8_t level = (uint8_t)digitalRead(PIN_LTC_INT_N);
    if (level == s_last_int0_level) return;
    s_last_int0_level = level;

    event_t e;
    e.t_ms   = millis();
    e.src    = SRC_LTC;
    e.reason = (level == LTC_INT_ASSERT_LEVEL) ? EVR_EDGE_FALL : EVR_EDGE_RISE;
    e.arg0   = (uint16_t)level;
    e.arg1   = 0;
    e.id     = (level == LTC_INT_ASSERT_LEVEL) ? EV_LTC_INT_ASSERTED : EV_LTC_INT_DEASSERTED;

    eventq_push_isr(&e);
}

static bool consume_press_ms(uint32_t *out_press_ms)
{
    event_t ev;
    while (eventq_pop(&ev))
    {
        if (ev.id == EV_LTC_INT_ASSERTED)
        {
            g_btn_down = true;
            g_btn_down_ms = ev.t_ms;
        }
        else if (ev.id == EV_LTC_INT_DEASSERTED && g_btn_down)
        {
            const uint32_t press_ms = ev.t_ms - g_btn_down_ms;
            g_btn_down = false;
            g_btn_down_ms = 0;

            if (press_ms < T_BTN_SHORT_MIN_MS)
                return false;

            *out_press_ms = press_ms;
            return true;
        }
    }
    return false;
}

// ============================================================================
// DVR LED reporting (dvr_led module)
// ============================================================================
static dvr_led_pattern_t s_last_pat = DVR_LED_UNKNOWN;

static void print_dvr_pattern(dvr_led_pattern_t p)
{
#if CFG_DEBUG_SERIAL
    Serial.print(F("DVR LED PATTERN -> "));
    switch (p)
    {
        case DVR_LED_OFF:           Serial.println(F("OFF")); break;
        case DVR_LED_SOLID:         Serial.println(F("SOLID")); break;
        case DVR_LED_SLOW_BLINK:    Serial.println(F("SLOW_BLINK")); break;
        case DVR_LED_FAST_BLINK:    Serial.println(F("FAST_BLINK")); break;
        case DVR_LED_ABNORMAL_BOOT: Serial.println(F("ABNORMAL_BOOT")); break;
        case DVR_LED_UNKNOWN:
        default:                    Serial.println(F("UNKNOWN")); break;
    }
#else
    (void)p;
#endif
}

static inline dvr_led_pattern_t dvr_pat_now(uint32_t now)
{
    dvr_led_poll(now);
    return dvr_led_get_pattern();
}

static void dvr_led_report_poll(uint32_t now)
{
    const dvr_led_pattern_t p = dvr_pat_now(now);
    if (p != s_last_pat)
    {
        s_last_pat = p;
        print_dvr_pattern(p);
    }
}

// ============================================================================
// Smoke sequencer with LED-gated readiness checks
// + STOP settle time measurement (wait SOLID stable for T_SOLID_MS)
// ============================================================================
typedef enum
{
    STEP_WAIT_PWRON = 0,
    STEP_WAIT_READY_SOLID,      // after power-on command: wait SOLID
    STEP_WAIT_START_REC,        // wait user short, send start, then wait SLOW_BLINK
    STEP_WAIT_REC_SLOW,         // waiting for SLOW_BLINK confirmation
    STEP_WAIT_STOP_REC,         // wait user short, send stop, then wait SOLID stable (measure)
    STEP_WAIT_IDLE_SOLID,       // waiting for SOLID stable (measure settle)
    STEP_WAIT_PWROFF,           // wait user grace, send off, then wait OFF
    STEP_WAIT_OFF,              // waiting for OFF confirmation
    STEP_DONE
} smoke_step_t;

static smoke_step_t s_step = STEP_WAIT_PWRON;
static uint32_t     s_deadline_ms = 0;

// STOP settle measurement
static uint32_t     s_stop_t0_ms = 0;
static uint32_t     s_solid_since_ms = 0;

static inline void arm_wait(uint32_t now, uint32_t wait_ms)
{
    s_deadline_ms = now + wait_ms;
}

static inline bool wait_expired(uint32_t now)
{
    return time_reached(now, s_deadline_ms);
}

static inline void fail_and_halt(const __FlashStringHelper *msg)
{
#if CFG_DEBUG_SERIAL
    Serial.print(F("SMOKE FAIL: "));
    Serial.println(msg);
#endif
    enqueue_beep(millis(), BEEP_DOUBLE);
    s_step = STEP_DONE;
}

void setup()
{
#if CFG_DEBUG_SERIAL
    Serial.begin(115200);
    delay(200);
#endif

    pins_init();
    eventq_init();
    actionq_init();
    executor_init();

    dvr_led_init();

    // INT0 for LTC INT#
    s_last_int0_level = (uint8_t)digitalRead(PIN_LTC_INT_N);
    attachInterrupt(digitalPinToInterrupt(PIN_LTC_INT_N), isr_ltc_int_change, CHANGE);

#if CFG_DEBUG_SERIAL
    Serial.println(F("SMOKE: short->ON(wait SOLID), short->START(wait SLOW), short->STOP(measure -> SOLID stable), grace->OFF(wait OFF)."));
#endif
}

void loop()
{
    const uint32_t now = millis();

    executor_poll(now);
    dvr_led_report_poll(now);

    // --- State-driven waiting on DVR LED confirmations ---

    if (s_step == STEP_WAIT_READY_SOLID)
    {
        const dvr_led_pattern_t p = dvr_led_get_pattern();
        if (p == DVR_LED_SOLID)
        {
#if CFG_DEBUG_SERIAL
            Serial.println(F("READY: DVR LED is SOLID."));
#endif
            s_step = STEP_WAIT_START_REC;
            return;
        }

        if (wait_expired(now))
        {
            fail_and_halt(F("timeout waiting for SOLID after power-on"));
            return;
        }
        return;
    }

    if (s_step == STEP_WAIT_REC_SLOW)
    {
        const dvr_led_pattern_t p = dvr_led_get_pattern();
        if (p == DVR_LED_SLOW_BLINK)
        {
#if CFG_DEBUG_SERIAL
            Serial.println(F("CONFIRMED: DVR is RECORDING (SLOW_BLINK)."));
#endif
            s_step = STEP_WAIT_STOP_REC;
            return;
        }

        if (p == DVR_LED_FAST_BLINK)
        {
            fail_and_halt(F("FAST_BLINK seen while expecting recording"));
            return;
        }

        if (wait_expired(now))
        {
            fail_and_halt(F("timeout waiting for SLOW_BLINK after start"));
            return;
        }
        return;
    }

    // STOP measurement wait:
    // We require SOLID to persist for T_SOLID_MS (stable), then print settle time.
    if (s_step == STEP_WAIT_IDLE_SOLID)
    {
        const dvr_led_pattern_t p = dvr_led_get_pattern();

        if (p == DVR_LED_SOLID)
        {
            if (s_solid_since_ms == 0)
                s_solid_since_ms = now;

            if (time_reached(now, s_solid_since_ms + (uint32_t)T_SOLID_MS))
            {
#if CFG_DEBUG_SERIAL
                const uint32_t settle_ms = now - s_stop_t0_ms;
                Serial.print(F("STOP settle_ms="));
                Serial.println(settle_ms);
                Serial.println(F("CONFIRMED: DVR is IDLE (SOLID stable)."));
#endif
                s_step = STEP_WAIT_PWROFF;
                return;
            }
        }
        else
        {
            s_solid_since_ms = 0;
        }

        if (wait_expired(now))
        {
            fail_and_halt(F("timeout waiting for SOLID after stop"));
            return;
        }
        return;
    }

    if (s_step == STEP_WAIT_OFF)
    {
        const dvr_led_pattern_t p = dvr_led_get_pattern();
        if (p == DVR_LED_OFF)
        {
#if CFG_DEBUG_SERIAL
            Serial.println(F("CONFIRMED: DVR is OFF."));
#endif
            s_step = STEP_DONE;
            return;
        }

        // FAST_BLINK during shutdown is acceptable; ignore it
        if (wait_expired(now))
        {
            fail_and_halt(F("timeout waiting for OFF after power-off"));
            return;
        }
        return;
    }

    // --- Otherwise, we are waiting for a user press ---
    uint32_t press_ms = 0;
    if (!consume_press_ms(&press_ms))
        return;

#if CFG_DEBUG_SERIAL
    Serial.print(F("PRESS ms="));
    Serial.println(press_ms);
#endif

    if (press_ms >= T_BTN_NUCLEAR_MS)
    {
#if CFG_DEBUG_SERIAL
        Serial.println(F("NUCLEAR hold: ignored (LTC hardware will cut power)."));
#endif
        return;
    }

    const bool is_grace = (press_ms >= T_BTN_GRACE_MS);

    // Do not enqueue while executor is busy
    if (executor_busy())
        return;

    switch (s_step)
    {
        case STEP_WAIT_PWRON:
        {
            if (is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Grace hold ignored here; expecting SHORT to power on."));
#endif
                return;
            }

#if CFG_DEBUG_SERIAL
            Serial.println(F("Action: DVR POWER ON (LONG press)"));
#endif
            enqueue_dvr_long(now);
            enqueue_beep(now, BEEP_DOUBLE);

            arm_wait(now, (uint32_t)T_BOOT_TIMEOUT_MS);
            s_step = STEP_WAIT_READY_SOLID;
            return;
        }

        case STEP_WAIT_START_REC:
        {
            if (is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Grace hold early: powering off (LONG press)"));
#endif
                enqueue_dvr_long(now);
                enqueue_beep(now, BEEP_DOUBLE);

                arm_wait(now, (uint32_t)T_BOOT_TIMEOUT_MS);
                s_step = STEP_WAIT_OFF;
                return;
            }

#if CFG_DEBUG_SERIAL
            Serial.println(F("Action: DVR TOGGLE (SHORT press) -> start recording"));
#endif
            enqueue_dvr_short(now);
            enqueue_beep(now, BEEP_SINGLE);

            // Wait for recording confirmation (slow blink)
            arm_wait(now, (uint32_t)(T_SLOW_MAX_MS * 2u + T_SOLID_MS));
            s_step = STEP_WAIT_REC_SLOW;
            return;
        }

        case STEP_WAIT_STOP_REC:
        {
            if (is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Grace hold: powering off (LONG press)"));
#endif
                enqueue_dvr_long(now);
                enqueue_beep(now, BEEP_DOUBLE);

                arm_wait(now, (uint32_t)T_BOOT_TIMEOUT_MS);
                s_step = STEP_WAIT_OFF;
                return;
            }

#if CFG_DEBUG_SERIAL
            Serial.println(F("Action: DVR TOGGLE (SHORT press) -> stop recording"));
            Serial.println(F("Waiting for SOLID stable after stop (measuring)..."));
#endif
            enqueue_dvr_short(now);
            enqueue_beep(now, BEEP_SINGLE);

            // Measurement run: give a provisional budget.
            // We'll tighten once you see STOP settle_ms in the logs.
            arm_wait(now, (uint32_t)(T_SOLID_MS + T_SLOW_MAX_MS * 3u));

            s_stop_t0_ms = now;
            s_solid_since_ms = 0;

            s_step = STEP_WAIT_IDLE_SOLID;
            return;
        }

        case STEP_WAIT_PWROFF:
        {
            if (!is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Expect GRACE hold to power off (>=T_BTN_GRACE_MS). Short ignored."));
#endif
                return;
            }

#if CFG_DEBUG_SERIAL
            Serial.println(F("Action: DVR POWER OFF (LONG press) [grace hold]"));
#endif
            enqueue_dvr_long(now);
            enqueue_beep(now, BEEP_DOUBLE);

            arm_wait(now, (uint32_t)T_BOOT_TIMEOUT_MS);
            s_step = STEP_WAIT_OFF;
            return;
        }

        case STEP_DONE:
        default:
            return;
    }
}
