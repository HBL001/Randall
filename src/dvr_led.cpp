// dvr_led.cpp
//
//  - Use Arduino attachInterrupt() on PIN_DVR_STAT (INT1 on Nano / ATmega328P)
//  - NEVER alias blink into OFF<->SOLID 
//  - Robust, low-compute classification using edge COUNT over a rolling window

#include <Arduino.h>
#include <dvr_led.h>
#include <pins.h>
#include <timings.h>
#include <enums.h>

static const uint16_t DVR_LED_GLITCH_MS = 5;   // reject <5ms edges (noise / ringing)

// -----------------------------------------------------------------------------
// ISR-driven edge accounting
// -----------------------------------------------------------------------------
static volatile uint8_t s_isr_edge_count = 0;  // saturating count of CHANGE edges
static volatile uint8_t s_isr_dirty = 0;       // "something happened" hint

static void dvr_led_isr_change()
{
    // Count edges 
    if (s_isr_edge_count != 0xFFu)
        s_isr_edge_count++;

    s_isr_dirty = 1;
}

static inline uint8_t fetch_and_clear_edges_isr()
{
    uint8_t n;
    noInterrupts();
    n = s_isr_edge_count;
    s_isr_edge_count = 0;
    s_isr_dirty = 0;
    interrupts();
    return n;
}

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------
static dvr_led_pattern_t s_pat = DVR_LED_UNKNOWN;

// Raw level tracking
static uint8_t  s_level = HIGH;              // last sampled level at poll time
static uint32_t s_last_edge_ms = 0;          // last accepted edge time
static uint32_t s_last_level_change_ms = 0;  // for glitch reject

// Rolling window for blink classification
static uint32_t s_win_start_ms = 0;
static uint16_t s_win_edges = 0;             // accepted edges in current window (not ISR count)

// Boot marker (kept for future abnormal-boot signature work)
static uint32_t s_boot_start_ms = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline uint16_t u16_sat_add(uint16_t a, uint16_t b)
{
    uint32_t s = (uint32_t)a + (uint32_t)b;
    return (s > 0xFFFFu) ? 0xFFFFu : (uint16_t)s;
}

static inline void reset_window(uint32_t now_ms)
{
    s_win_start_ms = now_ms;
    s_win_edges = 0;
}

// Estimate the blink period (ms) using edges observed over window duration.
// Each full cycle yields ~2 edges (rising+falling). We use integer math.
static inline uint16_t estimate_period_ms(uint32_t win_dt_ms, uint16_t edges)
{
    if (edges < 4) return 0;                 // need >=2 cycles to be confident
    uint16_t cycles = (uint16_t)(edges / 2); // floor
    if (cycles == 0) return 0;
    return (uint16_t)(win_dt_ms / cycles);
}

// Classify based on estimated period only (robust).
// Your detailed per-edge min/max checks were the source of aliasing/missed evidence.
// If you later want duty-cycle checks, add them as a SECONDARY filter, not primary.
static inline dvr_led_pattern_t classify_by_period(uint16_t period_ms)
{
    if (period_ms == 0) return DVR_LED_UNKNOWN;

    if (period_ms >= T_FAST_MIN_MS && period_ms <= T_FAST_MAX_MS)
        return DVR_LED_FAST_BLINK;

    if (period_ms >= T_SLOW_MIN_MS && period_ms <= T_SLOW_MAX_MS)
        return DVR_LED_SLOW_BLINK;

    return DVR_LED_UNKNOWN;
}

void dvr_led_init(void)
{
    pinMode(PIN_DVR_STAT, INPUT);

    // Use the Arduino core interrupt plumbing (matches your working mirror test).
    attachInterrupt(digitalPinToInterrupt(PIN_DVR_STAT), dvr_led_isr_change, CHANGE);

    const uint32_t now = millis();

    s_pat = DVR_LED_UNKNOWN;

    s_level = (uint8_t)digitalRead(PIN_DVR_STAT);
    s_last_edge_ms = now;
    s_last_level_change_ms = now;

    reset_window(now);

    s_boot_start_ms = now;

    // clear ISR counters
    fetch_and_clear_edges_isr();
}

void dvr_led_poll(uint32_t now_ms)
{
    // Always sample instantaneous level for stable SOLID/OFF classification.
    const uint8_t level = (uint8_t)digitalRead(PIN_DVR_STAT);

    // Pull and clear edge count since last poll (atomic).
    const uint8_t edges = fetch_and_clear_edges_isr();

    // -----------------------------------------------------------------------------
    // Edge acceptance / glitch reject
    //
    // Important: We do NOT try to timestamp every edge in the ISR.
    // We only need to know "edges happened" and keep a stable rolling count.
    // -----------------------------------------------------------------------------
    if (edges > 0)
    {
        // Only accept edges if enough time has passed since last level change.
        // This rejects ringing / micro-glitches that can come from the mirror transistor.
        const uint32_t dt = now_ms - s_last_level_change_ms;
        if (dt >= DVR_LED_GLITCH_MS)
        {
            // Update last-edge time and rolling evidence
            s_last_edge_ms = now_ms;
            s_last_level_change_ms = now_ms;

            // Accumulate edges into the classification window
            s_win_edges = u16_sat_add(s_win_edges, (uint16_t)edges);

            // Update sampled level (phase) so stable classification later uses the latest value.
            s_level = level;

            // If window gets too long, reset it (prevents stale averages).
            // We base this on existing constants only.
            const uint32_t win_dt = now_ms - s_win_start_ms;

            // If we've been accumulating for "too long" without locking a period,
            // restart the window. Use slow max as a stable upper scale.
            if (win_dt > (uint32_t)T_SLOW_MAX_MS * 4u)
                reset_window(now_ms);

            // Attempt blink classification if we have enough evidence.
            // Use the window duration for period estimate.
            const uint32_t win_dt2 = now_ms - s_win_start_ms;
            const uint16_t per = estimate_period_ms(win_dt2, s_win_edges);
            const dvr_led_pattern_t bp = classify_by_period(per);

            if (bp != DVR_LED_UNKNOWN)
            {
                // Publish blink pattern and keep window running (it will continue to confirm).
                s_pat = bp;

                // Optional: once classified, you can shorten reactivity by resetting the window
                // so the next estimate tracks changes quickly. This also reduces latency.
                reset_window(now_ms);
            }

            // CRITICAL BEHAVIOUR:
            // While edges are happening, DO NOT overwrite s_pat with OFF/SOLID based on phase.
            // That was the root cause of OFF<->SOLID spam during blink.
        }
        else
        {
            // Glitch-like edge burst: ignore, but still update sampled level.
            // (Level sampling is cheap; this avoids staying stale.)
            s_level = level;
        }
    }
    else
    {
        // No edges observed this poll; still keep last sampled level.
        s_level = level;
    }

    // -----------------------------------------------------------------------------
    // Quiet-time classification (true stable states)
    // -----------------------------------------------------------------------------
    const uint32_t quiet_ms = now_ms - s_last_edge_ms;

    if (quiet_ms >= (uint32_t)T_SOLID_MS)
    {
        // Truly stable: classify as SOLID or OFF based on level.
        // Remember: LOW = LED ON.
        if (level == LOW)
            s_pat = DVR_LED_SOLID;
        else
            s_pat = DVR_LED_OFF;

        // Reset blink evidence now that we're stable again.
        reset_window(now_ms);
    }

    // -----------------------------------------------------------------------------
    // Abnormal boot signature placeholder
    // -----------------------------------------------------------------------------
    (void)s_boot_start_ms;
}

dvr_led_pattern_t dvr_led_get_pattern(void)
{
    return s_pat;
}
