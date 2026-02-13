/*
    SMOKE TEST (ARCH): controller_fsm + ui_policy + executor + drv_fuel_gauge + drv_dvr_led + drv_dvr_status

    - main.cpp is plumbing + observability only.
    - dvr_button is the ONLY producer of EV_BTN_* events (polling).
    - drv_fuel_gauge produces EV_BAT_* events (polling).
    - drv_dvr_led owns the dvr_led classifier and produces EV_DVR_LED_PATTERN_CHANGED.
    - drv_dvr_status consumes EV_DVR_LED_PATTERN_CHANGED and emits semantic EV_DVR_* events, incl EV_DVR_ERROR.
    - controller_fsm consumes events and enqueues actions (using ui_policy on transitions).
    - executor consumes actions and drives LED/BEEP/DVR press engines.

    Verification criteria:
      - When we observe DVR LED FAST_BLINK (shutdown ack/animation),
        we must observe DVR LED OFF before timeout (T_BOOT_TIMEOUT_MS).
        If not, print FAIL.

    NOTE: Do NOT pop/stash/repush the action_queue here.
*/

#include <Arduino.h>

#include "config.h"
#include "pins.h"
#include "timings.h"
#include "enums.h"

#include "event_queue.h"
#include "action_queue.h"

#include "executor.h"
#include "dvr_button.h"
#include "drv_fuel_gauge.h"

#include "drv_dvr_led.h"
#include "drv_dvr_status.h"

#include "controller_fsm.h"

// ----------------------------------------------------------------------------
// Time helper
// ----------------------------------------------------------------------------
static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

// ============================================================================
// Shutdown signature verification (arm on FAST_BLINK -> expect OFF)
// ============================================================================

static bool              s_shutdown_armed       = false;
static uint32_t          s_shutdown_deadline_ms = 0;
static dvr_led_pattern_t s_last_pat_printed     = DVR_LED_UNKNOWN;

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

static void dvr_led_observe_and_check_shutdown(uint32_t now_ms)
{
    const dvr_led_pattern_t p = drv_dvr_led_last_pattern();

    // Print only on change (local observability; no queue meddling)
    if (p != s_last_pat_printed)
    {
        s_last_pat_printed = p;
        print_dvr_pattern(p);

        if (p == DVR_LED_FAST_BLINK && !s_shutdown_armed)
        {
            s_shutdown_armed       = true;
            s_shutdown_deadline_ms = now_ms + (uint32_t)T_BOOT_TIMEOUT_MS;
#if CFG_DEBUG_SERIAL
            Serial.println(F("SMOKE: shutdown signature armed (FAST_BLINK observed)."));
#endif
        }

        if (s_shutdown_armed && p == DVR_LED_OFF)
        {
#if CFG_DEBUG_SERIAL
            Serial.println(F("PASS: shutdown signature FAST_BLINK -> OFF observed."));
#endif
            s_shutdown_armed = false;
        }
    }

    if (s_shutdown_armed && time_reached(now_ms, s_shutdown_deadline_ms))
    {
#if CFG_DEBUG_SERIAL
        Serial.println(F("FAIL: shutdown signature not completed before timeout."));
#endif
        s_shutdown_armed = false;
    }
}

// ============================================================================
// Battery logging (1 Hz + event logging)
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

// Log EV_BAT_* events but preserve the queue for everyone else (stash+repush)
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
        (void)eventq_push(&stash[i]);
#endif
}

// ============================================================================
// Setup / Loop
// ============================================================================

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

    // Producers
    button_init();
    drv_fuel_gauge_init();
    drv_dvr_led_init();         // owns dvr_led classifier internally
    drv_dvr_status_init();      // semantic discriminator (FAST_BLINK persistence => ERR_DVR_CARD_ERROR)

    // Policy/FSM
    controller_fsm_init();

#if CFG_DEBUG_SERIAL
    Serial.println(F("SMOKE(ARCH): controller_fsm + ui_policy + executor + drv_fuel_gauge + drv_dvr_led + drv_dvr_status"));
#endif
}

void loop()
{
    const uint32_t now = millis();

    // 1) Low-level producers -> events
    button_poll(now);
    drv_fuel_gauge_poll(now);

    // 2) DVR LED classifier + bridge -> EV_DVR_LED_PATTERN_CHANGED
    drv_dvr_led_poll(now);

    // 3) DVR semantic discriminator (consumes LED pattern events, emits EV_DVR_* incl EV_DVR_ERROR)
    drv_dvr_status_poll(now);

    // 4) Observability (does NOT touch action_queue; event_queue only via safe stash for BAT logging)
    battery_event_log_poll();
    battery_status_print_periodic(now);
    dvr_led_observe_and_check_shutdown(now);

    // 5) Controller consumes events -> emits actions
    controller_fsm_poll(now);

    // 6) Executor consumes actions -> drives outputs/press engines
    executor_poll(now);
}
