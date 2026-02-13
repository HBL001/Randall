// dvr_led.cpp
//
// Rise/fall, single-cycle classifier:
//  - Capture BOTH edges (LOW=ON, HIGH=OFF) with timestamps.
//  - As soon as we have one ON duration and one OFF duration, compute period and classify.
//  - Keep quiet-time logic for SOLID/OFF.
//
// IMPORTANT electrical assumption (as you stated / mirror test):
//   - PIN_DVR_STAT is a mirror of DVR LED
//   - LOW  = DVR LED ON
//   - HIGH = DVR LED OFF
//
// Notes:
//  - We timestamp edges in the ISR (micros()) so we don't lose timing if loop() is busy.
//  - ISR writes edges into a tiny ring buffer. poll() drains it and classifies.
//  - No new global timing constants are introduced; DVR_LED_GLITCH_MS remains local hygiene.

#include <Arduino.h>

#include "dvr_led.h"
#include "pins.h"
#include "timings.h"
#include "enums.h"

// -----------------------------------------------------------------------------
// Local hygiene: glitch reject for sniffer input edges (ms)
// -----------------------------------------------------------------------------
static const uint16_t DVR_LED_GLITCH_MS = 5;

// -----------------------------------------------------------------------------
// Edge ring buffer (ISR -> poll)
// -----------------------------------------------------------------------------
typedef struct {
    uint32_t t_us;     // micros timestamp at edge
    uint8_t  level;    // sampled pin level AFTER the change (0=LOW, 1=HIGH)
} dvr_led_edge_t;

static const uint8_t EDGEQ_N = 8;
static volatile dvr_led_edge_t s_edgeq[EDGEQ_N];
static volatile uint8_t s_edgeq_w = 0;
static volatile uint8_t s_edgeq_r = 0;

// Last classified pattern exposed to the rest of the system
static dvr_led_pattern_t s_pat = DVR_LED_UNKNOWN;

// Raw signal tracking
static uint8_t  s_level = 1;                // last known level (1=HIGH/off, 0=LOW/on)

// Timestamp tracking
static uint32_t s_last_edge_ms = 0;         // last accepted edge time in ms (for quiet-time)
static uint32_t s_boot_start_ms = 0;

// Duration tracking (ms)
static uint16_t s_last_on_ms     = 0;       // LOW duration
static uint16_t s_last_off_ms    = 0;       // HIGH duration
static uint16_t s_last_period_ms = 0;       // on+off once both known

// For duration calculation: record the start time of current level
static uint32_t s_level_start_us = 0;       // micros() when current s_level began

// -----------------------------------------------------------------------------
// Fast pin read in ISR
// -----------------------------------------------------------------------------
static inline uint8_t fastReadPin(uint8_t pin)
{
    uint8_t bit = digitalPinToBitMask(pin);
    uint8_t port = digitalPinToPort(pin);
    volatile uint8_t *in = portInputRegister(port);
    return ((*in & bit) ? 1u : 0u);
}

// Arduino ISR trampoline (INT1 on Nano = D3)
static void dvr_led_isr_change()
{
    const uint32_t t = micros();
    const uint8_t  lvl = fastReadPin(PIN_DVR_STAT);

    uint8_t w = s_edgeq_w;
    uint8_t w_next = (uint8_t)((w + 1u) % EDGEQ_N);

    // Drop edge on overflow (better than corrupting indices)
    if (w_next == s_edgeq_r)
        return;

    s_edgeq[w].t_us  = t;
    s_edgeq[w].level = lvl;
    s_edgeq_w = w_next;
}

static inline dvr_led_pattern_t classify_period(uint16_t period_ms,
                                                uint16_t on_ms,
                                                uint16_t off_ms)
{
    // Fast blink (error)
    if (period_ms >= T_FAST_MIN_MS && period_ms <= T_FAST_MAX_MS &&
        on_ms     >= T_FAST_EDGE_MIN_MS && on_ms  <= T_FAST_EDGE_MAX_MS &&
        off_ms    >= T_FAST_EDGE_MIN_MS && off_ms <= T_FAST_EDGE_MAX_MS)
        return DVR_LED_FAST_BLINK;

    // Slow blink (recording)
    if (period_ms >= T_SLOW_MIN_MS && period_ms <= T_SLOW_MAX_MS &&
        on_ms     >= T_SLOW_EDGE_MIN_MS && on_ms  <= T_SLOW_EDGE_MAX_MS &&
        off_ms    >= T_SLOW_EDGE_MIN_MS && off_ms <= T_SLOW_EDGE_MAX_MS)
        return DVR_LED_SLOW_BLINK;

    return DVR_LED_UNKNOWN;
}

void dvr_led_init(void)
{
    pinMode(PIN_DVR_STAT, INPUT);

    // INT1 = D3 on ATmega328P/Nano (your prior build uses this)
    attachInterrupt(digitalPinToInterrupt(PIN_DVR_STAT), dvr_led_isr_change, CHANGE);

    // Reset edge queue
    noInterrupts();
    s_edgeq_w = 0;
    s_edgeq_r = 0;
    interrupts();

    s_pat = DVR_LED_UNKNOWN;

    const uint32_t now_ms = millis();
    const uint32_t now_us = micros();

    s_level = (uint8_t)digitalRead(PIN_DVR_STAT);
    s_level_start_us = now_us;

    s_last_edge_ms = now_ms;

    s_last_on_ms = 0;
    s_last_off_ms = 0;
    s_last_period_ms = 0;

    s_boot_start_ms = now_ms;
}

static inline bool edgeq_pop(dvr_led_edge_t *out)
{
    bool ok = false;

    noInterrupts();
    uint8_t r = s_edgeq_r;
    if (r != s_edgeq_w)
    {
        *out = s_edgeq[r];
        s_edgeq_r = (uint8_t)((r + 1u) % EDGEQ_N);
        ok = true;
    }
    interrupts();

    return ok;
}

void dvr_led_poll(uint32_t now_ms)
{
    // Always sample current level for quiet-time logic
    const uint8_t level_now = (uint8_t)digitalRead(PIN_DVR_STAT);

    // Drain all pending edges captured by ISR
    dvr_led_edge_t e;
    while (edgeq_pop(&e))
    {
        const uint8_t new_level = (uint8_t)(e.level & 1u);

        // Ignore if "edge" doesn't actually change level (shouldn’t happen, but safe)
        if (new_level == s_level)
            continue;

        // Compute duration of the PREVIOUS level: (edge_time - level_start_time)
        uint32_t dt_us = e.t_us - s_level_start_us;
        uint32_t dt_ms = dt_us / 1000u;

        // Glitch reject
        if (dt_ms < (uint32_t)DVR_LED_GLITCH_MS)
        {
            // Still update bookkeeping to avoid getting stuck on pathological chatter:
            // move start to this time and accept new level as reality.
            s_level = new_level;
            s_level_start_us = e.t_us;
            continue;
        }

        // Record duration of previous level (LOW=ON, HIGH=OFF)
        if (s_level == LOW)
        {
            s_last_on_ms = (dt_ms > 0xFFFFu) ? 0xFFFFu : (uint16_t)dt_ms;
        }
        else
        {
            s_last_off_ms = (dt_ms > 0xFFFFu) ? 0xFFFFu : (uint16_t)dt_ms;
        }

        // Once we have both ON and OFF durations at least once, we have one full cycle
        if (s_last_on_ms != 0 && s_last_off_ms != 0)
        {
            const uint32_t p = (uint32_t)s_last_on_ms + (uint32_t)s_last_off_ms;
            s_last_period_ms = (p > 0xFFFFu) ? 0xFFFFu : (uint16_t)p;

            const dvr_led_pattern_t per_pat =
                classify_period(s_last_period_ms, s_last_on_ms, s_last_off_ms);

            if (per_pat != DVR_LED_UNKNOWN)
            {
                // Single-cycle decision: update immediately
                s_pat = per_pat;
            }
        }

        // Update raw level state to the new level, and set the new start time
        s_level = new_level;
        s_level_start_us = e.t_us;

        // Update last edge time for quiet-time classification
        const uint32_t edge_ms = (uint32_t)(e.t_us / 1000u);
        s_last_edge_ms = edge_ms;
    }

    // Quiet-time classification for SOLID/OFF (no edges for >= T_SOLID_MS)
    // This still matters, because SOLID/OFF is defined by absence of edges.
    const uint32_t quiet_ms = now_ms - s_last_edge_ms;

    if (quiet_ms >= (uint32_t)T_SOLID_MS)
    {
        if (level_now == LOW)
        {
            s_pat = DVR_LED_SOLID;
        }
        else
        {
            s_pat = DVR_LED_OFF;
        }
    }

    // Abnormal boot placeholder (left as-is; you can add signature logic later)
    (void)s_boot_start_ms;
}

dvr_led_pattern_t dvr_led_get_pattern(void)
{
    return s_pat;
}
