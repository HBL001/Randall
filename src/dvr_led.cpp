#include <Arduino.h>

#include "dvr_led.h"
#include "pins.h"
#include "timings.h"
#include "enums.h"

// -----------------------------------------------------------------------------
// Assumptions (as per your mirror test / spec):
//   - PIN_DVR_STAT is connected to a mirror of the DVR LED.
//   - LOW  = DVR LED ON
//   - HIGH = DVR LED OFF
// -----------------------------------------------------------------------------

// Debounce/glitch reject for edges on the sniffer input (ms).
// This is NOT a new "timing constant" in your global system; it's local hygiene.
// Keep it small so it doesn't mask real FAST blink edges.
static const uint16_t DVR_LED_GLITCH_MS = 5;

// ISR flag: "we saw at least one change since last poll"
static volatile uint8_t s_dvr_dirty = 0;

// Last classified pattern exposed to the rest of the system
static dvr_led_pattern_t s_pat = DVR_LED_UNKNOWN;

// Raw signal tracking
static uint8_t  s_level = 1;             // last sampled level (1=HIGH/off, 0=LOW/on)
static uint32_t s_last_edge_ms = 0;       // time of last accepted edge
static uint32_t s_last_level_change_ms = 0;

// Duration tracking (ms)
static uint16_t s_last_on_ms  = 0;        // most recent LOW duration
static uint16_t s_last_off_ms = 0;        // most recent HIGH duration
static uint16_t s_last_period_ms = 0;     // on+off for a full cycle when we have it

// For "abnormal boot" placeholder: we keep the enum but do not overfit the rule
// without your exact signature logic wired in. You can extend later.
static uint32_t s_boot_start_ms = 0;

// Arduino ISR trampoline
static void dvr_led_isr_change()
{
    s_dvr_dirty = 1;
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

    // INT1 = D3 on ATmega328P/Nano
    attachInterrupt(digitalPinToInterrupt(PIN_DVR_STAT), dvr_led_isr_change, CHANGE);

    s_dvr_dirty = 0;
    s_pat = DVR_LED_UNKNOWN;

    s_level = (uint8_t)digitalRead(PIN_DVR_STAT);
    const uint32_t now = millis();
    s_last_edge_ms = now;
    s_last_level_change_ms = now;

    s_last_on_ms = 0;
    s_last_off_ms = 0;
    s_last_period_ms = 0;

    s_boot_start_ms = now;
}

void dvr_led_poll(uint32_t now_ms)
{
    // Always sample level (needed for SOLID/OFF quiet-time classification)
    const uint8_t level = (uint8_t)digitalRead(PIN_DVR_STAT);

    // -------------------------------------------------------------------------
    // Edge processing: if ISR flagged dirty, consume it by examining pin level.
    // We do NOT rely on edge timestamps from ISR; we use poll time for ms-level
    // decoding (your thresholds are ms-scale).
    // -------------------------------------------------------------------------
    if (s_dvr_dirty)
    {
        s_dvr_dirty = 0;

        // If the level hasn't actually changed, ignore (spurious ISR wake)
        if (level != s_level)
        {
            const uint32_t dt = now_ms - s_last_level_change_ms;

            // Glitch reject: ignore edges faster than a few ms
            if (dt >= DVR_LED_GLITCH_MS)
            {
                // We are transitioning AWAY from previous level; record its duration.
                // Remember: LOW=ON, HIGH=OFF.
                if (s_level == LOW)
                {
                    // Previous was ON (LOW)
                    s_last_on_ms = (dt > 0xFFFFu) ? 0xFFFFu : (uint16_t)dt;
                }
                else
                {
                    // Previous was OFF (HIGH)
                    s_last_off_ms = (dt > 0xFFFFu) ? 0xFFFFu : (uint16_t)dt;
                }

                // When we have both an ON and OFF duration, we have a full cycle period.
                if (s_last_on_ms != 0 && s_last_off_ms != 0)
                {
                    const uint32_t p = (uint32_t)s_last_on_ms + (uint32_t)s_last_off_ms;
                    s_last_period_ms = (p > 0xFFFFu) ? 0xFFFFu : (uint16_t)p;

                    // Period-based classification takes priority over quiet-time
                    const dvr_led_pattern_t per_pat =
                        classify_period(s_last_period_ms, s_last_on_ms, s_last_off_ms);

                    if (per_pat != DVR_LED_UNKNOWN)
                    {
                        s_pat = per_pat;
                    }
                }

                // Update raw state
                s_level = level;
                s_last_level_change_ms = now_ms;
                s_last_edge_ms = now_ms;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Quiet-time classification:
    //   - SOLID: stable LOW (LED ON) with no edges for >= T_SOLID_MS
    //   - OFF:   stable HIGH (LED OFF) with no edges for >= T_SOLID_MS
    //
    // This is what makes SOLID possible even without edges.
    // -------------------------------------------------------------------------
    const uint32_t quiet_ms = now_ms - s_last_edge_ms;

    if (quiet_ms >= (uint32_t)T_SOLID_MS)
    {
        if (level == LOW)
        {
            // LED is ON continuously
            s_pat = DVR_LED_SOLID;
        }
        else
        {
            // LED is OFF continuously
            s_pat = DVR_LED_OFF;
        }
    }

    // -------------------------------------------------------------------------
    // Abnormal boot: keep enum available, but don't guess a signature here.
    // You already have T_BOOT_WINDOW_MS / T_ABN_* constants; once you confirm the
    // exact signature logic, we can promote this from UNKNOWN->ABNORMAL_BOOT.
    // -------------------------------------------------------------------------
    (void)s_boot_start_ms;
}

dvr_led_pattern_t dvr_led_get_pattern(void)
{
    return s_pat;
}
