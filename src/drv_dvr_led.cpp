// drv_dvr_led.cpp
//
// DVR LED -> Event bridge
//
// Produces: EV_DVR_LED_PATTERN_CHANGED (arg0=dvr_led_pattern_t)
//
// Notes:
// - dvr_led.cpp already has hysteresis for blink detection.
// - This bridge adds a small stability filter to reduce chatter
//   between UNKNOWN/SOLID/OFF at start-up or when wiring is noisy.
// - No buffering: emits only on accepted changes.
//

#include "drv_dvr_led.h"

#include <Arduino.h>

#include "dvr_led.h"
#include "event_queue.h"
#include "enums.h"

// -----------------------------------------------------------------------------
// Module-local hygiene only (NOT global timing constants)
// -----------------------------------------------------------------------------
static const uint16_t kMinEmitSpacingMs = 30;   // rate-limit rapid churn
static const uint8_t  kStableReq        = 2;    // consecutive polls to accept change

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static dvr_led_pattern_t s_reported       = DVR_LED_UNKNOWN;
static dvr_led_pattern_t s_candidate      = DVR_LED_UNKNOWN;
static uint8_t           s_candidate_cnt  = 0;

static uint32_t          s_last_emit_ms   = 0;
static uint32_t          s_last_change_ms = 0;

// -----------------------------------------------------------------------------
// Local helpers
// -----------------------------------------------------------------------------
static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

// If your event_t includes optional metadata fields, define these in config.h
// (or build flags) to enable population.
//   CFG_EVENT_HAS_SRC=1    -> event_t has .src
//   CFG_EVENT_HAS_REASON=1 -> event_t has .reason
#ifndef CFG_EVENT_HAS_SRC
#define CFG_EVENT_HAS_SRC 0
#endif
#ifndef CFG_EVENT_HAS_REASON
#define CFG_EVENT_HAS_REASON 0
#endif

static inline void emit_led_event(uint32_t now_ms, dvr_led_pattern_t pat)
{
    event_t e;
    e.t_ms   = now_ms;
    e.id     = EV_DVR_LED_PATTERN_CHANGED;
    e.src    = SRC_DVR_LED;
    e.reason = EVR_CLASSIFIER_STABLE;
    e.arg0   = (uint16_t)((uint8_t)pat);  // explicit width
    e.arg1   = 0;

    (void)eventq_push(&e);
}


// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void drv_dvr_led_init(void)
{
    dvr_led_init();

    s_reported       = DVR_LED_UNKNOWN;
    s_candidate      = DVR_LED_UNKNOWN;
    s_candidate_cnt  = 0;

    s_last_emit_ms   = 0;
    s_last_change_ms = 0;
}

void drv_dvr_led_poll(uint32_t now_ms)
{
    // Keep classifier alive
    dvr_led_poll(now_ms);

    const dvr_led_pattern_t p = dvr_led_get_pattern();

    // Candidate stability filter
    if (p != s_candidate)
    {
        s_candidate     = p;
        s_candidate_cnt = 1;
        return;
    }

    if (s_candidate_cnt < 255)
        s_candidate_cnt++;

    if (s_candidate_cnt < kStableReq)
        return;

    // Candidate is stable enough. Only emit if it differs from reported.
    if (s_candidate == s_reported)
        return;

    // Rate-limit chatter
    if (!time_reached(now_ms, s_last_emit_ms + kMinEmitSpacingMs))
        return;

    s_reported       = s_candidate;
    s_last_emit_ms   = now_ms;
    s_last_change_ms = now_ms;

    emit_led_event(now_ms, s_reported);
}

dvr_led_pattern_t drv_dvr_led_last_pattern(void)
{
    return s_reported;
}

uint32_t drv_dvr_led_last_change_ms(void)
{
    return s_last_change_ms;
}
