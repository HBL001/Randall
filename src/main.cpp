/*
    Randall Sport Camera Controller — main.cpp (APP)

    main.cpp is plumbing + optional observability only.

    Architecture (runtime contract):
    - dvr_button is the ONLY producer of EV_BTN_* events (polling).
    - drv_fuel_gauge produces EV_BAT_* events (polling).
    - drv_dvr_led owns the dvr_led classifier and produces EV_DVR_LED_PATTERN_CHANGED.
    - drv_dvr_status consumes EV_DVR_LED_PATTERN_CHANGED and emits semantic EV_DVR_* events, incl EV_DVR_ERROR.
    - controller_fsm consumes events and enqueues actions (using ui_policy on transitions).
    - executor consumes actions and drives LED/BEEP/DVR press engines.

    Notes:
    - No action_queue meddling here (executor owns it).
    - Observability must NOT consume events unless it stashes + re-pushes.
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

#if CFG_DEBUG_SERIAL
// ============================================================================
// Debug helpers (serial only)
// ============================================================================

static const __FlashStringHelper* dvr_pat_str(dvr_led_pattern_t p)
{
    switch (p)
    {
        case DVR_LED_OFF:           return F("OFF");
        case DVR_LED_SOLID:         return F("SOLID");
        case DVR_LED_SLOW_BLINK:    return F("SLOW_BLINK");
        case DVR_LED_FAST_BLINK:    return F("FAST_BLINK");
        case DVR_LED_ABNORMAL_BOOT: return F("ABNORMAL_BOOT");
        case DVR_LED_UNKNOWN:
        default:                    return F("UNKNOWN");
    }
}

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

// ============================================================================
// DVR LED observability (print on change only)
// ============================================================================
static dvr_led_pattern_t s_last_pat_printed = DVR_LED_UNKNOWN;

static void dvr_led_observe(uint32_t now_ms)
{
    const dvr_led_pattern_t p = drv_dvr_led_last_pattern();
    if (p == s_last_pat_printed)
        return;

    s_last_pat_printed = p;

    Serial.print(F("DVR LED PATTERN -> "));
    Serial.println(dvr_pat_str(p));

    (void)now_ms;
}

// ============================================================================
// Battery logging (1 Hz + event logging)
// ============================================================================
static uint32_t s_bat_next_print_ms = 0;

static void battery_status_print_periodic(uint32_t now_ms)
{
    if ((int32_t)(now_ms - s_bat_next_print_ms) < 0)
        return;

    s_bat_next_print_ms = now_ms + 1000;

    const uint16_t adc = drv_fuel_gauge_last_adc();
    const battery_state_t st = drv_fuel_gauge_last_state();
    const bool lockout = drv_fuel_gauge_lockout_active();

    Serial.print(F("BAT: "));
    Serial.print(bat_state_str(st));
    Serial.print(F(" adc="));
    Serial.print(adc);
    Serial.print(F(" lockout="));
    Serial.println(lockout ? F("YES") : F("NO"));
}

// Log EV_BAT_* events but preserve the queue for everyone else (stash+repush)
static void battery_event_log_poll(void)
{
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
            Serial.print(bat_state_str((battery_state_t)(ev.arg0 & 0xFFu)));
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
}

#endif // CFG_DEBUG_SERIAL

// ============================================================================
// Setup / Loop
// ============================================================================

void setup()
{
#if CFG_DEBUG_SERIAL
    Serial.begin(115200);
    delay(200);
    Serial.println(F("APP: Randall controller starting..."));
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
    Serial.println(F("APP: controller_fsm + ui_policy + executor + button + fuel + dvr_led + dvr_status"));
#endif
}

void loop()
{
    const uint32_t now_ms = millis();

    // -------------------------------------------------------------------------
    // 1) Producers -> events
    //    (keep these first for responsiveness)
    // -------------------------------------------------------------------------
    button_poll(now_ms);
    drv_fuel_gauge_poll(now_ms);

    // DVR LED classifier + bridge -> EV_DVR_LED_PATTERN_CHANGED
    drv_dvr_led_poll(now_ms);

    // DVR semantic discriminator:
    // consumes EV_DVR_LED_PATTERN_CHANGED, emits EV_DVR_* incl EV_DVR_ERROR
    drv_dvr_status_poll(now_ms);

    // -------------------------------------------------------------------------
    // 2) Policy: consumes events -> emits actions
    // -------------------------------------------------------------------------
    controller_fsm_poll(now_ms);

    // -------------------------------------------------------------------------
    // 3) Executor: step engines + dispatch actions (non-blocking)
    // -------------------------------------------------------------------------
    executor_poll(now_ms);

#if CFG_DEBUG_SERIAL
    // -------------------------------------------------------------------------
    // 4) Observability (safe: event_queue only via stash+repush; never touch action_queue)
    //    Keep LAST so it cannot add latency to control behaviour.
    // -------------------------------------------------------------------------
    battery_event_log_poll();
    battery_status_print_periodic(now_ms);
    dvr_led_observe(now_ms);
#endif
}
