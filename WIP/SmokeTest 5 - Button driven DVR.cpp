/*
    SMOKE TEST: Button-driven DVR lifecycle with continuous DVR LED monitoring

    Sequence:
      1) Wait SHORT press  -> DVR POWER ON (LONG shutter press)
      2) Wait SHORT press  -> DVR TOGGLE (SHORT press) (start recording)
      3) Wait SHORT press  -> DVR TOGGLE (SHORT press) (stop recording)
      4) Wait GRACE hold   -> DVR POWER OFF (LONG press)
         - Nuclear hold is ignored (LTC will cut power)

    Notes:
      - Uses existing identifiers from: pins.h, timings.h, enums.h, event_queue.h, action_queue.h, executor.h, dvr_led.h
      - INT0 (D2) = LTC INT# used as the user button source (press timing measured on ASSERT/DEASSERT)
      - DVR LED monitoring is owned by dvr_led.cpp (it likely uses INT1). DO NOT define INT1 ISR here.
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
// Action enqueue helpers (use your existing action IDs / types)
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

// Consume ONE complete press (measured from INT# edges). Returns true when a press completes.
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
// DVR LED reporting (owned by dvr_led module)
// ============================================================================

// IMPORTANT:
// We do NOT add any new enums or alias identifiers.
// We only print using existing dvr_led_pattern_t values from enums.h.

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

static void dvr_led_report_poll(uint32_t now)
{
    // Keep the module running (it owns INT1 if implemented that way)
    dvr_led_poll(now);

    // NOTE:
    // This assumes your dvr_led module exposes a getter returning dvr_led_pattern_t.
    // If your current dvr_led.h doesn't have this, add it there and implement in dvr_led.cpp.
    dvr_led_pattern_t p = dvr_led_get_pattern();

    if (p != s_last_pat)
    {
        s_last_pat = p;
        print_dvr_pattern(p);
    }
}

// ============================================================================
// Smoke-test sequencer
// ============================================================================

typedef enum
{
    STEP_WAIT_PWRON = 0,
    STEP_WAIT_READY,
    STEP_WAIT_START_REC,
    STEP_WAIT_STOP_REC,
    STEP_WAIT_PWROFF,
    STEP_DONE
} smoke_step_t;

static smoke_step_t s_step = STEP_WAIT_PWRON;
static uint32_t     s_deadline_ms = 0;

static inline void arm_wait(uint32_t now, uint32_t wait_ms)
{
    s_deadline_ms = now + wait_ms;
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

    // DVR LED module init (it may configure INT1 internally)
    dvr_led_init();

    // INT0 for LTC INT#
    s_last_int0_level = (uint8_t)digitalRead(PIN_LTC_INT_N);
    attachInterrupt(digitalPinToInterrupt(PIN_LTC_INT_N), isr_ltc_int_change, CHANGE);

#if CFG_DEBUG_SERIAL
    Serial.println(F("SMOKE: short->ON, short->START, short->STOP, grace->OFF. DVR LED monitored continuously."));
#endif
}

void loop()
{
    const uint32_t now = millis();

    // Always run executor + DVR LED monitoring
    executor_poll(now);
    dvr_led_report_poll(now);

    // Active wait window (non-blocking)
    if ((s_step == STEP_WAIT_READY) && !time_reached(now, s_deadline_ms))
        return;

    if (s_step == STEP_WAIT_READY && time_reached(now, s_deadline_ms))
        s_step = STEP_WAIT_START_REC;

    // Wait for a completed press
    uint32_t press_ms = 0;
    if (!consume_press_ms(&press_ms))
        return;

#if CFG_DEBUG_SERIAL
    Serial.print(F("PRESS ms="));
    Serial.println(press_ms);
#endif

    // Nuclear hold: ignore in software (LTC cuts power)
    if (press_ms >= T_BTN_NUCLEAR_MS)
    {
#if CFG_DEBUG_SERIAL
        Serial.println(F("NUCLEAR hold: ignored (LTC hardware will cut power)."));
#endif
        return;
    }

    const bool is_grace = (press_ms >= T_BTN_GRACE_MS);

    // Determinism: do not enqueue while executor is busy
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
            s_step = STEP_WAIT_READY;
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
                s_step = STEP_DONE;
                return;
            }

#if CFG_DEBUG_SERIAL
            Serial.println(F("Action: DVR TOGGLE (SHORT press) -> start recording"));
#endif
            enqueue_dvr_short(now);
            enqueue_beep(now, BEEP_SINGLE);

            s_step = STEP_WAIT_STOP_REC;
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
                s_step = STEP_DONE;
                return;
            }

#if CFG_DEBUG_SERIAL
            Serial.println(F("Action: DVR TOGGLE (SHORT press) -> stop recording"));
#endif
            enqueue_dvr_short(now);
            enqueue_beep(now, BEEP_SINGLE);

            s_step = STEP_WAIT_PWROFF;
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

            s_step = STEP_DONE;
            return;
        }

        case STEP_DONE:
        default:
            // Keep monitoring DVR LED; no further control actions.
            return;
    }
}
