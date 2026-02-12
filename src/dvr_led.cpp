// dvr_led.cpp
//
// Production DVR LED classifier for Randall
//
//  - Uses Arduino attachInterrupt() on PIN_DVR_STAT (INT1 on Nano / ATmega328P)
//  - LOW = DVR LED ON (per your NPN mirror)
//  - Edge-timestamp ring buffer (micros) => robust periods/duty even if loop jitters
//  - Sticky blink: once in blink, never overwritten by OFF/SOLID until truly quiet
//  - Classification uses timings.h thresholds (period + optional edge bounds)
//
// Public API:
//  - dvr_led_init()
//  - dvr_led_poll(now_ms)
//  - dvr_led_get_pattern()

#include <Arduino.h>

#include <dvr_led.h>
#include <pins.h>
#include <timings.h>
#include <enums.h>

// -----------------------------------------------------------------------------
// Local hygiene only (NOT a system timing constant)
// -----------------------------------------------------------------------------
static const uint16_t DVR_LED_GLITCH_US = 3000; // reject edges closer than 3ms

// -----------------------------------------------------------------------------
// ISR ring buffer (timestamps + level-after-edge)
// -----------------------------------------------------------------------------
static const uint8_t QN = 32;                  // power-of-two recommended
static volatile uint32_t s_q_ts_us[QN];
static volatile uint8_t  s_q_lvl[QN];
static volatile uint8_t  s_q_w = 0;
static volatile uint8_t  s_q_r = 0;

static volatile uint32_t s_last_isr_us = 0;

static void dvr_led_isr_change()
{
    const uint32_t now_us = micros();
    const uint32_t dt_us  = now_us - s_last_isr_us;
    if (dt_us < DVR_LED_GLITCH_US)
        return;

    s_last_isr_us = now_us;

    const uint8_t w = s_q_w;
    const uint8_t w_next = (uint8_t)((w + 1u) & (QN - 1u));
    if (w_next == s_q_r)
        return; // overflow => drop (rare, but safe)

    s_q_ts_us[w]  = now_us;
    s_q_lvl[w]    = (uint8_t)digitalRead(PIN_DVR_STAT); // level AFTER edge
    s_q_w         = w_next;
}

static bool pop_edge(uint32_t &ts_us, uint8_t &lvl_after)
{
    noInterrupts();
    if (s_q_r == s_q_w)
    {
        interrupts();
        return false;
    }
    const uint8_t r = s_q_r;
    ts_us     = s_q_ts_us[r];
    lvl_after = s_q_lvl[r];
    s_q_r     = (uint8_t)((r + 1u) & (QN - 1u));
    interrupts();
    return true;
}

static inline void clear_queue()
{
    noInterrupts();
    s_q_r = s_q_w;
    interrupts();
}

// -----------------------------------------------------------------------------
// Internal classifier state
// -----------------------------------------------------------------------------
static dvr_led_pattern_t s_pat = DVR_LED_UNKNOWN;

static uint8_t  s_level = HIGH;        // last sampled instantaneous level
static uint32_t s_last_edge_ms = 0;    // last accepted edge time (ms, for quiet-time)
static uint32_t s_prev_edge_us = 0;    // previous edge timestamp (us)
static uint8_t  s_prev_level = HIGH;   // level held BEFORE current edge

// For full-period (same-phase) timing
static uint32_t s_last_on_us  = 0;     // last transition into ON (LOW)
static uint32_t s_last_off_us = 0;     // last transition into OFF (HIGH)

// Last measured durations (ms) (optional for debugging / future use)
static uint16_t s_last_on_dur_ms  = 0;
static uint16_t s_last_off_dur_ms = 0;
static uint16_t s_last_period_ms  = 0;

// Hysteresis: require consecutive confirmations before switching blink state
static uint8_t s_slow_hits = 0;
static uint8_t s_fast_hits = 0;

static inline uint16_t u16_sat(uint32_t v)
{
    return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

static inline bool in_blink(dvr_led_pattern_t p)
{
    return (p == DVR_LED_SLOW_BLINK) || (p == DVR_LED_FAST_BLINK);
}

static inline bool in_range_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    return (v >= lo) && (v <= hi);
}

static inline dvr_led_pattern_t classify_from_measurements(uint16_t period_ms,
                                                          uint16_t on_dur_ms,
                                                          uint16_t off_dur_ms)
{
    // Primary: period gates the class
    if (in_range_u16(period_ms, T_FAST_MIN_MS, T_FAST_MAX_MS))
    {
        // Optional duty checks (kept tolerant): only apply if both edges non-zero
        if (on_dur_ms && off_dur_ms)
        {
            if (!in_range_u16(on_dur_ms,  T_FAST_EDGE_MIN_MS, T_FAST_EDGE_MAX_MS)) return DVR_LED_UNKNOWN;
            if (!in_range_u16(off_dur_ms, T_FAST_EDGE_MIN_MS, T_FAST_EDGE_MAX_MS)) return DVR_LED_UNKNOWN;
        }
        return DVR_LED_FAST_BLINK;
    }

    if (in_range_u16(period_ms, T_SLOW_MIN_MS, T_SLOW_MAX_MS))
    {
        if (on_dur_ms && off_dur_ms)
        {
            if (!in_range_u16(on_dur_ms,  T_SLOW_EDGE_MIN_MS, T_SLOW_EDGE_MAX_MS)) return DVR_LED_UNKNOWN;
            if (!in_range_u16(off_dur_ms, T_SLOW_EDGE_MIN_MS, T_SLOW_EDGE_MAX_MS)) return DVR_LED_UNKNOWN;
        }
        return DVR_LED_SLOW_BLINK;
    }

    return DVR_LED_UNKNOWN;
}

void dvr_led_init(void)
{
    pinMode(PIN_DVR_STAT, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_DVR_STAT), dvr_led_isr_change, CHANGE);

    const uint32_t now_ms = millis();

    s_pat = DVR_LED_UNKNOWN;

    s_level = (uint8_t)digitalRead(PIN_DVR_STAT);
    s_last_edge_ms = now_ms;

    s_prev_level = s_level;
    s_prev_edge_us = micros();

    s_last_on_us  = 0;
    s_last_off_us = 0;

    s_last_on_dur_ms = 0;
    s_last_off_dur_ms = 0;
    s_last_period_ms = 0;

    s_slow_hits = 0;
    s_fast_hits = 0;

    clear_queue();
}

void dvr_led_poll(uint32_t now_ms)
{
    // Always sample instantaneous level for SOLID/OFF decisions
    const uint8_t level_now = (uint8_t)digitalRead(PIN_DVR_STAT);
    s_level = level_now;

    // Drain all queued edges; compute real on/off durations and same-phase periods
    uint32_t ts_us;
    uint8_t lvl_after;

    bool saw_edge = false;

    while (pop_edge(ts_us, lvl_after))
    {
        saw_edge = true;
        s_last_edge_ms = now_ms;

        // Adjacent-edge duration: s_prev_level was held until this edge
        const uint32_t held_us = ts_us - s_prev_edge_us;
        const uint16_t held_ms = u16_sat(held_us / 1000u);

        if (s_prev_level == LOW)  s_last_on_dur_ms  = held_ms; // LED was ON
        else                      s_last_off_dur_ms = held_ms; // LED was OFF

        // Update previous-edge tracking
        s_prev_edge_us = ts_us;
        s_prev_level   = lvl_after;

        // Same-phase period: successive ON-edges or OFF-edges
        const bool led_on_now = (lvl_after == LOW);

        if (led_on_now)
        {
            if (s_last_on_us != 0)
            {
                const uint16_t per_ms = u16_sat((ts_us - s_last_on_us) / 1000u);
                s_last_period_ms = per_ms;

                const dvr_led_pattern_t bp =
                    classify_from_measurements(per_ms, s_last_on_dur_ms, s_last_off_dur_ms);

                if (bp == DVR_LED_SLOW_BLINK)
                {
                    if (s_slow_hits < 255) s_slow_hits++;
                    s_fast_hits = 0;
                }
                else if (bp == DVR_LED_FAST_BLINK)
                {
                    if (s_fast_hits < 255) s_fast_hits++;
                    s_slow_hits = 0;
                }
                else
                {
                    // Transitional oddities (e.g. stop-record flash) reset confidence
                    s_slow_hits = 0;
                    s_fast_hits = 0;
                }

                // Require 2 hits to change blink state (hysteresis)
                if (s_slow_hits >= 2) s_pat = DVR_LED_SLOW_BLINK;
                if (s_fast_hits >= 2) s_pat = DVR_LED_FAST_BLINK;
            }
            s_last_on_us = ts_us;
        }
        else
        {
            if (s_last_off_us != 0)
            {
                const uint16_t per_ms = u16_sat((ts_us - s_last_off_us) / 1000u);
                s_last_period_ms = per_ms;

                const dvr_led_pattern_t bp =
                    classify_from_measurements(per_ms, s_last_on_dur_ms, s_last_off_dur_ms);

                if (bp == DVR_LED_SLOW_BLINK)
                {
                    if (s_slow_hits < 255) s_slow_hits++;
                    s_fast_hits = 0;
                }
                else if (bp == DVR_LED_FAST_BLINK)
                {
                    if (s_fast_hits < 255) s_fast_hits++;
                    s_slow_hits = 0;
                }
                else
                {
                    s_slow_hits = 0;
                    s_fast_hits = 0;
                }

                if (s_slow_hits >= 2) s_pat = DVR_LED_SLOW_BLINK;
                if (s_fast_hits >= 2) s_pat = DVR_LED_FAST_BLINK;
            }
            s_last_off_us = ts_us;
        }

        // NOTE: While edges are flowing, we never overwrite blink with SOLID/OFF here.
        // Blink is sticky until quiet-time says blink ended.
    }

    // Quiet-time classification: only when genuinely quiet
    const uint32_t quiet_ms = now_ms - s_last_edge_ms;

    if (!in_blink(s_pat))
    {
        if (quiet_ms >= (uint32_t)T_SOLID_MS)
        {
            s_pat = (s_level == LOW) ? DVR_LED_SOLID : DVR_LED_OFF;
            s_slow_hits = 0;
            s_fast_hits = 0;
        }
        return;
    }

    // If we *were* blinking, only drop to SOLID/OFF after a long quiet.
    // Use T_SOLID_MS + T_SLOW_MAX_MS as "blink has definitely stopped".
    if (quiet_ms >= (uint32_t)T_SOLID_MS + (uint32_t)T_SLOW_MAX_MS)
    {
        s_pat = (s_level == LOW) ? DVR_LED_SOLID : DVR_LED_OFF;
        s_slow_hits = 0;
        s_fast_hits = 0;
        return;
    }

    (void)saw_edge; // reserved for future debug
}

dvr_led_pattern_t dvr_led_get_pattern(void)
{
    return s_pat;
}
