/*
    SMOKE TEST: Button-driven DVR lifecycle with continuous DVR LED monitoring
    + Fuel gauge polling + battery status/event logging

    (Uses dvr_button as the ONLY producer of EV_BTN_* events)

    Sequence:
      1) Wait SHORT press  -> DVR POWER ON (LONG shutter press)
      2) Wait SHORT press  -> DVR TOGGLE (SHORT press) (start recording)
      3) Wait SHORT press  -> DVR TOGGLE (SHORT press) (stop recording)
      4) Wait GRACE hold   -> DVR POWER OFF (LONG press)

    Verification criteria:
      - After power-off request, shutdown must exhibit:
          DVR_LED_FAST_BLINK  (ack / shutdown animation)
          then DVR_LED_OFF    (power-down complete)
        If FAST_BLINK->OFF is not observed before timeout, print FAIL.

    Notes:
      - Uses existing identifiers from: pins.h, timings.h, enums.h, event_queue.h, action_queue.h,
        executor.h, dvr_led.h, dvr_button.h, drv_fuel_gauge.h
      - Button semantics are owned by dvr_button.cpp (polling). NO ISR / attachInterrupt here.
      - DVR LED monitoring is owned by dvr_led.cpp (it likely uses INT1). DO NOT define INT1 ISR here.
      - Fuel gauge is polling-based; it emits EV_BAT_* events into the same event queue.
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
#include "dvr_button.h"
#include "drv_fuel_gauge.h"   // <<< FIXED: use quotes, and correct header name

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
// Shutdown signature verification (FAST_BLINK -> OFF)
// ============================================================================
static bool     s_shutdown_armed       = false;
static bool     s_shutdown_seen_fast   = false;
static uint32_t s_shutdown_deadline_ms = 0;

// ============================================================================
// DVR LED reporting (owned by dvr_led module)
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

static void dvr_led_report_poll(uint32_t now)
{
    dvr_led_poll(now);

    dvr_led_pattern_t p = dvr_led_get_pattern();

    if (p != s_last_pat)
    {
        s_last_pat = p;
        print_dvr_pattern(p);

        if (s_shutdown_armed)
        {
            if (!s_shutdown_seen_fast && p == DVR_LED_FAST_BLINK)
            {
                s_shutdown_seen_fast = true;
#if CFG_DEBUG_SERIAL
                Serial.println(F("Shutdown signature: FAST_BLINK observed."));
#endif
            }

            if (s_shutdown_seen_fast && p == DVR_LED_OFF)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("PASS: shutdown signature FAST_BLINK -> OFF observed."));
#endif
                s_shutdown_armed = false;
            }
        }
    }
}

// ============================================================================
// Battery logging (fuel gauge driver)
// ============================================================================
static uint32_t s_bat_next_print_ms = 0;

static const __FlashStringHelper* bat_state_str(battery_state_t s)
{
    switch (s)
    {
        case BAT_FULL:     return F("FULL");
        case BAT_HALF:     return F("HALF");
        case BAT_LOW:      return F("LOW");
        case BAT_CRITICAL: return F("CRITICAL");
        case BAT_UNKNOWN:
        default:           return F("UNKNOWN");
    }
}

// Print periodic "battery OK" style line (once per second, avoids serial spam)
static void battery_status_print_periodic(uint32_t now)
{
#if CFG_DEBUG_SERIAL
    if ((int32_t)(now - s_bat_next_print_ms) < 0)
        return;

    s_bat_next_print_ms = now + 1000;

    const uint16_t adc = drv_fuel_gauge_last_adc();
    const battery_state_t st = drv_fuel_gauge_last_state();
    const bool lockout = drv_fuel_gauge_lockout_active();

    Serial.print(F("BAT: "));
    Serial.print(bat_state_str(st));
    Serial.print(F(" adc="));
    Serial.print(adc);
    Serial.print(F(" lockout="));
    Serial.println(lockout ? F("YES") : F("NO"));
#else
    (void)now;
#endif
}

// Log EV_BAT_* events without consuming button events (stash-and-repush)
static void battery_event_log_poll(void)
{
#if CFG_DEBUG_SERIAL
    enum { STASH_MAX = 16 };
    event_t stash[STASH_MAX];
    uint8_t n = 0;

    event_t ev;
    while (eventq_pop(&ev))
    {
        if (ev.id == EV_BAT_STATE_CHANGED ||
            ev.id == EV_BAT_LOCKOUT_ENTER ||
            ev.id == EV_BAT_LOCKOUT_EXIT)
        {
            Serial.print(F("EV_BAT: id="));
            Serial.print((uint16_t)ev.id);
            Serial.print(F(" state="));
            Serial.print(bat_state_str((battery_state_t)ev.arg0));
            Serial.print(F(" adc="));
            Serial.print(ev.arg1);
            Serial.print(F(" reason="));
            Serial.println((uint16_t)ev.reason);
            continue;
        }

        if (n < STASH_MAX)
            stash[n++] = ev;
        else
            break;
    }

    for (uint8_t i = 0; i < n; i++)
        eventq_push(&stash[i]);
#endif
}

// ============================================================================
// Button gesture consume (EV_BTN_* produced ONLY by dvr_button)
// ============================================================================
typedef struct
{
    bool           valid;
    bool           is_grace;   // true for EV_BTN_LONG_PRESS
    uint16_t       press_ms;   // arg0 from event
    event_reason_t reason;
} button_gesture_t;

// ============================================================================
// Gesture policy:
// - While boot-waiting (STEP_WAIT_READY): discard taps (no buffering).
// - Otherwise: latch ONE gesture and never drop it until executed.
// ============================================================================
static bool             s_has_pending = false;
static button_gesture_t s_pending;

static bool consume_button_gesture(button_gesture_t *out_g)
{
    event_t ev;
    while (eventq_pop(&ev))
    {
        if (ev.id == EV_BTN_SHORT_PRESS)
        {
            out_g->valid    = true;
            out_g->is_grace = false;
            out_g->press_ms = ev.arg0;
            out_g->reason   = ev.reason;
            return true;
        }
        if (ev.id == EV_BTN_LONG_PRESS)
        {
            out_g->valid    = true;
            out_g->is_grace = true;
            out_g->press_ms = ev.arg0;
            out_g->reason   = ev.reason;
            return true;
        }

        // For this harness: ignore other events.
        // BUT: do not destroy them here; caller policy decides.
    }

    out_g->valid = false;
    return false;
}

// Discard ONLY EV_BTN_* during boot window, preserving any non-button events.
static void discard_button_gestures_preserve_others(void)
{
    enum { STASH_MAX = 16 };
    event_t stash[STASH_MAX];
    uint8_t n = 0;

    event_t ev;
    while (eventq_pop(&ev))
    {
        if (ev.id == EV_BTN_SHORT_PRESS || ev.id == EV_BTN_LONG_PRESS)
        {
            // discard
            continue;
        }

        if (n < STASH_MAX)
            stash[n++] = ev;
#if CFG_DEBUG_SERIAL
        else
            Serial.println(F("WARN: event stash overflow during boot-discard."));
#endif
    }

    for (uint8_t i = 0; i < n; i++)
        eventq_push(&stash[i]);
}

static void latch_one_gesture_if_none(void)
{
    if (s_has_pending)
        return;

    button_gesture_t g;
    if (!consume_button_gesture(&g))
        return;

    s_pending = g;
    s_has_pending = true;

#if CFG_DEBUG_SERIAL
    Serial.print(F("PRESS ms="));
    Serial.print(g.press_ms);
    Serial.print(F(" type="));
    Serial.print(g.is_grace ? F("GRACE(LONG)") : F("SHORT"));
    Serial.print(F(" reason="));
    Serial.println((uint16_t)g.reason);
#endif
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

    dvr_led_init();
    button_init();

    drv_fuel_gauge_init();

#if CFG_DEBUG_SERIAL
    Serial.println(F("SMOKE: short->ON, short->START, short->STOP, grace->OFF. dvr_button owns EV_BTN_*."));
#endif
}

void loop()
{
    const uint32_t now = millis();

    // Always run these
    executor_poll(now);
    dvr_led_report_poll(now);
    button_poll(now);

    // Fuel gauge: may enqueue EV_BAT_* events
    drv_fuel_gauge_poll(now);

    // Battery observability:
    //  - log EV_BAT_* events
    //  - print periodic status line
    battery_event_log_poll();
    battery_status_print_periodic(now);

    // Shutdown signature timeout check (reuses T_BOOT_TIMEOUT_MS)
    if (s_shutdown_armed && time_reached(now, s_shutdown_deadline_ms))
    {
#if CFG_DEBUG_SERIAL
        Serial.println(F("FAIL: shutdown signature not completed before timeout."));
#endif
        s_shutdown_armed = false;
    }

    // -------------------------------------------------------------------------
    // Input policy enforcement + non-blocking boot wait:
    // - STEP_WAIT_READY (boot window): discard taps, no buffering.
    // - Otherwise: latch ONE gesture and never drop it (even if executor is busy).
    // -------------------------------------------------------------------------
    if (s_step == STEP_WAIT_READY && !time_reached(now, s_deadline_ms))
    {
        // Booting: throw away button taps so they don't "queue up" for later.
        // NOTE: This preserves non-button events, including EV_BAT_* from fuel gauge.
        discard_button_gestures_preserve_others();
        return;
    }

    if (s_step == STEP_WAIT_READY && time_reached(now, s_deadline_ms))
    {
        s_step = STEP_WAIT_START_REC;
        // fall through into normal gesture handling
    }

    // Normal (READY/RECORD) behaviour: latch one gesture and keep it until executed.
    latch_one_gesture_if_none();

    if (!s_has_pending)
        return;

    // Determinism: do not enqueue while executor is busy (BUT do not lose the gesture)
    if (executor_busy())
        return;

    // Consume the latched gesture now that we're allowed to act
    button_gesture_t g = s_pending;
    s_has_pending = false;

    switch (s_step)
    {
        case STEP_WAIT_PWRON:
        {
            if (g.is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Grace hold ignored here; expecting SHORT to power on."));
#endif
                // Gesture is consumed (pending cleared above). Nothing else to do.
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
            if (g.is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Grace hold early: powering off (LONG press)"));
#endif
                enqueue_dvr_long(now);
                enqueue_beep(now, BEEP_DOUBLE);

                s_shutdown_armed       = true;
                s_shutdown_seen_fast   = false;
                s_shutdown_deadline_ms = now + (uint32_t)T_BOOT_TIMEOUT_MS;

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
            if (g.is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Grace hold: powering off (LONG press)"));
#endif
                enqueue_dvr_long(now);
                enqueue_beep(now, BEEP_DOUBLE);

                s_shutdown_armed       = true;
                s_shutdown_seen_fast   = false;
                s_shutdown_deadline_ms = now + (uint32_t)T_BOOT_TIMEOUT_MS;

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
            if (!g.is_grace)
            {
#if CFG_DEBUG_SERIAL
                Serial.println(F("Expect GRACE hold to power off (>=T_BTN_GRACE_MS). Short ignored."));
#endif
                // Gesture is consumed (pending cleared above). Nothing else to do.
                return;
            }

#if CFG_DEBUG_SERIAL
            Serial.println(F("Action: DVR POWER OFF (LONG press) [grace hold]"));
#endif
            enqueue_dvr_long(now);
            enqueue_beep(now, BEEP_DOUBLE);

            s_shutdown_armed       = true;
            s_shutdown_seen_fast   = false;
            s_shutdown_deadline_ms = now + (uint32_t)T_BOOT_TIMEOUT_MS;

            s_step = STEP_DONE;
            return;
        }

        case STEP_DONE:
        default:
            return;
    }
}
