#include <Arduino.h>
#include <avr/interrupt.h>

#include "pins.h"
#include "timings.h"
#include "enums.h"
#include "event_queue.h"

// INT1 flag set by ISR only
static volatile uint8_t s_dvr_dirty = 0;

// last stable interpreted LED state (true = LED ON)
static bool     s_led_on = false;

// timing capture for classifier
static uint32_t s_last_change_ms = 0;
static uint32_t s_last_on_ms = 0;
static uint32_t s_last_off_ms = 0;

static dvr_led_pattern_t s_pat = DVR_LED_UNKNOWN;

// INT1 ISR: keep minimal
ISR(INT1_vect)
{
    s_dvr_dirty = 1;
}

static inline bool dvr_led_is_on_from_pin(void)
{
    // Your proven truth:
    // READ_DVR inverted by NPN: LOW = DVR LED ON
    return (digitalRead(PIN_DVR_STAT) == LOW);
}

static inline void push_evt(event_id_t id, uint32_t t_ms, uint16_t arg0, uint16_t arg1)
{
    event_t e;
    e.id     = id;
    e.t_ms   = t_ms;
    e.src    = SRC_DVR_LED;
    e.reason = EVR_EDGE_FALL;   // not strictly correct for both edges; not critical
    e.arg0   = arg0;
    e.arg1   = arg1;
    eventq_push(&e);
}

static inline void push_pattern_changed(uint32_t t_ms, dvr_led_pattern_t new_pat)
{
    event_t e;
    e.id     = EV_DVR_LED_PATTERN_CHANGED;
    e.t_ms   = t_ms;
    e.src    = SRC_DVR_LED;
    e.reason = EVR_CLASSIFIER_STABLE;
    e.arg0   = (uint16_t)new_pat;
    e.arg1   = 0;
    eventq_push(&e);
}

// Simple period-based classifier using CHANGE edges
// We classify based on the full period between ON edges or OFF edges.
// This is robust even if duty cycle is weird.
static dvr_led_pattern_t classify_period_ms(uint32_t period_ms)
{
    if (period_ms >= T_SLOW_MIN_MS && period_ms <= T_SLOW_MAX_MS)
        return DVR_LED_SLOW_BLINK;

    if (period_ms >= T_FAST_MIN_MS && period_ms <= T_FAST_MAX_MS)
        return DVR_LED_FAST_BLINK;

    return DVR_LED_UNKNOWN;
}

void dvr_led_init(void)
{
    // PIN_DVR_STAT already configured in pins_init(); ok to repeat defensively
    pinMode(PIN_DVR_STAT, INPUT);

    // Configure INT1 for ANY logical change (both edges)
    cli();
    EICRA &= ~(_BV(ISC11) | _BV(ISC10));
    EICRA |=  (_BV(ISC10));   // 01 = any change
    EIFR  |=  _BV(INTF1);
    EIMSK |=  _BV(INT1);
    sei();

    s_dvr_dirty = 1; // force initial sync
    s_led_on = dvr_led_is_on_from_pin();
    s_last_change_ms = millis();
    s_last_on_ms  = s_led_on ? s_last_change_ms : 0;
    s_last_off_ms = s_led_on ? 0 : s_last_change_ms;
    s_pat = DVR_LED_UNKNOWN;
}

void dvr_led_poll(uint32_t now_ms)
{
    // 1) Solid detection: if we've seen no edges for long enough
    // (Works even if you miss some interrupts: no edges => likely SOLID or OFF.)
    if (s_last_change_ms != 0)
    {
        const uint32_t quiet_ms = now_ms - s_last_change_ms;
        if (quiet_ms >= T_SOLID_MS)
        {
            // If the pin is currently ON and quiet, call it SOLID
            const bool cur_on = dvr_led_is_on_from_pin();
            if (cur_on && s_pat != DVR_LED_SOLID)
            {
                s_pat = DVR_LED_SOLID;
                push_pattern_changed(now_ms, s_pat);
            }
            else if (!cur_on && s_pat != DVR_LED_OFF)
            {
                s_pat = DVR_LED_OFF;
                push_pattern_changed(now_ms, s_pat);
            }
        }
    }

    // 2) Consume edge notification (coalesced)
    if (!s_dvr_dirty)
        return;

    cli();
    s_dvr_dirty = 0;
    sei();

    const bool cur_on = dvr_led_is_on_from_pin();
    if (cur_on == s_led_on)
        return; // no real change (belt + braces)

    // Update timing and emit raw edge event
    s_led_on = cur_on;
    s_last_change_ms = now_ms;

    if (cur_on)
    {
        // LED turned ON
        push_evt(EV_DVR_LED_EDGE_ON, now_ms, 1, 0);

        // period based on ON-to-ON
        if (s_last_on_ms != 0)
        {
            const uint32_t period = now_ms - s_last_on_ms;
            const dvr_led_pattern_t p = classify_period_ms(period);
            if (p != DVR_LED_UNKNOWN && p != s_pat)
            {
                s_pat = p;
                push_pattern_changed(now_ms, s_pat);
            }
        }
        s_last_on_ms = now_ms;
    }
    else
    {
        // LED turned OFF
        push_evt(EV_DVR_LED_EDGE_OFF, now_ms, 0, 0);

        // period based on OFF-to-OFF (also works)
        if (s_last_off_ms != 0)
        {
            const uint32_t period = now_ms - s_last_off_ms;
            const dvr_led_pattern_t p = classify_period_ms(period);
            if (p != DVR_LED_UNKNOWN && p != s_pat)
            {
                s_pat = p;
                push_pattern_changed(now_ms, s_pat);
            }
        }
        s_last_off_ms = now_ms;
    }
}
